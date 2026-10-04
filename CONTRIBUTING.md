# Contributing to Drift

Thanks for taking the time to help. This is a short guide — the rules that matter are all here.

## Before you start

- For anything larger than a bug fix, open an issue first so the approach can be agreed on before
  you write the code. A rejected PR is a waste of your evening, not just ours.
- Blank issues are disabled. Use one of the [issue templates](.github/ISSUE_TEMPLATE); for bugs,
  include the output of the in-app debug info popup.
- One logical change per pull request. Unrelated fixes belong in their own PR.

## Building and testing

Setup, dependencies, and platform notes live in [docs/BUILDING.md](docs/BUILDING.md).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

**Before opening a pull request, make sure the full test suite passes.** Suites are `Core`,
`EditorState`, `Playback`, `Engine`, `MediaProbe`, `AddonPackage`, and `Translations`.

On a machine with no display, run the suite under Xvfb — not `QT_QPA_PLATFORM=offscreen`, which
cannot create an OpenGL context and makes the compositor tests compare against a null frame:

```bash
QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 1280x1024x24" ctest --test-dir build --output-on-failure
```

If a test is already broken on `main`, say so in the PR rather than working around it.

## Effects and transitions

**Changes to effects and transitions are not accepted in this repository.** Although they are
bundled here so the editor works out of the box, they are maintained as addons — submit new or
updated effects, transitions, templates, and audio effects to
[CutWire-Studios/Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons) instead.

This applies to the `effects/`, `transitions/`, `effect-templates/`, and `audio-effects/` trees.
Changes to the *engine* that runs them — `src/engine/`, the shader host, the catalog loader — do
belong here.

## Code style

- **Commenting is highly encouraged.** Explain why the code does what it does, especially where a
  workaround exists for a driver bug, a platform quirk, or a Qt/FFmpeg behaviour that is not
  obvious from reading the lines. The existing source and CI files are commented in this style;
  match them.
- Match the formatting of the file you are editing. There is no enforced formatter.
- C++20, Qt 6, QML. Keep UI logic in QML and heavy work off the GUI thread.
- User-visible strings must be translatable: `qsTr()` in QML, `tr()` or
  `QCoreApplication::translate()` in C++. After adding or changing them, run
  `cmake --build build --target update_translations` and commit the updated `i18n/*.ts` files.
  Do not commit `.qm` files.

## Commits

Use [Conventional Commits](https://www.conventionalcommits.org/) — the prefixes already in use are
`feat:`, `fix:`, `perf:`, `docs:`, `chore:`, and `ci:`. Reference the issue in the subject when the
commit closes one:

```
feat: add timeline close-gap and per-clip split actions (#73)
fix: shared video decode cursors causing performance degradation in overlapping clips
```

Do not update `CHANGELOG.md` — maintainers write it at release time.

## Use of AI

AI-assisted work is welcome, within limits. The point of these rules is that a human is
accountable for everything that lands in the repository.

**Allowed:**

- Submitting AI-generated or AI-assisted code.

**Prohibited:**

- Automating commits or pull requests with AI. A human must drive the submission.
- Writing commit messages with AI.
- Using AI to open issues, discussions, or comments.

**Required:** any AI-generated code must be reviewed by you *before* it is committed. You are
submitting it as your own work, and you are expected to be able to explain and defend every line of
it in review.

Note that AI-generated commit messages are typically accompanied by a `Co-Authored-By` trailer for
the AI tool — that trailer is a sign the rules above were not followed, so remove such trailers and
write the message yourself.

## Licence

Drift is GPL-3.0. By contributing, you agree that your contributions are licensed under the same
terms. Do not submit code you do not have the right to relicense — including code copied from
projects under an incompatible licence.
