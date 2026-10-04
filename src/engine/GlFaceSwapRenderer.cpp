#include "GlFaceSwapRenderer.h"

#include "FaceSwapSource.h"
#include "FaceTrack.h"
#include "GlModelRenderer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLShaderProgram>
#include <QVector2D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace drift {
namespace {

// How far the eye and mouth cut-outs fade back in, in mesh rings. Two, not three: the point is to
// expose the eye opening and the lips, and one ring further out already reaches the brow and the
// cheekbone, where the underlying face reads as a blotch rather than as an eye.
constexpr double kHoleRings = 2.0;

double smoothRamp(double t)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    return t * t * (3.0 - 2.0 * t);
}

std::vector<std::vector<int>> buildAdjacency(const FaceMeshRest &rest, int vertexCount)
{
    const size_t count = size_t(vertexCount);
    std::vector<std::vector<int>> adj(count);
    for (int i = 0; i + 2 < rest.indices.size(); i += 3) {
        const int t[3] = {int(rest.indices[i]), int(rest.indices[i + 1]), int(rest.indices[i + 2])};
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) {
                if (a == b)
                    continue;
                if (t[a] >= 0 && t[a] < vertexCount && t[b] >= 0 && t[b] < vertexCount)
                    adj[size_t(t[a])].push_back(t[b]);
            }
        }
    }
    return adj;
}

// Multi-source BFS. Unreachable vertices keep INT_MAX, which every ramp below reads as "far", so
// an island in a malformed mesh renders fully covered rather than fully transparent.
std::vector<int> ringDistance(const std::vector<std::vector<int>> &adj, const std::vector<int> &seeds)
{
    std::vector<int> dist(adj.size(), std::numeric_limits<int>::max());
    std::queue<int> pending;
    for (int s : seeds) {
        if (s < 0 || size_t(s) >= adj.size() || dist[size_t(s)] == 0)
            continue;
        dist[size_t(s)] = 0;
        pending.push(s);
    }
    while (!pending.empty()) {
        const int v = pending.front();
        pending.pop();
        for (int n : adj[size_t(v)]) {
            if (dist[size_t(n)] != std::numeric_limits<int>::max())
                continue;
            dist[size_t(n)] = dist[size_t(v)] + 1;
            pending.push(n);
        }
    }
    return dist;
}

double ringRamp(int distance, double rings)
{
    if (distance == std::numeric_limits<int>::max())
        return 1.0;
    if (rings <= 0.0)
        return distance > 0 ? 1.0 : 0.0;
    return smoothRamp(double(distance) / rings);
}

} // namespace

QVector<float> faceSwapVertexAlpha(const FaceMeshRest &rest, double feather, double keepEyes,
                                   double keepMouth)
{
    const int n = rest.positions.size();
    QVector<float> alpha(n, 1.f);
    if (n <= 0 || rest.indices.isEmpty())
        return alpha;

    const auto adj = buildAdjacency(rest, n);

    const std::vector<int> ovalSeeds(mpidx::kFaceOval.begin(), mpidx::kFaceOval.end());
    std::vector<int> eyeSeeds(mpidx::kEyeLeftRing.begin(), mpidx::kEyeLeftRing.end());
    eyeSeeds.insert(eyeSeeds.end(), mpidx::kEyeRightRing.begin(), mpidx::kEyeRightRing.end());
    const std::vector<int> mouthSeeds(mpidx::kLipInner.begin(), mpidx::kLipInner.end());

    const std::vector<int> dOval = ringDistance(adj, ovalSeeds);
    const std::vector<int> dEye = ringDistance(adj, eyeSeeds);
    const std::vector<int> dMouth = ringDistance(adj, mouthSeeds);

    // Feather is a fraction of how deep the mesh actually is rather than a ring count, so the
    // control means the same thing whatever topology it is handed.
    int deepest = 1;
    for (int d : dOval) {
        if (d != std::numeric_limits<int>::max())
            deepest = std::max(deepest, d);
    }
    const double featherRings = std::max(1.0, std::clamp(feather, 0.0, 1.0) * double(deepest));

    const double eyes = std::clamp(keepEyes, 0.0, 1.0);
    const double mouth = std::clamp(keepMouth, 0.0, 1.0);

    for (int i = 0; i < n; ++i) {
        double a = ringRamp(dOval[size_t(i)], featherRings);
        if (eyes > 0.0)
            a *= 1.0 - eyes * (1.0 - ringRamp(dEye[size_t(i)], kHoleRings));
        if (mouth > 0.0)
            a *= 1.0 - mouth * (1.0 - ringRamp(dMouth[size_t(i)], kHoleRings));
        alpha[i] = float(std::clamp(a, 0.0, 1.0));
    }
    return alpha;
}

