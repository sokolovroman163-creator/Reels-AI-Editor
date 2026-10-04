#include "ObjectDetector.h"

#include "GpuPackageParse.h"
#include "OrtRuntime.h"
#include "OrtSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

using drift::ort::ortPath;
using drift::ort::sessionNames;

namespace drift {
namespace {

const char *const kModelFile = "yolox_tiny.onnx";
const char *const kConstantsFile = "constants.json";

// YOLOX's feature-pyramid strides. The anchor count is the sum of (size/stride)^2 over these
// — 3549 at 416, which is what the published yolox_tiny.onnx reports as its output length.
constexpr int kStrides[] = {8, 16, 32};

double intersectionOverUnion(const QRectF &a, const QRectF &b)
{
    const QRectF overlap = a.intersected(b);
    if (overlap.isEmpty())
        return 0.0;
    const double intersection = overlap.width() * overlap.height();
    const double combined = a.width() * a.height() + b.width() * b.height() - intersection;
    return combined > 0.0 ? intersection / combined : 0.0;
}

QString resolveObjectModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_OBJECT_MODEL_DIR"), QStringLiteral("models/yolox-tiny"),
        QStringLiteral("object-model"));

    // A directory only counts as a model when every piece is there — a half-downloaded folder
    // must not look installed.
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QLatin1String(kModelFile)))
            && QFile::exists(dir.filePath(QLatin1String(kConstantsFile)))) {
            return root;
        }
    }
    return {};
}

} // namespace

double letterboxRatio(int sourceWidth, int sourceHeight, int inputSize)
{
    if (sourceWidth <= 0 || sourceHeight <= 0 || inputSize <= 0)
        return 0.0;
    return std::min(double(inputSize) / sourceHeight, double(inputSize) / sourceWidth);
}

QList<Detection> decodeYoloxOutput(const float *data, int anchors, int classCount, int inputSize,
                                   double scoreThreshold, const QStringList &classNames)
{
    QList<Detection> out;
    if (!data || anchors <= 0 || classCount <= 0 || inputSize <= 0)
        return out;

    const int stride = 5 + classCount;

    int anchor = 0;
    for (int strideValue : kStrides) {
        const int cells = inputSize / strideValue;
        for (int gy = 0; gy < cells; ++gy) {
            for (int gx = 0; gx < cells; ++gx, ++anchor) {
                if (anchor >= anchors)
                    return out; // the model's grid does not match this input size
                const float *row = data + qsizetype(anchor) * stride;

                const double objectness = row[4];
                if (objectness < scoreThreshold)
                    continue; // no class score can rescue this, since they multiply

                int bestClass = 0;
                double bestScore = 0.0;
                for (int c = 0; c < classCount; ++c) {
                    const double score = row[5 + c];
                    if (score > bestScore) {
                        bestScore = score;
                        bestClass = c;
                    }
                }

                const double confidence = objectness * bestScore;
                if (confidence < scoreThreshold)
                    continue;

                const double cx = (double(row[0]) + gx) * strideValue;
                const double cy = (double(row[1]) + gy) * strideValue;
                const double w = std::exp(double(row[2])) * strideValue;
                const double h = std::exp(double(row[3])) * strideValue;

                Detection detection;
                detection.box = QRectF(cx - w / 2.0, cy - h / 2.0, w, h);
                detection.classId = bestClass;
                detection.label = bestClass < classNames.size() ? classNames.at(bestClass)
                                                                : QString::number(bestClass);
                detection.score = confidence;
                out.append(detection);
            }
        }
    }
    return out;
}

QList<Detection> nonMaximumSuppression(QList<Detection> detections, double iouThreshold)
{
    std::sort(detections.begin(), detections.end(),
              [](const Detection &a, const Detection &b) { return a.score > b.score; });

    QList<Detection> kept;
    for (const Detection &candidate : std::as_const(detections)) {
        bool suppressed = false;
        for (const Detection &already : std::as_const(kept)) {
            // Per class: a person standing in front of a car is two valid boxes over the same
            // pixels, and suppressing across classes would throw one of them away.
            if (already.classId != candidate.classId)
                continue;
            if (intersectionOverUnion(already.box, candidate.box) > iouThreshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
            kept.append(candidate);
    }
    return kept;
}

// --- session ----------------------------------------------------------------

struct ObjectDetector::Impl
{
    bool loadAttempted = false;
    bool loaded = false;
    QString error;
    QString modelDir;

    int inputSize = 416;
    QStringList classNames;

    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;

    bool loadConstants(const QString &dir);
    bool ensureLoaded();
};

bool ObjectDetector::Impl::loadConstants(const QString &dir)
{
    QFile file(QDir(dir).filePath(QLatin1String(kConstantsFile)));
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Could not read the object model's constants.json");
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    inputSize = root.value(QStringLiteral("inputSize")).toInt(416);

    classNames.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("classes")).toArray())
        classNames.append(value.toString());

    if (inputSize <= 0 || classNames.isEmpty()) {
        error = QStringLiteral("Object model constants.json has unusable values");
        return false;
    }
    return true;
}

