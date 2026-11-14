"""Write the EOTF null chart as HDR files (PNG and HDR10 MP4).

The chart is the same one Apollo can show inside the stream (src/eotf_chart.cpp).
Keep the layout code of both files in sync.

Use the portable Python from setup.ps1:

    tools\\eotf_pattern\\python\\python.exe tools\\eotf_pattern\\make_eotf_chart.py --width 1920 --height 1080

The files must be shown pixel for pixel. Any scaling breaks the fine reference lines.
"""

import argparse
import math
import os
import struct
import subprocess
import sys
import zlib

import numpy as np

DEFAULT_BANDS = 12
MAX_BANDS = 15  # the stream correction has 16 points: the anchor plus one per band

HIGHLIGHT_STEPS = [100, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 4000, 10000]
BLACK_STEPS = [0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0]
STEP_COUNT = 12

# 5x7 font, one row per value, bit 4 is the leftmost pixel
FONT = {
    "0": [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],
    "1": [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
    "2": [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
    "3": [0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E],
    "4": [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02],
    "5": [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
    "6": [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E],
    "7": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
    "8": [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E],
    "9": [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
    ".": [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C],
    "+": [0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00],
    "-": [0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00],
}

# SMPTE ST 2084 (PQ) constants
M1 = 2610.0 / 4096.0 / 4.0
M2 = 2523.0 / 4096.0 * 128.0
C1 = 3424.0 / 4096.0
C2 = 2413.0 / 4096.0 * 32.0
C3 = 2392.0 / 4096.0 * 32.0


def nits_to_pq(nits):
    lp = np.power(np.clip(np.asarray(nits, dtype=np.float64) / 10000.0, 0.0, 1.0), M1)
    return np.power((C1 + C2 * lp) / (1.0 + C3 * lp), M2)


def pq_to_nits(pq):
    pp = np.power(np.clip(np.asarray(pq, dtype=np.float64), 0.0, 1.0), 1.0 / M2)
    num = np.maximum(pp - C1, 0.0)
    den = np.maximum(C2 - C3 * pp, 1e-9)
    return 10000.0 * np.power(num / den, 1.0 / M1)


def cdiv(a, b):
    """Integer division that rounds toward zero, like C++."""
    return int(a / b)


def lround(v):
    return int(math.floor(v + 0.5))


def format_nits(v):
    if v >= 100.0:
        return "%.0f" % v
    if v >= 10.0:
        return "%.1f" % v
    if v >= 1.0:
        return "%.2f" % v
    return "%.3f" % v


def format_stops(v):
    """Signed value with up to 3 decimals and no trailing zeros, e.g. "+0.5", "-0.125", "0"."""
    if abs(v) < 0.0005:
        return "0"
    return ("%+.3f" % v).rstrip("0").rstrip(".")


def text_width(text, scale):
    return len(text) * 6 * scale - scale if text else 0


class Canvas:
    def __init__(self, width, height):
        self.w = width
        self.h = height
        self.px = np.zeros((height, width), dtype=np.float32)

    def fill(self, x0, y0, x1, y1, nits):
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, self.w), min(y1, self.h)
        if x1 > x0 and y1 > y0:
            self.px[y0:y1, x0:x1] = nits

    def text(self, x, y, text, scale, nits):
        for ch in text:
            rows = FONT.get(ch)
            if rows:
                for row in range(7):
                    for col in range(5):
                        if rows[row] & (0x10 >> col):
                            self.fill(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, nits)
            x += 6 * scale


def layout(width, height, peak_nits, line_px, end_strips, range_stops=1.0, bands=DEFAULT_BANDS):
    """Positions shared by the renderer and the self test. Returns None if the image is too small."""
    if width < 64 or height < 64:
        return None

    lay = {}
    lay["peak"] = min(max(float(peak_nits), 10.0), 10000.0)
    lay["range"] = min(max(float(range_stops), 0.1), 2.0)
    band_count = min(max(int(bands), 4), MAX_BANDS)
    lay["bands"] = band_count
    line_px = min(max(int(line_px), 1), 8)

    fs = max(1, height // 540)
    glyph_h = 7 * fs
    pad = max(2, height // 90)
    margin_x = 6 * 6 * fs + 2 * pad
    x0 = margin_x
    x1 = width - margin_x
    chart_w = x1 - x0
    if chart_w < 32:
        return None
    strip_h = max(8, height // 20)

    y = pad
    lay["top_strip_y"] = y
    if end_strips:
        y += strip_h + pad // 2 + glyph_h + pad
    lay["ruler_y"] = y
    y += glyph_h + pad // 2

    bottom_reserve = pad + (strip_h + pad if end_strips else 0)
    avail = height - y - bottom_reserve
    gap = max(4, height // 90)

    while True:
        band_raw = avail // band_count - gap
        third = cdiv(cdiv(band_raw, 3), 2 * line_px) * (2 * line_px)
        if third > 0 or line_px == 1:
            break
        line_px //= 2
    if third <= 0:
        return None

    band_h = 3 * third
    total = band_count * (band_h + gap) + gap

    lay.update(
        fs=fs, glyph_h=glyph_h, pad=pad, x0=x0, x1=x1, chart_w=chart_w, strip_h=strip_h,
        slot_w=chart_w // STEP_COUNT, gap=gap, third=third, band_h=band_h, line_px=line_px,
        y_start=y + max(0, (avail - total) // 2),
    )
    lay["patch_w"] = lay["slot_w"] * 3 // 4
    return lay


def render(width, height, peak_nits=1000.0, line_px=2, end_strips=True, range_stops=1.0, bands=DEFAULT_BANDS, reference="lines"):
    """Return the chart as luminance in nits, shape (height, width)."""
    c = Canvas(width, height)
    lay = layout(width, height, peak_nits, line_px, end_strips, range_stops, bands)
    if lay is None:
        return c.px

    fs, glyph_h, pad = lay["fs"], lay["glyph_h"], lay["pad"]
    x0, x1, chart_w = lay["x0"], lay["x1"], lay["chart_w"]
    strip_h, slot_w, patch_w = lay["strip_h"], lay["slot_w"], lay["patch_w"]
    gap, third, band_h, line_px = lay["gap"], lay["third"], lay["band_h"], lay["line_px"]
    peak = lay["peak"]

    # Top strip: highlight steps with their nits value written below
    if end_strips:
        y = lay["top_strip_y"]
        for i in range(STEP_COUNT):
            px0 = x0 + i * slot_w + (slot_w - patch_w) // 2
            c.fill(px0, y, px0 + patch_w, y + strip_h, HIGHLIGHT_STEPS[i])
            label = format_nits(HIGHLIGHT_STEPS[i])
            c.text(px0 + cdiv(patch_w - text_width(label, fs), 2), y + strip_h + pad // 2, label, fs, 30.0)

    rng = lay["range"]

    def pos_to_x(pos):
        """pos runs from -1 (left edge) to +1 (right edge)."""
        return x0 + lround((pos + 1.0) * 0.5 * (chart_w - 1))

    # Ruler: error in stops
    for i in range(5):
        pos = -1.0 + 0.5 * i
        label = format_stops(pos * rng)
        c.text(pos_to_x(pos) - text_width(label, fs) // 2, lay["ruler_y"], label, fs, 30.0)

    # The solid strip sweeps -range..+range stops across the chart width
    pos = -1.0 + 2.0 * np.arange(chart_w, dtype=np.float64) / (chart_w - 1)
    sweep = np.power(2.0, pos * rng).astype(np.float32)

    # 20 divisions across the chart, long ticks where the ruler has a number
    def draw_ticks(ty, nits):
        tick_w = max(1, fs)
        for i in range(-10, 11):
            tx = pos_to_x(i / 10.0) - tick_w // 2
            th = gap if i % 5 == 0 else gap // 2
            c.fill(tx, ty + gap - th, tx + tick_w, ty + gap, nits)

    for k in range(lay["bands"]):
        ref = peak / (2.0 ** k)  # level of the bright lines
        target = ref * 0.5  # what the lines average to
        mark = min(max(ref, 4.0), 40.0)  # labels and ticks, kept dim near dark bands

        gy = lay["y_start"] + k * (band_h + gap)
        by = gy + gap
        draw_ticks(gy, mark)

        for r in range(band_h):
            yy = by + r
            if yy >= height:
                break
            if third <= r < 2 * third:
                c.px[yy, x0:x1] = target * sweep
            elif reference == "solid":
                # Experiment: a flat block at the target level instead of lines.
                # It goes through the same display curve as the gradient, so it cannot show EOTF errors.
                c.px[yy, x0:x1] = target
            elif (r // line_px) % 2 == 0:
                c.px[yy, x0:x1] = ref

        # Left: centre value of the solid strip. Right: level of the reference lines.
        left = format_nits(target)
        right = format_nits(ref)
        ly = by + cdiv(band_h - glyph_h, 2)
        c.text(x0 - pad - text_width(left, fs), ly, left, fs, mark)
        c.text(x1 + pad, ly, right, fs, mark)

    # Bottom strip: near-black steps, no labels so nothing bright sits next to them
    if end_strips:
        sy = height - pad - strip_h
        for i in range(STEP_COUNT):
            px0 = x0 + i * slot_w + (slot_w - patch_w) // 2
            c.fill(px0, sy, px0 + patch_w, sy + strip_h, BLACK_STEPS[i])

    return c.px


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(path, rgb, bit_depth, cicp=None):
    """Write an RGB PNG. rgb has shape (h, w, 3), dtype uint8 or uint16."""
    h, w, _ = rgb.shape
    raw = rgb.astype(">u2").tobytes() if bit_depth == 16 else rgb.astype(np.uint8).tobytes()
    stride = w * 3 * (bit_depth // 8)
    rows = bytearray()
    for y in range(h):
        rows += b"\x00"  # filter type: none
        rows += raw[y * stride:(y + 1) * stride]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bit_depth, 2, 0, 0, 0)))
        if cicp:
            f.write(png_chunk(b"cICP", bytes(cicp)))
        f.write(png_chunk(b"IDAT", zlib.compress(bytes(rows), 6)))
        f.write(png_chunk(b"IEND", b""))


def write_hdr_png(path, nits):
    pq = nits_to_pq(nits)
    code = np.round(pq * 65535.0).astype(np.uint16)
    # cICP: BT.2020 primaries (9), PQ transfer (16), RGB matrix (0), full range (1)
    write_png(path, np.repeat(code[:, :, None], 3, axis=2), 16, cicp=(9, 16, 0, 1))


def find_ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


def write_hdr_mp4(path, nits, full_range, seconds, fps, max_nits, ffmpeg):
    """Write a still HDR10 (HEVC Main10, BT.2020, PQ) video. Grey only, so chroma is neutral."""
    h, w = nits.shape
    h2, w2 = h - h % 2, w - w % 2
    pq = nits_to_pq(nits[:h2, :w2])
    if full_range:
        luma = np.round(pq * 1023.0)
    else:
        luma = np.round(64.0 + pq * 876.0)
    luma = luma.astype("<u2")
    chroma = np.full((h2 // 2) * (w2 // 2) * 2, 512, dtype="<u2")

    raw_path = path + ".yuv"
    with open(raw_path, "wb") as f:
        f.write(luma.tobytes())
        f.write(chroma.tobytes())

    rng = "pc" if full_range else "tv"
    frames = max(1, int(seconds * fps))
    max_lum = int(round(max_nits * 10000))
    x265 = (
        "colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=%s:hdr10=1:repeat-headers=1:"
        "master-display=G(8500,39850)B(6550,2300)R(35400,14600)WP(15635,16450)L(%d,50):max-cll=%d,%d:log-level=error"
        % ("full" if full_range else "limited", max_lum, int(max_nits), int(max_nits))
    )
    cmd = [
        ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "yuv420p10le", "-color_range", rng, "-s", "%dx%d" % (w2, h2), "-r", str(fps),
        "-stream_loop", str(frames - 1), "-i", raw_path,
        "-c:v", "libx265", "-preset", "medium", "-crf", "6", "-pix_fmt", "yuv420p10le",
        "-color_range", rng, "-color_primaries", "bt2020", "-color_trc", "smpte2084", "-colorspace", "bt2020nc",
        "-x265-params", x265, "-tag:v", "hvc1", "-movflags", "+faststart", path,
    ]
    try:
        subprocess.run(cmd, check=True)
    finally:
        os.remove(raw_path)


def simulate(nits, lay, gamma, preview_path, reference="lines"):
    """Pretend the display applies an extra gamma to the PQ signal and report where each strip vanishes."""
    def display(x):
        return pq_to_nits(np.power(nits_to_pq(x), gamma))

    peak = lay["peak"]
    chart_w = lay["chart_w"]
    stops = (-1.0 + 2.0 * np.arange(chart_w) / (chart_w - 1)) * lay["range"]
    print("Simulated display: PQ signal ^ %.3f" % gamma)
    print("  %10s  %-26s %s" % ("band nits", "strip vanishes at (stops)", "total error so far (stops)"))
    total = 0.0
    for k in range(lay["bands"]):
        ref = peak / (2.0 ** k)
        if reference == "solid":
            seen_ref = display(ref * 0.5)  # a flat block is bent by the display like everything else
        else:
            seen_ref = (display(ref) + display(0.0)) / 2.0  # the eye averages the lines in linear light
        seen_strip = display(ref * 0.5 * np.power(2.0, stops))
        null = stops[int(np.argmin(np.abs(seen_strip - seen_ref)))]
        total += null
        print("  %10s  %+-26.3f %+.3f" % (format_nits(ref * 0.5), null, total))

    # Preview: what the eye sees once the lines blur together, shown with a PQ-like tone curve
    emitted = display(nits)
    period = 2 * lay["line_px"]
    kernel = np.ones(period) / period
    blurred = np.apply_along_axis(lambda col: np.convolve(col, kernel, mode="same"), 0, emitted)
    grey = np.round(nits_to_pq(blurred) * 255.0).astype(np.uint8)
    write_png(preview_path, np.repeat(grey[:, :, None], 3, axis=2), 8)
    print("  preview:", preview_path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Write the EOTF null chart as HDR PNG and HDR10 MP4 files.")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--peak", type=float, default=1000.0, help="reference level of the top band in nits (default 1000)")
    ap.add_argument("--line-px", type=int, default=2, help="thickness of the reference lines in pixels (default 2)")
    ap.add_argument("--range", type=float, default=1.0, dest="range_stops", help="the strips sweep from -RANGE to +RANGE stops; smaller gives a finer reading (default 1)")
    ap.add_argument("--bands", type=int, default=DEFAULT_BANDS, help="number of bands, 4 to %d; every band is half as bright as the one above (default %d)" % (MAX_BANDS, DEFAULT_BANDS))
    ap.add_argument("--reference", choices=["lines", "solid"], default="lines", help="what the gradient is compared with: fine lines (real EOTF test) or a flat block (experiment, cannot show EOTF errors)")
    ap.add_argument("--limited", action="store_true", help="also write a limited range (64-940) MP4; the default is full range only")
    ap.add_argument("--no-end-strips", action="store_true", help="leave out the highlight and near-black strips")
    ap.add_argument("--seconds", type=float, default=10.0, help="length of the MP4 files (default 10)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--max-nits", type=float, default=None, help="mastering peak and MaxCLL written to the MP4 (default: --peak, at least 1000)")
    ap.add_argument("--no-video", action="store_true", help="only write the PNG")
    ap.add_argument("--simulate-gamma", type=float, default=None, help="self test: print where the strips vanish on a display with this extra PQ gamma, and write an SDR preview")
    ap.add_argument("--out", default=os.path.join(here, "out"))
    args = ap.parse_args()

    end_strips = not args.no_end_strips
    lay = layout(args.width, args.height, args.peak, args.line_px, end_strips, args.range_stops, args.bands)
    if lay is None:
        sys.exit("Image size is too small for the chart.")

    os.makedirs(args.out, exist_ok=True)
    nits = render(args.width, args.height, args.peak, args.line_px, end_strips, args.range_stops, args.bands, args.reference)
    name = "eotf_chart_%dx%d_%dnits" % (args.width, args.height, int(lay["peak"]))
    if args.reference == "solid":
        name += "_solidref"
    if lay["bands"] != DEFAULT_BANDS:
        name += "_%dbands" % lay["bands"]
    if abs(lay["range"] - 1.0) > 1e-6:
        name += "_range%s" % format_stops(lay["range"]).lstrip("+")

    if args.simulate_gamma is not None:
        simulate(nits, lay, args.simulate_gamma, os.path.join(args.out, name + "_sim_gamma_%.2f.png" % args.simulate_gamma), args.reference)
        return

    png_path = os.path.join(args.out, name + "_pq.png")
    write_hdr_png(png_path, nits)
    print("wrote", png_path)

    if args.no_video:
        return
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print("imageio-ffmpeg is not installed, skipping the MP4 files (run setup.ps1).")
        return
    max_nits = args.max_nits if args.max_nits else max(lay["peak"], 1000.0)
    for full_range in ((True, False) if args.limited else (True,)):
        mp4_path = os.path.join(args.out, name + ("_hdr10_full.mp4" if full_range else "_hdr10_limited.mp4"))
        write_hdr_mp4(mp4_path, nits, full_range, args.seconds, args.fps, max_nits, ffmpeg)
        print("wrote", mp4_path)


if __name__ == "__main__":
    main()
