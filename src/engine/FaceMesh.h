#pragma once

#include "engine/FaceLandmarker.h"

#include <QString>
#include <QVector>
#include <QVector3D>

#include <cstdint>
#include <memory>

namespace drift {

// Same 468 as FaceLandmarker.h (included above). Declared here so the warp API is
// self-describing; the landmarker header is the ODR definition.
static_assert(kFaceMeshPoints == 468);

// Rest-pose Surrey Face Model (full public reference) plus the MediaPipe handles
// used to warp it. Loaded from
// effects/face_mesh_3d/sfm_face.bin; GlModelRenderer (and tests) consume these CPU buffers
// without knowing the on-disk layout.

struct FaceMeshHandle
{
    int mediapipeIndex = 0; // 0..467
    int restVertex = 0;
};

struct FaceMeshRest
{
    QVector<QVector3D> positions;
    QVector<uint32_t> indices;
    QVector<FaceMeshHandle> handles;
};

// Keyed on (absolutePath, mtimeMs, fileSize). Caches null results too so a broken path is not
// re-parsed every frame. Thread-safe.
std::shared_ptr<const FaceMeshRest> loadFaceMeshRest(const QString &path);

// Warning recorded for the most recent failed (or empty) load of `path`. Empty when the last
// successful load of that path had no notes. Same role as modelAssetWarning.
QString faceMeshRestWarning(const QString &path);

// Width-normalized world (same units as poseOx/Oy/Oz) → head space.
// Head space: origin at pose origin, axes = pose basis (right, up, forward), unit = 2*faceRx
// (same scale as FaceModelTransform::headBasisAt which scales by 2*faceRx).
// poseScale is interocular; headBasisAt uses s = 2*faceRx. Convert using faceRx when > 0, else
// poseScale (via the same 2*(poseScale/2) fallback headBasisAt uses).
QVector3D worldToHead(const FaceAnchors &face, const QVector3D &wn);

// Warp rest → tracked face. Requires face.hasMesh && face.mesh.size()==kFaceMeshPoints &&
// face.hasPose; otherwise copies the rest pose (still filling normals).
// outPositions/outNormals sized to rest.positions. Normals from area-weighted triangle
// averages, normalized.
// 1. Origin-fixed uniform scale s = Σ rest·target / Σ rest·rest over handles (clamped),
//    so a taller SFM rest does not leave the hairline and chin at rest size while IDW
//    pins the dense mid-face handles.
// 2. Inverse-distance on residuals: displacement = Σ w_j * (target_j − s·rest_j) / Σ w_j
//    target_j = worldToHead(face, mesh[mediapipeIndex])
//    w_j = 1 / (d^2 + 1e-6) where d is scaled-rest distance to handle j
//    Handles coincident with the vertex (d~0) take that handle's displacement only.
//    Inner-lip handles are partitioned by rest-mesh topology: SFM rest is closed-mouth
//    (upper/lower inner verts coincide), so mixing both lips would zip the opening.
void warpFaceMesh(const FaceMeshRest &rest, const FaceAnchors &face,
                  QVector<QVector3D> *outPositions, QVector<QVector3D> *outNormals);

} // namespace drift