FaceSwapParams faceSwapParamsFromMap(const QMap<QString, QVariant> &parameters,
                                     const QString &packageDir)
{
    const auto number = [&parameters](const char *key, double fallback) {
        const auto it = parameters.constFind(QString::fromLatin1(key));
        return it == parameters.constEnd() ? fallback : it.value().toDouble();
    };

    FaceSwapParams p;
    p.meshPath = QDir(packageDir).filePath(QStringLiteral("mediapipe_face.bin"));
    p.sourceImage = parameters.value(QStringLiteral("sourceImage")).toString();
    p.opacity = number("opacity", 1.0);
    p.feather = number("feather", 0.35);
    p.colorMatch = number("colorMatch", 0.8);
    p.keepEyes = number("keepEyes", 0.6);
    p.keepMouth = number("keepMouth", 0.4);
    p.faceIndex = int(std::lround(number("faceIndex", 0.0)));
    return p;
}

} // namespace drift

namespace drift::gl {
namespace {

constexpr const char *kSwapVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;    // width-normalized tracked mesh point
layout(location = 1) in vec2 a_uv;     // source-photo uv, top-left origin
layout(location = 2) in float a_alpha;
uniform float u_aspect;                // frame height / width
out vec2 v_photoUv;
out vec2 v_frameUv;
out float v_alpha;
void main() {
    vec2 frameUv = vec2(a_pos.x, a_pos.y / u_aspect);
    v_photoUv = a_uv;
    v_frameUv = frameUv;
    v_alpha = a_alpha;
    // wnToNdc with no head-space round trip: the tracked mesh is already in the space that
    // matrix consumes, and the pose basis it would go through cancels itself out.
    //
    // MediaPipe z is smaller when nearer, so +0.25*z is what wins GL_LESS on the near cheek.
    // This is model3d's -0.25 composed with its u_flipDepth = -1, spelled out directly.
    gl_Position = vec4(frameUv * 2.0 - 1.0, a_pos.z * 0.25, 1.0);
}
)";

constexpr const char *kSwapFrag = R"(#version 330 core
in vec2 v_photoUv;
in vec2 v_frameUv;
in float v_alpha;
out vec4 fragColor;
uniform sampler2D u_photo;
uniform sampler2D u_lowPhoto;
uniform sampler2D u_lowVideo;
uniform float u_opacity;
uniform float u_colorMatch;
void main() {
    float a = v_alpha * u_opacity;
    // Feathered rim fragments still write depth, and a near-transparent triangle in front would
    // otherwise punch a hole through the opaque far cheek at profile. Drop them instead.
    if (a < 0.004)
        discard;

    vec3 rgb = texture(u_photo, clamp(v_photoUv, 0.0, 1.0)).rgb;
    vec3 lowPhoto = texture(u_lowPhoto, clamp(v_photoUv, 0.0, 1.0)).rgb;
    vec3 lowVideo = texture(u_lowVideo, clamp(v_frameUv, 0.0, 1.0)).rgb;

    // Transfer the frame's lighting onto the photo as a low-frequency ratio. The epsilon stops a
    // near-black photo background from dividing by nothing, and the clamp keeps a bad match to a
    // stop either way rather than letting it blow out to white.
    vec3 ratio = clamp((lowVideo + 0.02) / (lowPhoto + 0.02), vec3(0.5), vec3(2.0));
    rgb = mix(rgb, clamp(rgb * ratio, 0.0, 1.0), u_colorMatch);

    fragColor = vec4(rgb * a, a); // premultiplied, for resolveFaceOverlay
}
)";

// 4x4 box over the destination texel's footprint. Used for every step of the low-frequency
// reduction; a wider-than-exact kernel is what we want here, since the result is a lighting
// field and any aliasing in it shimmers frame to frame.
constexpr const char *kLowFreqFrag = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_step;
void main() {
    vec4 c = vec4(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x)
            c += texture(u_currentTexture, v_texCoord + u_step * (vec2(float(x), float(y)) - 1.5));
    }
    fragColor = c * (1.0 / 16.0);
}
)";

