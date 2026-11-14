"""Write 10% window grey patches, one file per brightness level, for measuring the EOTF with a camera.

Each file is a black screen with a grey box in the middle that covers 10% of the screen area.
The level in nits is in the file name and, very dim, in the bottom left corner.

    tools\\eotf_pattern\\python\\python.exe tools\\eotf_pattern\\make_patches.py

Output: tools\\eotf_pattern\\out\\patches\\  (HDR PNG and full range HDR10 MP4 per level)
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from make_eotf_chart import Canvas, find_ffmpeg, format_nits, write_hdr_mp4, write_hdr_png  # noqa: E402

DEFAULT_LEVELS = [0, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 25, 50, 75, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000]


def render_patch(width, height, nits, window_percent, label):
    c = Canvas(width, height)
    scale = math.sqrt(window_percent / 100.0)
    bw = int(round(width * scale / 2)) * 2
    bh = int(round(height * scale / 2)) * 2
    x0 = (width - bw) // 2
    y0 = (height - bh) // 2
    c.fill(x0, y0, x0 + bw, y0 + bh, nits)
    if label:
        # Small and dim, far from the box, so it adds almost no stray light
        fs = max(1, height // 540)
        pad = max(4, height // 60)
        c.text(pad, height - pad - 7 * fs, format_nits(nits) if nits > 0 else "0", fs, min(max(nits * 0.5, 0.5), 10.0))
    return c.px


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Write 10% window grey patches for measuring the EOTF with a camera.")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--levels", type=float, nargs="+", default=DEFAULT_LEVELS, help="brightness levels in nits")
    ap.add_argument("--window", type=float, default=10.0, help="box size in percent of the screen area (default 10)")
    ap.add_argument("--seconds", type=float, default=15.0, help="length of each MP4 (default 15)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--max-nits", type=float, default=None, help="mastering peak and MaxCLL written to every MP4 (default: the highest level, at least 1000)")
    ap.add_argument("--no-label", action="store_true", help="do not write the level in the corner")
    ap.add_argument("--no-video", action="store_true", help="only write the PNG files")
    ap.add_argument("--out", default=os.path.join(here, "out", "patches"))
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    levels = sorted(set(min(max(v, 0.0), 10000.0) for v in args.levels))
    # The same HDR metadata in every file, so the display does not tone-map each level differently
    max_nits = args.max_nits if args.max_nits else max(max(levels), 1000.0)
    ffmpeg = None if args.no_video else find_ffmpeg()
    if not args.no_video and not ffmpeg:
        print("imageio-ffmpeg is not installed, writing PNG files only (run setup.ps1).")

    for i, nits in enumerate(levels):
        px = render_patch(args.width, args.height, nits, args.window, not args.no_label)
        # The number in front keeps the files in order from dark to bright
        name = "patch_%02d_%snits" % (i, format_nits(nits) if nits > 0 else "0")
        write_hdr_png(os.path.join(args.out, name + "_pq.png"), px)
        if ffmpeg:
            write_hdr_mp4(os.path.join(args.out, name + "_hdr10_full.mp4"), px, True, args.seconds, args.fps, max_nits, ffmpeg)
        print("wrote", name)

    print("%d levels in %s (HDR metadata peak: %d nits)" % (len(levels), args.out, int(max_nits)))


if __name__ == "__main__":
    main()
