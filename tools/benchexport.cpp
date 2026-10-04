// Side-by-side export timing: GPU NV12 pipeline vs serial RGBA + sws_scale.
//
//   benchexport <video> [out_dir]
#include "core/Clip.h"
#include "core/Project.h"
#include "core/Track.h"
#include "engine/ClipReaderPool.h"
#include "engine/EffectCatalog.h"
#include "engine/Exporter.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"
#include "engine/GpuCompositor.h"
#include "engine/MediaProbe.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTextStream>

#include <cstdio>

namespace {

drift::Effect makeEffect(const QString &catalogId)
{
    drift::Effect effect;
    effect.catalogId = catalogId;
    return effect;
}

bool runTimedExport(const drift::Project &project, const ExportSettings &settings, const QString &outPath,
                    const QString &label, QTextStream &out, QTextStream &err)
{
    out << label << "…\n";
    out.flush();

    QElapsedTimer timer;
    timer.start();
    QString error;
    int lastPct = -1;
    const bool ok = Exporter::run(project, settings, outPath, &error, [&](double p) {
        const int pct = int(p * 100.0);
        if (pct != lastPct && pct % 10 == 0) {
            std::fprintf(stderr, "  %s %d%%\n", qUtf8Printable(label), pct);
            lastPct = pct;
        }
        return true;
    });
    const qint64 ms = timer.elapsed();

    if (!ok) {
        err << label << " failed: " << error << "\n";
        return false;
    }

    const QFileInfo info(outPath);
    const double sec = ms / 1000.0;
    const double timelineSec = project.durationUs() / 1e6;
    out << Qt::fixed;
    out.setRealNumberPrecision(2);
    out << "  wall   " << sec << " s\n";
    out << "  speed  " << (timelineSec / sec) << "× realtime\n";
    out << "  size   " << (info.size() / (1024.0 * 1024.0)) << " MiB  " << outPath << "\n";
    out.flush();
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    reloadFontCatalog();
    reloadEffectCatalog();

    QTextStream out(stdout);
    QTextStream err(stderr);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        err << "usage: benchexport <video> [out_dir]\n";
        return 1;
    }

    const QString path = args.at(1);
    const QString outDir = args.size() > 2 ? args.at(2) : QStringLiteral("/tmp");

    const QVariantMap vaapi = Exporter::videoCodecById(QStringLiteral("h264_vaapi"));
    if (!vaapi.value(QStringLiteral("available")).toBool()) {
        err << "h264_vaapi is not available on this machine\n";
        return 1;
    }

    const MediaInfo media = MediaProbe::probe(path);
    if (!media.ok || media.durationUs <= 0) {
        err << "could not probe " << path << ": " << media.errorString << "\n";
        return 1;
    }

    int srcW = 0;
    int srcH = 0;
    double fps = 30.0;
    for (const StreamInfo &s : media.streams) {
        if (s.type == StreamInfo::Type::Video && !s.attachedPicture) {
            srcW = s.width;
            srcH = s.height;
            if (s.fps > 1.0)
                fps = s.fps;
            break;
        }
    }
    if (srcW <= 0 || srcH <= 0) {
        err << "no video stream in " << path << "\n";
        return 1;
    }

    // Portrait 1080p keeps this 9:16 clip filling the frame (2160×3840 → 1080×1920).
    int outW = 1080;
    int outH = 1920;
    if (srcW > srcH) {
        outW = 1920;
        outH = 1080;
    }

    drift::Project project;
    project.setResolution(outW, outH);
    project.setFps(qMax(1, int(fps + 0.5)));
    project.tracks().clear();

    drift::Track video;
    video.type = drift::TrackType::Video;
    drift::Clip clip;
    clip.id = QStringLiteral("src");
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = media.durationUs;
    clip.srcIn = 0;
    clip.srcOut = media.durationUs;
    clip.effects.append(makeEffect(QStringLiteral("adjust.contrast")));
    clip.effects.append(makeEffect(QStringLiteral("stylize.vignette")));
    clip.effects.append(makeEffect(QStringLiteral("rgb_split")));
    video.clips << clip;
    project.tracks() << video;

    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 0;
    settings.videoCodecId = QStringLiteral("h264_vaapi");
    settings.audioCodecId = QStringLiteral("aac");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = 23;

    out << "source  " << srcW << "×" << srcH << "  " << (media.durationUs / 1e6) << " s  " << fps
        << " fps\n";
    out << "output  " << outW << "×" << outH << "  h264_vaapi  contrast+vignette+rgb_split\n";
    out << "gpu     " << (GpuCompositor::isAvailable() ? "yes" : "no") << "\n\n";
    out.flush();

    FrameCompositor compositor;
    compositor.setProject(&project);
    for (int i = 0; i < 3; ++i)
        compositor.compositeAt(drift::TimeUs(i) * 200'000);
    ClipReaderPool::instance().releaseAll();

    const QString gpuOut = outDir + QStringLiteral("/benchexport-gpu-nv12.mp4");
    const QString cpuOut = outDir + QStringLiteral("/benchexport-cpu-sws.mp4");

    qunsetenv("DRIFT_EXPORT_SWSCALE");
    if (!runTimedExport(project, settings, gpuOut, QStringLiteral("GPU NV12 pipeline"), out, err))
        return 1;

    ClipReaderPool::instance().releaseAll();

    qputenv("DRIFT_EXPORT_SWSCALE", "1");
    if (!runTimedExport(project, settings, cpuOut, QStringLiteral("CPU RGBA + sws_scale"), out, err))
        return 1;

    return 0;
}