// Width of the low-frequency field, chosen so roughly this many texels span the tracked face.
//
// Derived from faceRx — width-normalized, and therefore the same at preview's renderScale 0.5 as
// at export's 1.0. Sizing it from the frame's pixel width instead would make the lighting match
// visibly different between the two, which is the one thing this pipeline must never do.
constexpr double kLowFreqTexelsAcrossFace = 24.0;

int lowFreqWidth(double faceRx)
{
    const double span = std::max(2.0 * faceRx, 0.05);
    const double want = kLowFreqTexelsAcrossFace / span;
    // Power-of-two buckets so a drifting faceRx does not churn the FBO pool with a new size
    // every frame.
    int w = 32;
    while (w < 256 && double(w) < want)
        w *= 2;
    return w;
}

QOpenGLShaderProgram *lowFreqProgram(GlRuntime &rt)
{
    return rt.builtinProgram(QStringLiteral("face_swap_lowfreq"), kQuadVertexShader, kLowFreqFrag);
}

// One reduction step into a freshly acquired pooled target. Returns an invalid target on failure
// without touching `srcTex`, which the caller still owns.
GlTarget reduceStep(GlRuntime &rt, QOpenGLExtraFunctions *gl, GLuint srcTex, int dstW, int dstH)
{
    GlTarget dst = rt.acquireTarget(dstW, dstH);
    if (!dst.isValid())
        return {};
    QOpenGLShaderProgram *prog = lowFreqProgram(rt);
    if (!prog) {
        rt.releaseTarget(std::move(dst));
        return {};
    }

    dst.fbo->bind();
    gl->glViewport(0, 0, dstW, dstH);
    prog->bind();
    prog->setUniformValue("u_currentTexture", 0);
    prog->setUniformValue("u_step", QVector2D(1.f / (4.f * float(dstW)), 1.f / (4.f * float(dstH))));
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, srcTex);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    prog->release();
    dst.fbo->release();
    return dst;
}

// Reduce the frame to a face-relative low-frequency field, halving until the last step is a
// modest ratio so nothing aliases on the way down.
GlTarget buildLowFreqVideo(GlRuntime &rt, QOpenGLExtraFunctions *gl, const GlTarget &source,
                           double aspect, double faceRx)
{
    const int dstW = lowFreqWidth(faceRx);
    const int dstH = std::max(1, int(std::lround(double(dstW) * aspect)));

    GLuint tex = source.texture();
    int w = source.width;
    int h = source.height;
    GlTarget carry;

    while (w > dstW * 4 && h > 1) {
        const int nw = std::max(dstW, w / 2);
        const int nh = std::max(1, h / 2);
        GlTarget next = reduceStep(rt, gl, tex, nw, nh);
        if (!next.isValid()) {
            if (carry.isValid())
                rt.releaseTarget(std::move(carry));
            return {};
        }
        if (carry.isValid())
            rt.releaseTarget(std::move(carry));
        tex = next.texture();
        w = nw;
        h = nh;
        carry = std::move(next);
    }

    GlTarget result = reduceStep(rt, gl, tex, dstW, dstH);
    if (carry.isValid())
        rt.releaseTarget(std::move(carry));
    return result;
}

