#include "FaceMesh.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QtEndian>
#include <QtGlobal>

#include <cstring>

namespace drift {
namespace {

constexpr quint32 kVersion = 1;
constexpr int kHeaderBytes = 20; // "DRFM" + 4×uint32
constexpr qint64 kMaxFileBytes = 32LL * 1024 * 1024;
constexpr int kMaxVertices = 100'000;
constexpr int kMaxIndices = 300'000;
constexpr int kMaxHandles = kFaceMeshPoints;

struct CacheKey
{
    QString path;
    qint64 mtimeMs = 0;
    qint64 size = 0;

    bool operator==(const CacheKey &o) const
    {
        return path == o.path && mtimeMs == o.mtimeMs && size == o.size;
    }
};

struct CacheEntry
{
    CacheKey key;
    std::shared_ptr<const FaceMeshRest> rest; // null = remembered failure
    QString warning;
};

QMutex g_cacheMutex;
QHash<QString, CacheEntry> g_cache; // path → last load

quint32 readU32(const char *p)
{
    quint32 v;
    std::memcpy(&v, p, 4);
    return qFromLittleEndian(v);
}

quint16 readU16(const char *p)
{
    quint16 v;
    std::memcpy(&v, p, 2);
    return qFromLittleEndian(v);
}

float readF32(const char *p)
{
    quint32 bits = readU32(p);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

void poseBasis(const FaceAnchors &face, double right[3], double up[3], double fwd[3])
{
    if (face.hasPose) {
        const double x = face.poseQx, y = face.poseQy, z = face.poseQz, w = face.poseQw;
        right[0] = 1 - 2 * (y * y + z * z);
        right[1] = 2 * (x * y + z * w);
        right[2] = 2 * (x * z - y * w);
        up[0] = 2 * (x * y - z * w);
        up[1] = 1 - 2 * (x * x + z * z);
        up[2] = 2 * (y * z + x * w);
        fwd[0] = 2 * (x * z + y * w);
        fwd[1] = 2 * (y * z - x * w);
        fwd[2] = 1 - 2 * (x * x + y * y);
        return;
    }
    // Identity in image space: right = +x, up = −y (toward forehead / smaller uv.y),
    // forward = +z (toward camera in the Phase-1 mesh convention). Matches headBasisAt.
    right[0] = 1;
    right[1] = 0;
    right[2] = 0;
    up[0] = 0;
    up[1] = -1;
    up[2] = 0;
    fwd[0] = 0;
    fwd[1] = 0;
    fwd[2] = 1;
}

double headScale(const FaceAnchors &face)
{
    // Same s as FaceModelTransform::headBasisAt: 2·faceRx, with poseScale as interocular
    // stand-in (2 * poseScale/2) and a last-resort 0.15 so a missing pose does not divide by 0.
    const double rx = face.faceRx > 1e-6
                          ? face.faceRx
                          : (face.poseScale > 1e-6 ? face.poseScale * 0.5 : 0.15);
    return 2.0 * rx;
}

void accumulateNormals(const QVector<QVector3D> &positions, const QVector<uint32_t> &indices,
                       QVector<QVector3D> *outNormals)
{
    const int n = positions.size();
    outNormals->fill(QVector3D(), n);
    const int triCount = indices.size() / 3;
    for (int t = 0; t < triCount; ++t) {
        const uint32_t i0 = indices[t * 3 + 0];
        const uint32_t i1 = indices[t * 3 + 1];
        const uint32_t i2 = indices[t * 3 + 2];
        if (i0 >= uint32_t(n) || i1 >= uint32_t(n) || i2 >= uint32_t(n))
            continue;
        const QVector3D e1 = positions[int(i1)] - positions[int(i0)];
        const QVector3D e2 = positions[int(i2)] - positions[int(i0)];
        const QVector3D nrm = QVector3D::crossProduct(e1, e2); // length = 2·area
        (*outNormals)[int(i0)] += nrm;
        (*outNormals)[int(i1)] += nrm;
        (*outNormals)[int(i2)] += nrm;
    }
    for (int i = 0; i < n; ++i) {
        if ((*outNormals)[i].isNull())
            (*outNormals)[i] = QVector3D(0.f, 0.f, 1.f);
        else
            (*outNormals)[i].normalize();
    }
}

// Inner-lip MediaPipe indices (kLipInner). SFM rest has the mouth closed: upper and lower
// inner verts sit ~0.001 apart, so unfiltered IDW averages both lips and the opening stays
// zipped. Topology from rest triangles + these seeds keeps each lip on its own handles.
bool isUpperInnerMp(int mp)
{
    switch (mp) {
    case 78:
    case 191:
    case 80:
    case 81:
    case 82:
    case 13:
    case 312:
    case 311:
    case 310:
    case 415:
        return true;
    default:
        return false;
    }
}

bool isLowerInnerMp(int mp)
{
    switch (mp) {
    case 308:
    case 324:
    case 318:
    case 402:
    case 317:
    case 14:
    case 87:
    case 178:
    case 88:
    case 95:
        return true;
    default:
        return false;
    }
}

bool isUpperOuterMp(int mp)
{
    switch (mp) {
    case 61:
    case 185:
    case 40:
    case 39:
    case 37:
    case 0:
    case 267:
    case 269:
    case 270:
    case 409:
    case 291:
        return true;
    default:
        return false;
    }
}

bool isLowerOuterMp(int mp)
{
    switch (mp) {
    case 375:
    case 321:
    case 405:
    case 314:
    case 17:
    case 84:
    case 181:
    case 91:
    case 146:
        return true;
    default:
        return false;
    }
}

void kRing(const QVector<QVector<int>> &adj, const QVector<int> &seeds, const QVector<char> &blocked,
           int rings, QVector<char> *out)
{
    QVector<int> frontier = seeds;
    for (int s : seeds) {
        if (s >= 0 && s < out->size())
            (*out)[s] = 1;
    }
    for (int r = 0; r < rings; ++r) {
        QVector<int> next;
        for (int i : frontier) {
            if (i < 0 || i >= adj.size())
                continue;
            for (int j : adj[i]) {
                if (j < 0 || j >= out->size() || (*out)[j] || blocked[j])
                    continue;
                (*out)[j] = 1;
                next.append(j);
            }
        }
        frontier.swap(next);
    }
}

// 1 = upper lip only, 2 = lower lip only, 0 = neither or both (mouth corners).
QVector<char> lipTags(const FaceMeshRest &rest)
{
    const int n = rest.positions.size();
    QVector<char> tags(n, 0);
    if (n == 0 || rest.indices.size() < 3)
        return tags;

    QVector<QVector<int>> adj(n);
    const int triCount = rest.indices.size() / 3;
    for (int t = 0; t < triCount; ++t) {
        const int i0 = int(rest.indices[t * 3 + 0]);
        const int i1 = int(rest.indices[t * 3 + 1]);
        const int i2 = int(rest.indices[t * 3 + 2]);
        if (i0 < 0 || i0 >= n || i1 < 0 || i1 >= n || i2 < 0 || i2 >= n)
            continue;
        adj[i0].append(i1);
        adj[i0].append(i2);
        adj[i1].append(i0);
        adj[i1].append(i2);
        adj[i2].append(i0);
        adj[i2].append(i1);
    }

    QVector<int> upperInner;
    QVector<int> lowerInner;
    QVector<int> upperSeeds;
    QVector<int> lowerSeeds;
    for (const FaceMeshHandle &h : rest.handles) {
        if (h.restVertex < 0 || h.restVertex >= n)
            continue;
        if (isUpperInnerMp(h.mediapipeIndex)) {
            upperInner.append(h.restVertex);
            upperSeeds.append(h.restVertex);
        } else if (isLowerInnerMp(h.mediapipeIndex)) {
            lowerInner.append(h.restVertex);
            lowerSeeds.append(h.restVertex);
        } else if (isUpperOuterMp(h.mediapipeIndex)) {
            upperSeeds.append(h.restVertex);
        } else if (isLowerOuterMp(h.mediapipeIndex)) {
            lowerSeeds.append(h.restVertex);
        }
    }

    QVector<char> blockLower(n, 0);
    QVector<char> blockUpper(n, 0);
    for (int v : lowerInner) {
        if (v >= 0 && v < n)
            blockLower[v] = 1;
    }
    for (int v : upperInner) {
        if (v >= 0 && v < n)
            blockUpper[v] = 1;
    }

    QVector<char> upper(n, 0);
    QVector<char> lower(n, 0);
    kRing(adj, upperSeeds, blockLower, 2, &upper);
    kRing(adj, lowerSeeds, blockUpper, 2, &lower);
    for (int i = 0; i < n; ++i) {
        if (upper[i] && !lower[i])
            tags[i] = 1;
        else if (lower[i] && !upper[i])
            tags[i] = 2;
    }
    return tags;
}

std::shared_ptr<const FaceMeshRest> parseFaceMesh(const QByteArray &bytes, QString *warningOut)
{
    if (bytes.size() < kHeaderBytes) {
        *warningOut = QStringLiteral("face mesh file too small");
        return nullptr;
    }
    if (std::memcmp(bytes.constData(), "DRFM", 4) != 0) {
        *warningOut = QStringLiteral("bad face mesh magic");
        return nullptr;
    }
    const quint32 version = readU32(bytes.constData() + 4);
    if (version != kVersion) {
        *warningOut = QStringLiteral("unsupported face mesh version");
        return nullptr;
    }
    const quint32 vertCount = readU32(bytes.constData() + 8);
    const quint32 indexCount = readU32(bytes.constData() + 12);
    const quint32 handleCount = readU32(bytes.constData() + 16);
    if (vertCount == 0 || vertCount > quint32(kMaxVertices) || indexCount > quint32(kMaxIndices)
        || handleCount > quint32(kMaxHandles) || (indexCount % 3u) != 0) {
        *warningOut = QStringLiteral("face mesh counts out of range");
        return nullptr;
    }

    const qint64 vertBytes = qint64(vertCount) * 12;
    const qint64 indexBytes = qint64(indexCount) * 4;
    const qint64 handleBytes = qint64(handleCount) * 4;
    const qint64 need = qint64(kHeaderBytes) + vertBytes + indexBytes + handleBytes;
    if (qint64(bytes.size()) != need) {
        *warningOut = QStringLiteral("face mesh size mismatch");
        return nullptr;
    }

    auto rest = std::make_shared<FaceMeshRest>();
    rest->positions.resize(int(vertCount));
    const char *p = bytes.constData() + kHeaderBytes;
    for (quint32 i = 0; i < vertCount; ++i) {
        rest->positions[int(i)] = QVector3D(readF32(p), readF32(p + 4), readF32(p + 8));
        p += 12;
    }
    rest->indices.resize(int(indexCount));
    for (quint32 i = 0; i < indexCount; ++i) {
        const quint32 idx = readU32(p);
        if (idx >= vertCount) {
            *warningOut = QStringLiteral("face mesh index out of range");
            return nullptr;
        }
        rest->indices[int(i)] = idx;
        p += 4;
    }
    rest->handles.resize(int(handleCount));
    for (quint32 i = 0; i < handleCount; ++i) {
        const quint16 mp = readU16(p);
        const quint16 vi = readU16(p + 2);
        if (int(mp) >= kFaceMeshPoints || quint32(vi) >= vertCount) {
            *warningOut = QStringLiteral("face mesh handle out of range");
            return nullptr;
        }
        rest->handles[int(i)] = FaceMeshHandle{int(mp), int(vi)};
        p += 4;
    }
    return rest;
}

} // namespace

std::shared_ptr<const FaceMeshRest> loadFaceMeshRest(const QString &path)
{
    if (path.isEmpty()) {
        QMutexLocker lock(&g_cacheMutex);
        CacheEntry entry;
        entry.key.path = path;
        entry.warning = QStringLiteral("empty face mesh path");
        g_cache.insert(path, entry);
        return nullptr;
    }

    const QFileInfo info(QFileInfo(path).absoluteFilePath());
    CacheKey key{info.absoluteFilePath(), info.lastModified().toMSecsSinceEpoch(), info.size()};

    {
        QMutexLocker lock(&g_cacheMutex);
        const auto it = g_cache.constFind(key.path);
        if (it != g_cache.cend() && it.value().key == key)
            return it.value().rest;
    }

    QString warning;
    std::shared_ptr<const FaceMeshRest> rest;
    if (!info.exists() || !info.isFile()) {
        warning = QStringLiteral("face mesh file not found");
    } else if (info.size() > kMaxFileBytes) {
        warning = QStringLiteral("face mesh exceeds 32 MB limit");
    } else {
        QFile file(key.path);
        if (!file.open(QIODevice::ReadOnly)) {
            warning = QStringLiteral("could not open face mesh file");
        } else {
            const QByteArray bytes = file.readAll();
            file.close();
            if (bytes.isEmpty())
                warning = QStringLiteral("empty face mesh file");
            else
                rest = parseFaceMesh(bytes, &warning);
        }
    }

    {
        QMutexLocker lock(&g_cacheMutex);
        CacheEntry entry;
        entry.key = key;
        entry.rest = rest;
        entry.warning = warning;
        g_cache.insert(key.path, entry);
    }
    return rest;
}

QString faceMeshRestWarning(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString abs = QFileInfo(path).absoluteFilePath();
    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_cache.constFind(abs);
    if (it == g_cache.cend())
        return {};
    return it.value().warning;
}

QVector3D worldToHead(const FaceAnchors &face, const QVector3D &wn)
{
    double right[3], up[3], fwd[3];
    poseBasis(face, right, up, fwd);
    const double s = headScale(face);
    const double inv = s > 1e-12 ? (1.0 / s) : 0.0;

    const double ox = face.hasPose ? face.poseOx : face.faceCenter.x();
    const double oy = face.hasPose ? face.poseOy : face.faceCenter.y();
    const double oz = face.hasPose ? face.poseOz : 0.0;
    const double dx = double(wn.x()) - ox;
    const double dy = double(wn.y()) - oy;
    const double dz = double(wn.z()) - oz;

    return QVector3D(float((dx * right[0] + dy * right[1] + dz * right[2]) * inv),
                     float((dx * up[0] + dy * up[1] + dz * up[2]) * inv),
                     float((dx * fwd[0] + dy * fwd[1] + dz * fwd[2]) * inv));
}

void warpFaceMesh(const FaceMeshRest &rest, const FaceAnchors &face,
                  QVector<QVector3D> *outPositions, QVector<QVector3D> *outNormals)
{
    if (!outPositions || !outNormals)
        return;

    const int n = rest.positions.size();
    outPositions->resize(n);
    outNormals->resize(n);
    if (n == 0)
        return;

    const bool canWarp = face.hasMesh && face.mesh.size() == kFaceMeshPoints && face.hasPose
                         && !rest.handles.isEmpty();
    if (!canWarp) {
        *outPositions = rest.positions;
        accumulateNormals(rest.positions, rest.indices, outNormals);
        return;
    }

    QVector<QVector3D> handleRest;
    QVector<QVector3D> handleTarget;
    QVector<int> handleMp;
    handleRest.reserve(rest.handles.size());
    handleTarget.reserve(rest.handles.size());
    handleMp.reserve(rest.handles.size());
    for (const FaceMeshHandle &h : rest.handles) {
        if (h.restVertex < 0 || h.restVertex >= n)
            continue;
        if (h.mediapipeIndex < 0 || h.mediapipeIndex >= kFaceMeshPoints)
            continue;
        handleRest.append(rest.positions[h.restVertex]);
        handleTarget.append(worldToHead(face, face.mesh[h.mediapipeIndex]));
        handleMp.append(h.mediapipeIndex);
    }
    if (handleRest.isEmpty()) {
        *outPositions = rest.positions;
        accumulateNormals(rest.positions, rest.indices, outNormals);
        return;
    }

    // Origin-fixed uniform scale (both clouds live in head space, origin at the eyes).
    // SFM rest is taller/wider than a typical MediaPipe face. IDW alone pins the dense
    // mid-face handles (eyes, mouth) and leaves the hairline and chin at rest size.
    // Residuals after this scale are the expression warp.
    float restDotRest = 0.f;
    float restDotTgt = 0.f;
    for (int j = 0; j < handleRest.size(); ++j) {
        restDotRest += QVector3D::dotProduct(handleRest[j], handleRest[j]);
        restDotTgt += QVector3D::dotProduct(handleRest[j], handleTarget[j]);
    }
    float simScale = 1.f;
    if (restDotRest > 1e-8f)
        simScale = qBound(0.25f, restDotTgt / restDotRest, 4.0f);

    QVector<QVector3D> handleDisp(handleRest.size());
    for (int j = 0; j < handleRest.size(); ++j) {
        handleRest[j] *= simScale;
        handleDisp[j] = handleTarget[j] - handleRest[j];
    }

    const int nh = handleRest.size();
    const QVector<char> lip = lipTags(rest);
    for (int i = 0; i < n; ++i) {
        const QVector3D p = rest.positions[i] * simScale;
        QVector3D disp(0.f, 0.f, 0.f);
        float wsum = 0.f;
        bool coincident = false;
        const char tag = (i < lip.size()) ? lip[i] : 0;
        for (int j = 0; j < nh; ++j) {
            if (tag == 1 && isLowerInnerMp(handleMp[j]))
                continue;
            if (tag == 2 && isUpperInnerMp(handleMp[j]))
                continue;
            const QVector3D d = p - handleRest[j];
            const float d2 = d.lengthSquared();
            // A handle sitting on this vertex owns it; mixing in the others would smear a
            // known correspondence (the whole point of storing restVertex).
            if (d2 < 1e-12f) {
                disp = handleDisp[j];
                coincident = true;
                break;
            }
            const float w = 1.f / (d2 + 1e-6f);
            disp += handleDisp[j] * w;
            wsum += w;
        }
        if (!coincident && wsum > 0.f)
            disp /= wsum;
        (*outPositions)[i] = p + disp;
    }
    accumulateNormals(*outPositions, rest.indices, outNormals);
}

} // namespace drift
