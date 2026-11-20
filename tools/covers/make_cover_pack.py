#!/usr/bin/env python3
"""Build a cover pack for a3DO from a folder of full-wrap box scans.

    make_cover_pack.py SOURCE_DIR OUTPUT_DIR [--height 640] [--quality 85]

The source can mix the two naming schemes the scans arrive in:

    3do.<slug>_<US|JP|EU>.<id>.jpg      thecoverproject.net downloads
    3do_<slug>[_au|_jp][_N].jpg         arcadeartwork.org downloads

Each game gets at most one cover per region, written as

    <slug>.jpg  <slug>_eu.jpg  <slug>_jp.jpg

which is the naming the app's CoverIndex reads (src/ui/cover_art.cpp). A
plain scan beats a numbered variant ("_2"), and a thecoverproject scan beats
an arcadeartwork one, since its template is the same for every game.

OUTPUT_DIR ends up holding the images and covers.txt, one "name<TAB>bytes"
line per image. That manifest is what the app downloads first; it then
fetches only images it does not already have at that size. Upload the whole
folder anywhere that serves plain files over HTTPS and point the app's
cover pack URL at it.

The scans are usually 3225x2156, several megabytes each. The app never
draws a side taller than 560 px, so 640 px keeps them sharp at a tenth of
the size.
"""

import argparse
import os
import re
import sys

from PIL import Image

TCP = re.compile(r"^3do\.(?P<slug>[a-z0-9]+)_(?P<region>[A-Za-z]+)\.\d+\.(jpe?g|png)$", re.I)
ARCADE = re.compile(
    r"^3do_(?P<slug>[a-z0-9]+?)(?:_(?P<region>au|eu|jp))?(?:_(?P<variant>\d+))?"
    r"(?: \(\d+\))?\.(jpe?g|png)$", re.I)

REGIONS = {"us": "us", "usa": "us", "jp": "jp", "jpn": "jp", "eu": "eu",
           "au": "eu", "uk": "eu", "pal": "eu"}


def classify(name):
    """(slug, region, rank) or None. Lower rank wins."""
    m = TCP.match(name)
    if m:
        region = REGIONS.get(m["region"].lower())
        return (m["slug"].lower(), region, 0) if region else None
    m = ARCADE.match(name)
    if m:
        region = REGIONS[(m["region"] or "us").lower()]
        variant = 1 if (m["variant"] or "(" in name) else 0
        return m["slug"].lower(), region, 1 + variant
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source")
    parser.add_argument("output")
    parser.add_argument("--height", type=int, default=640)
    parser.add_argument("--quality", type=int, default=85)
    args = parser.parse_args()

    chosen = {}      # (slug, region) -> (rank, name)
    skipped = []
    for name in sorted(os.listdir(args.source)):
        info = classify(name)
        if info is None:
            skipped.append(name)
            continue
        slug, region, rank = info
        best = chosen.get((slug, region))
        if best is None or rank < best[0]:
            chosen[(slug, region)] = (rank, name)

    os.makedirs(args.output, exist_ok=True)
    manifest = []
    for (slug, region), (_, name) in sorted(chosen.items()):
        out_name = slug + ("" if region == "us" else "_" + region) + ".jpg"
        out_path = os.path.join(args.output, out_name)
        src_path = os.path.join(args.source, name)
        if not os.path.exists(out_path) or os.path.getmtime(out_path) < os.path.getmtime(src_path):
            with Image.open(src_path) as image:
                image = image.convert("RGB")
                if image.height > args.height:
                    width = round(image.width * args.height / image.height)
                    image = image.resize((width, args.height), Image.LANCZOS)
                image.save(out_path, "JPEG", quality=args.quality, optimize=True)
        manifest.append((out_name, os.path.getsize(out_path)))
        print(f"{out_name:60s} <- {name}")

    # Anything left over from an earlier run is no longer part of the pack.
    keep = {n for n, _ in manifest} | {"covers.txt"}
    for name in os.listdir(args.output):
        if name not in keep and name.lower().endswith((".jpg", ".jpeg", ".png")):
            os.remove(os.path.join(args.output, name))

    with open(os.path.join(args.output, "covers.txt"), "w", encoding="utf-8") as f:
        f.write("# a3DO cover pack: name<TAB>bytes\n")
        for out_name, size in manifest:
            f.write(f"{out_name}\t{size}\n")

    total = sum(size for _, size in manifest)
    print(f"\n{len(manifest)} covers, {total / 1e6:.1f} MB -> {args.output}")
    if skipped:
        print(f"Skipped {len(skipped)} unrecognised names:", file=sys.stderr)
        for name in skipped:
            print("  " + name, file=sys.stderr)


if __name__ == "__main__":
    main()
