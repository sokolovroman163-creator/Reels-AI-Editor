#!/usr/bin/env python3
"""Extract a single <release> entry's notes from the Flatpak AppStream
metainfo file and render them as Markdown, for use as GitHub release notes.

The metainfo file is the single source of release notes: Flathub shows it in
the software centre, and the release workflow reuses the same text so the two
can never drift apart.

Usage: extract_release_notes.py <version> <output-file>
       (version without a leading "v", e.g. "0.1.0")
"""

import sys
import xml.etree.ElementTree as ET

METAINFO_PATH = "flatpak/org.cutwire.Drift.metainfo.xml"


def render(description: ET.Element) -> str:
    parts = []
    for child in description:
        if child.tag == "p":
            text = " ".join("".join(child.itertext()).split())
            if text:
                parts.append(text)
        elif child.tag in ("ul", "ol"):
            items = []
            for li in child:
                text = " ".join("".join(li.itertext()).split())
                if text:
                    items.append(f"- {text}")
            if items:
                parts.append("\n".join(items))
    return "\n\n".join(parts)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <version> <output-file>", file=sys.stderr)
        return 2

    version, out_path = sys.argv[1], sys.argv[2]

    root = ET.parse(METAINFO_PATH).getroot()
    releases = root.find("releases")
    release = None
    if releases is not None:
        release = next(
            (r for r in releases.findall("release") if r.get("version") == version),
            None,
        )

    if release is None:
        print(
            f'::error::No <release version="{version}"> entry found in {METAINFO_PATH}',
            file=sys.stderr,
        )
        return 1

    description = release.find("description")
    notes = render(description) if description is not None else ""
    if not notes.strip():
        print(
            f'::error::<release version="{version}"> in {METAINFO_PATH} has no release notes',
            file=sys.stderr,
        )
        return 1

    with open(out_path, "w") as f:
        f.write(notes + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
