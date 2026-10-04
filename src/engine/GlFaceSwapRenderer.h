#pragma once

#include "FaceLandmarker.h"
#include "FaceMesh.h"
#include "GlRuntime.h"

#include <QMap>
#include <QOpenGLExtraFunctions>
#include <QString>
#include <QVariant>
#include <QVector>

namespace drift {

// User-facing knobs for the Face Swap effect, resolved from the effect's parameter map before
// the draw. Same role FaceModelParams plays for model3d.
struct FaceSwapParams
{
    QString meshPath;    // the package's mediapipe_face.bin; only its index buffer is read
    QString sourceImage; // the photo to take the face from; empty means "not picked yet"
    double opacity = 1.0;
    double feather = 0.35;    // fraction of the oval-to-centre span the edge fades over
    double colorMatch = 0.8;  // how much of the frame's lighting to transfer onto the photo
    double keepEyes = 0.6;    // how much of the tracked face's own eyes to let through
    double keepMouth = 0.4;   // likewise for the mouth
    int faceIndex = 0;
};

FaceSwapParams faceSwapParamsFromMap(const QMap<QString, QVariant> &parameters,
                                     const QString &packageDir);

// Per-vertex coverage for the swap, one float per rest vertex, in [0, 1].
//
// Pure CPU and free of GL so it can be tested directly, which is where the real risk in this
// effect lives: it is graph work over the mesh, not something a rendered pixel would localize.
//
// Three multi-source breadth-first searches over the mesh's own edges give each vertex its ring
// distance from the face oval, from either eye ring, and from the inner lip ring. Coverage then
// ramps up from the oval — so the swap fades out rather than ending on a hard silhouette — and
// back down around the eyes and mouth, which is what lets the subject's real blinks and speech
// show through a face taken from a single still.
//
// Ring distance rather than Euclidean distance because the mesh is the only thing that knows the
// mouth is a hole: two points either side of the lips are close in space and far apart across
// the surface, and a Euclidean falloff would smear the upper lip into the lower one.
QVector<float> faceSwapVertexAlpha(const FaceMeshRest &rest, double feather, double keepEyes,
                                   double keepMouth);

} // namespace drift

namespace drift::gl {

// Draw a Face Swap effect into a new pooled target composited over `source`.
// Returns an invalid target on any failure — callers treat that as grace mode (leave source).
//
// Like drawFaceModelEffect, this has to stay in step across all three chain call sites
// (GpuCompositor::buildLayerTarget, GpuEffectExecutor::applyChain, EffectProcessor's chain
// batching), or preview, export and the tools diverge.
GlTarget drawFaceSwapEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                            const FaceSwapParams &params, const QList<FaceAnchors> &faceSlots,
                            const GlTarget &source);

} // namespace drift::gl
