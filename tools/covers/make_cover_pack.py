#!/usr/bin/env python3
"""Build a cover pack for a3DO from a folder of full-wrap box scans.

    make_cover_pack.py SOURCE_DIR OUTPUT_DIR --dat REDUMP.dat
                       [--aliases aliases.txt] [--height 640] [--quality 85]

The source can mix the two naming schemes the scans arrive in:

    3do.<slug>_<US|JP|EU>.<id>.jpg      thecoverproject.net downloads
    3do_<slug>[_au|_jp][_N].jpg         arcadeartwork.org downloads

Each game gets at most one cover per region, written as

    <key>.jpg  <key>_eu.jpg  <key>_jp.jpg

which is the naming the app's CoverIndex reads (src/ui/cover_art.cpp). A
plain scan beats a numbered variant ("_2"), and a thecoverproject scan beats
an arcadeartwork one, since its template is the same for every game.

<key> is the key of the game's Redump title, not the scan's slug. The scans
carry typos and short names ("familyfued", "hell"), while most discs are named
after Redump ("Family Feud (USA).chd"), so naming the covers after Redump
makes those discs match exactly. Each scan is matched to a title in the
Redump DAT (http://redump.org/datfile/3do/): through aliases.txt, exactly, by
prefix, or by a close spelling, preferring titles from the scan's own region.
Every match that is not exact is printed for review; correct a wrong one, or
name a scan Redump does not list, with a line in aliases.txt.

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
import difflib
import html
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
ROMAN = {"ii": "2", "iii": "3", "iv": "4"}


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


def title_key(title):
    """CoverIndex::key for a bracket-free title: "The Horde" -> "horde"."""
    text = title.lower().strip()
    if re.match(r"^the[ ,\-_]", text):
        text = text[4:]
    if re.search(r"[ ,\-_]the$", text):
        text = text[:-4]
    words = re.split(r"([^a-z0-9]+)", text)
    text = "".join(ROMAN.get(w, w) for w in words)
    return "".join("and" if c == "&" else c for c in text if c.isascii() and (c.isalnum() or c == "&"))


def title_region(tags):
    """CoverIndex::title_region, on the bracketed tags only."""
    if "usa" in tags:
        return "us"
    if "japan" in tags:
        return "jp"
    if any(e in tags for e in ("europe", "pal", "uk", "germany", "france", "spain",
                               "italy", "australia", "netherlands", "sweden")):
        return "eu"
    return "us"


def load_dat(path):
    """{key: (title, {regions})} for every game in a Redump DAT."""
    titles = {}
    with open(path, encoding="utf-8") as f:
        for name in re.findall(r'<game name="([^"]*)"', f.read()):
            name = html.unescape(name)
            base = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", name).strip()
            # "Incredible Machine, The" -> "The Incredible Machine"
            if base.endswith(", The"):
                base = "The " + base[:-5]
            tags = " ".join(re.findall(r"[\(\[]([^\)\]]*)", name.lower()))
            entry = titles.setdefault(title_key(base), (base, set()))
            entry[1].add(title_region(tags))
    return titles


def load_aliases(path):
    """{slug: redump title}. Lines are "slug<TAB>Title"; '#' starts a comment.
    "slug_jp" (or _eu, _us) applies to that region's scan only."""
    aliases = {}
    if path and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].rstrip()
                if "\t" in line:
                    slug, title = line.split("\t", 1)
                    aliases[slug.strip().lower()] = title.strip()
    return aliases


def resolve(slug, region, titles, aliases):
    """(key, how) for a scan's slug."""
    for name in (f"{slug}_{region}", slug):
        if name in aliases:
            return title_key(aliases[name]), "alias"
    key = title_key(slug)
    # Slugs have no spaces, so "thehorde" and "coventhe" keep their "the".
    if key.startswith("the") and key[3:] in titles:
        key = key[3:]
    elif key.endswith("the") and key[:-3] in titles:
        key = key[:-3]
    if key in titles:
        return key, "exact"
    local = [k for k, (_, regions) in titles.items() if region in regions]
    for pool in (local, list(titles)):
        # "hell" -> "Hell - A Cyberpunk Thriller": the shortest title that
        # starts with the slug.
        if len(key) >= 4:
            longer = sorted((k for k in pool if k.startswith(key)), key=len)
            if longer:
                return longer[0], "prefix"
        close = difflib.get_close_matches(key, pool, n=1, cutoff=0.86)
        if close:
            return close[0], "close"
    return key, "none"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source")
    parser.add_argument("output")
    parser.add_argument("--dat", required=True, help="Redump 3DO DAT file")
    parser.add_argument("--aliases",
                        default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                             "aliases.txt"))
    parser.add_argument("--height", type=int, default=640)
    parser.add_argument("--quality", type=int, default=85)
    args = parser.parse_args()

    titles = load_dat(args.dat)
    aliases = load_aliases(args.aliases)

    chosen = {}      # (key, region) -> (rank, name)
    skipped = []
    review = []
    for name in sorted(os.listdir(args.source)):
        info = classify(name)
        if info is None:
            skipped.append(name)
            continue
        slug, region, rank = info
        key, how = resolve(slug, region, titles, aliases)
        if how != "exact":
            shown = titles[key][0] if key in titles else "(not in Redump)"
            review.append(f"  {how:6s} {slug:44s} -> {shown}")
        best = chosen.get((key, region))
        if best is None or rank < best[0]:
            chosen[(key, region)] = (rank, name)

    os.makedirs(args.output, exist_ok=True)
    manifest = []
    for (key, region), (_, name) in sorted(chosen.items()):
        out_name = key + ("" if region == "us" else "_" + region) + ".jpg"
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
    print(f"{len(manifest)} covers, {total / 1e6:.1f} MB -> {args.output}")
    if review:
        print(f"\n{len(review)} scans matched to Redump by something other than their exact name:")
        print("\n".join(review))
    if skipped:
        print(f"\nSkipped {len(skipped)} unrecognised names:", file=sys.stderr)
        for name in skipped:
            print("  " + name, file=sys.stderr)


if __name__ == "__main__":
    main()
