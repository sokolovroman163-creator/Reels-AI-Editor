# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.6.0`

---

## ✅ Fixed

- Pasting clips whose kind needed a new track left the wrong clips selected.
- Dropping an effect on empty track space put its adjustment layer at the top of the timeline, so it graded every track instead of the ones under the drop. It now goes directly above that track.
- Time Echo did nothing when added the ordinary way, as an adjustment on the clip's lane.
- Face and depth effects were accepted on standalone adjustment layers, where there is no face or depth to read, and silently did nothing. They are now refused there with a pointer to the clip.
- The blurred background fill ignored the bottom clip's effects, so it showed the ungraded footage around a graded picture.
- An audio effect pinned to one clip also played on its neighbour during a crossfade.
- Shifting an animated position (canvas crop) turned a muted animation back on.
- Composite clips had no box on the preview, so they could only be moved and scaled from the inspector. Their corners keep the aspect like footage.
- Changing the video size or cropping the canvas only rebased the timeline that was open: clips inside composites kept their old positions, and composites were stretched onto the new size. Every timeline is rebased now, and a composite's box scales with the canvas so what it shows holds still.
- **Behaviour change:** MCP `set_transform` added a second keyframe at the playhead whenever a property had one key — which every new clip does — so a plain move became an animation. It now moves the single key; only a property that is already animated, or auto-key, gets a key at the playhead, as the tool always described.
- Standalone adjustment layers darkened and thickened soft edges over a transparent background (including inside composites): the canvas was fed to the effects premultiplied and then drawn over itself.
- **Behaviour change:** A mask or effect pinned to one clip also applied to its neighbour on the same track wherever the two were on screen together, as during a transition. A pinned adjustment now applies to its own clip only.
- The "Audio adjustment" track option added an empty video adjustment track. It now adds an audio adjustment track holding an audio-effects clip at the playhead.
- **Behaviour change:** Imported video, images and vector graphics are centred on the canvas instead of sitting in the top-left corner, and Reset transform puts them back to that centred fit rather than stretching them over the whole canvas.

- Importing on Android wrote the copy under whatever name the document provider reported, so a name carrying a character the storage volume will not accept — or one longer than an encrypted volume such as a Samsung Secure Folder container can hold — failed with nothing but "could not open that file". Names are now made storable first: separators, control characters and the reserved set are replaced, trailing dots and spaces dropped, and an over-long name is cut with its extension kept. Two documents that want the same copy are numbered apart rather than one silently standing in for the other.
- An export started by an agent overwrote the settings the export dialog remembers, and an omitted audio-only or GIF switch carried over from the previous render — which could turn a video export into an audio file with nothing to say so.
- Adding a transition could bind it to a clip that overlapped almost all of the outgoing one instead of the clip at the cut, and a transition loaded from an older project could claim a span far longer than the clips it joins.
- Auto-reframe stretched the picture and zoomed far past what was asked for — a 9:16 crop of a 4K clip came out around 3x magnified and distorted. It now keeps the source's shape, fits the requested aspect inside the canvas, and reports how much it magnified the source so an upscale is visible before it reaches a render.
- Adding two graphics at the same moment — an emoji and a Lottie, say — pushed the second one down the timeline instead of stacking it on its own lane, so it appeared at the wrong time.
- Keyframes written while the playhead sat outside a clip landed outside it too, where they could never play but still bent the animation.
- Setting an effect, audio-effect or transition parameter that does not exist now fails instead of quietly storing a value nothing reads. The colour-parameter schema no longer advertises alpha it cannot carry.
- Beat detection reported half the tempo on music with a strong downbeat — 60 BPM on a 120 BPM track — and said it was certain. It now checks whether the faster tempo explains the music just as well, and reports lower confidence when the two are genuinely close.
- Ducking music under speech flattened any volume envelope the music already had, and running it again stacked new keyframes on top of the old ones so the music pumped between words. Each dip now returns to the level the music was actually at, and re-running replaces the previous pass.
- Setting a value at a keyframe's own time could add a second keyframe a fraction of a millisecond away instead of updating the one already there.
- The Limiter made audio louder instead of limiting it, and lowering its ceiling added more gain rather than less. It is now a real ceiling limiter: the output never exceeds the ceiling, and a signal already below it is left alone.
- The true-peak reading was always 0.0 dBFS: it was taken after the master soft clipper and used an interpolation that could not see between samples. It is now a real 4x oversampled measurement of the unclipped mix.
- Loudness was measured 3 dB below the standard, so Normalise applied 3 dB too much gain and pushed audio into clipping. Normalising now also builds on the clip's existing volume instead of replacing it, and warns when the target would clip.
- Splitting an animated clip replayed the animation from the start on the second half instead of continuing it, so a cut visibly changed the motion.
- Lottie and SVG clips seeked one frame past the end of the animation, so a clip set to hold its last frame could show the empty frame after it instead.
- Splitting a clip left the second half with no effects: the grade, mask or colour work stayed on the first half only.
- The Duotone effect rendered a black frame instead of tinting the picture, and its shadow and highlight colours could not be changed.

## ✨ Added

- Transform layers: move, scale, rotate, tilt and fade several tracks as one, without nesting them in a composite. Select clips and choose Transform together (Ctrl+G), or add a Transform track; a bracket in the track headers shows what the layer covers and its foot drags to change that. Each clip keeps its own transform inside the group, layers nest, and Shift+G selects the layer moving the selected clip. Also on Android and through MCP (`make_transform_layer`, `set_transform_span`, `add_track` type `transform`).

## 🎨 Improved
