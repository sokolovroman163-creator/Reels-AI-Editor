#pragma once

#include <QImage>
#include <QString>

namespace drift {

// Source photos are capped before anything looks at them. The landmarker letterboxes to 640 for
// detection regardless, the swapped face rarely covers more than a third of an exported frame,
// and the texture stays resident for the session — so a 12-megapixel phone photo would cost
// 48 MB of VRAM to gain nothing.
inline constexpr int kFaceSwapPhotoMaxEdge = 1024;

// The one way a face-swap source photo is turned into pixels.
//
// Both the ingest below and the renderer's texture upload go through here, and they have to:
// the landmarks are stored as coordinates into *this* image, so a photo that the two paths
// decoded differently — EXIF rotation applied on one side only, say — would land the face
// sideways with no error anywhere. Null on any failure.
QImage loadFaceSwapPhoto(const QString &path);

// Landmark a source photo and cache the result as a one-frame FaceTrack at
// faceSwapSourcePath(photoPath).
//
// Blocking, and it constructs ONNX sessions on first use — never call this from the GUI or the
// GL thread. False when the photo cannot be read, the face models are missing, or no face was
// found; `errorOut` carries a message fit to show the user.
//
// Multi-face photos keep every face the detector found, largest first, and the renderer uses the
// first. There is deliberately no "which face in the photo" control: cropping the photo is a
// clearer way to say it, and one more index parameter next to faceIndex invites confusing the two.
bool ingestFaceSwapSource(const QString &photoPath, QString *errorOut);

// True when this photo already has usable landmarks cached, so the app layer can skip a re-ingest.
// Touches no models and constructs no session.
bool faceSwapSourceReady(const QString &photoPath);

} // namespace drift