// Photo textures, memoized on the runtime so a scrub does not re-decode the image every frame.
// GL-thread only, which is what makes the unguarded map safe: GlRuntime::exec serializes all GL
// work onto one thread.
const GlRuntime::FaceSwapPhotoGpu *acquirePhoto(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                                                const QString &photoPath, double faceRx)
{
    const QFileInfo info(photoPath);
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(info.absoluteFilePath())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(info.size());

    const auto it = rt.faceSwapPhotos.find(key);
    if (it != rt.faceSwapPhotos.end())
        return it->second.texture ? &it->second : nullptr;

    GlRuntime::FaceSwapPhotoGpu entry;
    const QImage photo = loadFaceSwapPhoto(photoPath);
    if (photo.isNull() || photo.width() <= 0 || photo.height() <= 0) {
        // Cached as a failure so a broken path is not re-decoded once per frame.
        rt.faceSwapPhotos[key] = entry;
        return nullptr;
    }

    entry.aspect = float(double(photo.height()) / double(photo.width()));
    entry.texture = uploadTexture(gl, photo, /*flipVertically=*/false);

    // The photo's half of the lighting match, at the same face-relative scale the frame's half
    // uses — otherwise the ratio measures the difference in blur radius, not in lighting.
    const int lowW = std::max(1, lowFreqWidth(faceRx));
    const int lowH = std::max(1, int(std::lround(double(lowW) * double(entry.aspect))));
    entry.lowFreq = uploadTexture(
        gl, photo.scaled(lowW, lowH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
        /*flipVertically=*/false);

    rt.faceSwapPhotos[key] = entry;
    return &rt.faceSwapPhotos[key];
}

bool uploadSwapMesh(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &meshPath,
                    const FaceMeshRest &rest, const QList<QVector3D> &mesh,
                    const QList<QVector3D> &sourceMesh, float sourceAspect,
                    const QVector<float> &alpha)
{
    const int vertexCount = kFaceMeshPoints;
    if (mesh.size() != vertexCount || sourceMesh.size() != vertexCount
        || alpha.size() < vertexCount || rest.indices.isEmpty())
        return false;

    constexpr int kFloatsPerVertex = 6; // pos3 + uv2 + alpha1; unlit, so no normals
    QVector<float> verts(vertexCount * kFloatsPerVertex);
    for (int i = 0; i < vertexCount; ++i) {
        const QVector3D &p = mesh.at(i);
        const QVector3D &s = sourceMesh.at(i);
        float *v = verts.data() + i * kFloatsPerVertex;
        v[0] = p.x();
        v[1] = p.y();
        v[2] = p.z();
        // Width-normalized back to plain uv: the mesh stores y already scaled by its own aspect.
        v[3] = s.x();
        v[4] = sourceAspect > 1e-6f ? s.y() / sourceAspect : s.y();
        v[5] = alpha.at(i);
    }

    auto &gpu = rt.faceSwapMesh;
    const int stride = kFloatsPerVertex * int(sizeof(float));
    if (!gpu.vao) {
        gl->glGenVertexArrays(1, &gpu.vao);
        gl->glGenBuffers(1, &gpu.vbo);
        gl->glGenBuffers(1, &gpu.ibo);
        gl->glBindVertexArray(gpu.vao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(3 * sizeof(float)));
        gl->glEnableVertexAttribArray(2);
        gl->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(5 * sizeof(float)));
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        gl->glBindVertexArray(0);
    }

    gl->glBindVertexArray(gpu.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    const GLsizeiptr vboBytes = GLsizeiptr(verts.size()) * GLsizeiptr(sizeof(float));
    if (gpu.vertexCount != vertexCount) {
        gl->glBufferData(GL_ARRAY_BUFFER, vboBytes, verts.constData(), GL_STREAM_DRAW);
        gpu.vertexCount = vertexCount;
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, vboBytes, verts.constData());
    }

    const int indexCount = rest.indices.size();
    if (gpu.path != meshPath || gpu.indexCount != indexCount) {
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         GLsizeiptr(indexCount) * GLsizeiptr(sizeof(uint32_t)),
                         rest.indices.constData(), GL_STATIC_DRAW);
        gpu.path = meshPath;
        gpu.indexCount = indexCount;
    }
    gl->glBindVertexArray(0);
    return true;
}

// The alpha ramp is graph work over the whole mesh and depends only on the topology and three
// sliders, so it is held between frames. GL-thread only, like everything else in this file.
const QVector<float> &cachedVertexAlpha(const QString &meshPath, const FaceMeshRest &rest,
                                        double feather, double keepEyes, double keepMouth)
{
    static QString cachedPath;
    static double cachedFeather = -1.0;
    static double cachedEyes = -1.0;
    static double cachedMouth = -1.0;
    static QVector<float> cached;

    if (cachedPath != meshPath || cachedFeather != feather || cachedEyes != keepEyes
        || cachedMouth != keepMouth || cached.size() != rest.positions.size()) {
        cached = faceSwapVertexAlpha(rest, feather, keepEyes, keepMouth);
        cachedPath = meshPath;
        cachedFeather = feather;
        cachedEyes = keepEyes;
        cachedMouth = keepMouth;
    }
    return cached;
}

} // namespace

