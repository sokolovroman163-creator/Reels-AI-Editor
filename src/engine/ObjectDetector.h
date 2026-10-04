#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>

namespace drift {

// YOLOX object detection on ONNX Runtime, used to label detected scenes with what is in
// them. Apache-2.0 upstream, which is why it is YOLOX rather than an Ultralytics YOLO —
// those release their trained weights under AGPL-3.0.
//
// The model ships as the `object-model` addon; nothing here downloads it.

struct Detection
{
    QRectF box;  // in source-image pixels
    int classId = 0;
    QString label;
    double score = 0.0;
};

// --- pure decoding ----------------------------------------------------------
// Separated from the session so the coordinate maths is testable without a model.

// YOLOX's ONNX export does NOT bake in the grid decode — verified against the published
// yolox_tiny.onnx, whose raw cx/cy come out around -2..2 rather than spanning the input.
// So the anchor grid has to be reapplied here: for strides 8, 16 and 32 over grids of
// (size/stride)^2 cells each, laid out row-major with x varying fastest,
//
//     cx = (raw_cx + grid_x) * stride      w = exp(raw_w) * stride
//
// `data` is [anchors, 5 + classCount]: cx, cy, w, h, objectness, then per-class scores.
// Returns boxes in letterboxed-input pixels; divide by the letterbox ratio to get source
// pixels. Confidence is objectness * class score, as in YOLOX's own demo.
QList<Detection> decodeYoloxOutput(const float *data, int anchors, int classCount, int inputSize,
                                   double scoreThreshold, const QStringList &classNames);

// Greedy non-maximum suppression, applied per class — two different objects legitimately
// overlap, two boxes on the same object do not.
QList<Detection> nonMaximumSuppression(QList<Detection> detections, double iouThreshold);

// YOLOX letterboxes by scaling to fit and padding bottom/right with grey, leaving the image
// at the TOP-LEFT rather than centring it. That is why mapping a box back to source pixels
// is a plain divide with no offset term.
double letterboxRatio(int sourceWidth, int sourceHeight, int inputSize);

// Confidence below which a detection is dropped, and the IoU at which two boxes of the same
// class are considered the same object. Both are YOLOX's own demo defaults.
inline constexpr double kObjectScoreThreshold = 0.30;
inline constexpr double kObjectIouThreshold = 0.45;

class ObjectDetector
{
public:
    static ObjectDetector &instance();

    // Whether the model files are on disk. Cheap — no session is created.
    static bool modelPresent();

    // Resolves the model and creates the session on first use. False if it is missing or
    // failed to load (see lastError()).
    bool available();
    QString lastError() const;

    // Objects in one frame, already NMS'd and mapped back to `frame`'s pixel coordinates.
    // Empty when the model is unavailable or nothing clears the threshold.
    QList<Detection> detect(const QImage &frame);

    ObjectDetector(const ObjectDetector &) = delete;
    ObjectDetector &operator=(const ObjectDetector &) = delete;

private:
    ObjectDetector();
    ~ObjectDetector();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