bool ObjectDetector::Impl::ensureLoaded()
{
    if (loadAttempted)
        return loaded;

    modelDir = resolveObjectModelDir();
    if (modelDir.isEmpty()) {
        // Deliberately not latched: the model arrives as an addon the user can install while
        // the app is running, and latching here would make it need a restart. A model that is
        // present but fails to load is latched below, since retrying just repeats the failure.
        error = QStringLiteral("Object model not found. Install the object detection addon, "
                               "place it in models/yolox-tiny, or set DRIFT_OBJECT_MODEL_DIR.");
        return false;
    }
    if (!drift::ort::ensureLoaded(&error))
        return false;
    loadAttempted = true;

    if (!loadConstants(modelDir))
        return false;

    Ort::Env &ortEnv = drift::ort::env();
    const QDir dir(modelDir);
    QString buildError;
    const bool built = drift::ort::buildSessions(
        ortEnv, "object", false, &buildError, [&](Ort::SessionOptions &opts) {
            session = std::make_unique<Ort::Session>(
                ortEnv, ortPath(dir.filePath(QLatin1String(kModelFile))).c_str(), opts);
        });
    if (!built) {
        error = QStringLiteral("Failed to load object model: ") + buildError;
        session.reset();
        return false;
    }

    inputs = sessionNames(*session, true);
    outputs = sessionNames(*session, false);

    loaded = true;
    return true;
}

ObjectDetector::ObjectDetector() : d(std::make_unique<Impl>()) { }
ObjectDetector::~ObjectDetector() = default;

ObjectDetector &ObjectDetector::instance()
{
    static ObjectDetector detector;
    return detector;
}

bool ObjectDetector::modelPresent()
{
    return !resolveObjectModelDir().isEmpty();
}

bool ObjectDetector::available()
{
    return d->ensureLoaded();
}

QString ObjectDetector::lastError() const
{
    return d->error;
}

QList<Detection> ObjectDetector::detect(const QImage &frame)
{
    if (frame.isNull() || !d->ensureLoaded())
        return {};

    const int size = d->inputSize;
    const double ratio = letterboxRatio(frame.width(), frame.height(), size);
    if (ratio <= 0.0)
        return {};

    const int scaledW = int(frame.width() * ratio);
    const int scaledH = int(frame.height() * ratio);
    const QImage scaled = frame.convertToFormat(QImage::Format_RGBA8888)
                              .scaled(scaledW, scaledH, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);

    // YOLOX takes planar BGR at the raw 0..255 scale — no mean/std normalisation, unlike most
    // detectors — padded with 114 grey. The image sits at the top-left rather than centred,
    // which is why mapping boxes back is a plain divide by `ratio` with no offset.
    std::vector<float> input(size_t(3) * size * size, 114.0f);
    const size_t plane = size_t(size) * size;
    for (int y = 0; y < scaled.height(); ++y) {
        const uchar *row = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const size_t at = size_t(y) * size + x;
            input[at] = float(row[x * 4 + 2]);              // B
            input[plane + at] = float(row[x * 4 + 1]);      // G
            input[2 * plane + at] = float(row[x * 4]);      // R
        }
    }

    const std::array<int64_t, 4> shape{1, 3, size, size};
    Ort::Value tensor = Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(), input.data(),
                                                        input.size(), shape.data(), shape.size());

    std::vector<Ort::Value> result;
    try {
        result = d->session->Run(Ort::RunOptions{nullptr}, drift::ort::cstrs(d->inputs).data(),
                                 &tensor, 1, drift::ort::cstrs(d->outputs).data(), 1);
    } catch (const Ort::Exception &e) {
        d->error = QString::fromUtf8(e.what());
        return {};
    }
    if (result.empty())
        return {};

    const auto info = result.front().GetTensorTypeAndShapeInfo();
    const auto outShape = info.GetShape();
    if (outShape.size() != 3)
        return {};
    const int anchors = int(outShape[1]);
    const int classCount = int(outShape[2]) - 5;
    if (anchors <= 0 || classCount <= 0)
        return {};

    QList<Detection> detections =
        decodeYoloxOutput(result.front().GetTensorData<float>(), anchors, classCount, size,
                          kObjectScoreThreshold, d->classNames);
    detections = nonMaximumSuppression(std::move(detections), kObjectIouThreshold);

    // Back to the caller's pixel space.
    for (Detection &detection : detections) {
        detection.box = QRectF(detection.box.x() / ratio, detection.box.y() / ratio,
                               detection.box.width() / ratio, detection.box.height() / ratio);
    }
    return detections;
}

} // namespace drift