GlTarget drawFaceSwapEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                            const FaceSwapParams &params, const QList<FaceAnchors> &faceSlots,
                            const GlTarget &source)
{
    if (!source.isValid() || !gl)
        return {};

    if (params.faceIndex < 0 || params.faceIndex >= faceSlots.size())
        return {};
    const FaceAnchors &face = faceSlots.at(params.faceIndex);
    if (!face.valid || !face.hasMesh || face.mesh.size() != kFaceMeshPoints)
        return {};

    // Freshly added effect with no photo chosen yet: pass through silently, the way an empty
    // model path does on the face-prop path.
    if (params.sourceImage.isEmpty())
        return {};

    // Landmarks for the photo. Absent until the app layer's ingest lands, which is the normal
    // state for the first moment after picking a photo and after opening a project.
    const auto sourceTrack = loadFaceTrackCached(faceSwapSourcePath(params.sourceImage));
    if (!sourceTrack || sourceTrack->frames.isEmpty())
        return {};
    const QList<FaceAnchors> &sourceFaces = sourceTrack->frames.first().faces;
    if (sourceFaces.isEmpty())
        return {};
    const FaceAnchors &sourceFace = sourceFaces.first();
    if (!sourceFace.valid || !sourceFace.hasMesh || sourceFace.mesh.size() != kFaceMeshPoints)
        return {};

    const auto rest = loadFaceMeshRest(params.meshPath);
    // The index buffer addresses the 468 tracked points directly, so a bin with any other vertex
    // count would index past the end of the stream buffer.
    if (!rest || rest->indices.isEmpty() || rest->positions.size() != kFaceMeshPoints)
        return {};

    const GlRuntime::FaceSwapPhotoGpu *photo =
        acquirePhoto(rt, gl, params.sourceImage, sourceFace.faceRx);
    if (!photo || !photo->texture || !photo->lowFreq)
        return {};

    const QVector<float> &alpha =
        cachedVertexAlpha(params.meshPath, *rest, params.feather, params.keepEyes, params.keepMouth);
    if (!uploadSwapMesh(rt, gl, params.meshPath, *rest, face.mesh, sourceFace.mesh, photo->aspect,
                        alpha))
        return {};

    const int srcW = source.width;
    const int srcH = source.height;
    const double aspect = double(srcH) / double(srcW);

    GlTarget lowVideo = buildLowFreqVideo(rt, gl, source, aspect, face.faceRx);
    if (!lowVideo.isValid())
        return {};

    const bool supersample = (qint64(srcW) * srcH) <= (1920LL * 1080LL);
    const int drawW = supersample ? srcW * 2 : srcW;
    const int drawH = supersample ? srcH * 2 : srcH;

    GlTarget overlay = rt.acquireTarget(drawW, drawH, /*wantDepth=*/true);
    if (!overlay.isValid()) {
        rt.releaseTarget(std::move(lowVideo));
        return {};
    }

    QOpenGLShaderProgram *prog =
        rt.builtinProgram(QStringLiteral("face_swap"), kSwapVert, kSwapFrag);
    if (!prog) {
        rt.releaseTarget(std::move(lowVideo));
        rt.releaseTarget(std::move(overlay));
        return {};
    }

    GlStateGuard guard(gl);

    overlay.fbo->bind();
    gl->glViewport(0, 0, drawW, drawH);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LESS);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    // Screen winding flips with head yaw, so culling would drop the whole face on one side of
    // frontal. Same reason the warped face-mesh path disables it.
    gl->glDisable(GL_CULL_FACE);

    prog->bind();
    prog->setUniformValue("u_aspect", float(aspect));
    prog->setUniformValue("u_opacity", float(std::clamp(params.opacity, 0.0, 1.0)));
    prog->setUniformValue("u_colorMatch", float(std::clamp(params.colorMatch, 0.0, 1.0)));
    prog->setUniformValue("u_photo", 0);
    prog->setUniformValue("u_lowPhoto", 1);
    prog->setUniformValue("u_lowVideo", 2);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, photo->texture);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, photo->lowFreq);
    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_2D, lowVideo.texture());
    gl->glActiveTexture(GL_TEXTURE0);

    gl->glBindVertexArray(rt.faceSwapMesh.vao);
    gl->glDrawElements(GL_TRIANGLES, rt.faceSwapMesh.indexCount, GL_UNSIGNED_INT, nullptr);
    gl->glBindVertexArray(0);
    prog->release();

    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_DEPTH_TEST);
    overlay.fbo->release();

    rt.releaseTarget(std::move(lowVideo));
    return resolveFaceOverlay(rt, gl, std::move(overlay), source);
}

} // namespace drift::gl
