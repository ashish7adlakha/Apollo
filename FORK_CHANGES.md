# What this fork changes

This fork of Apollo adds picture fixes for displays that do not show SDR or HDR correctly
(in my case: XR glasses connected to a Samsung S25 Ultra).

Everything here is compared to upstream Apollo at commit `adc5c5a0`.
All new settings are in the web UI under **Configuration → Audio/Video**.
The picture fixes work on Windows hosts (GPU capture path).

---

## 1. SDR fixes

### sRGB clamp for Display P3 screens
- **Problem:** A wide-colour (P3) screen shows normal sRGB content with colours that are too strong.
- **Fix:** Apollo converts the colours so they look normal on a P3 screen.
- **Setting:** `sdr_display_p3` (checkbox). Advanced: `sdr_colorspace` = `p3`, `rec709`, `rec2020` or `rec601`.

### SDR gamma correction
- **Problem:** The screen's gamma is not the gamma you want (picture looks too flat or too dark).
- **Fix:** You tell Apollo the gamma your screen really has and the gamma you want. Apollo bends the picture to match.
- **Settings:** `sdr_display_gamma` (what the screen has, default 2.2) and `sdr_target_gamma` (what you want, default 2.2).
  If both are the same, nothing changes.

### SDR colour range
- **Problem:** Host and screen disagree about "full" or "limited" range. Blacks get crushed and whites get clipped.
- **Setting:** `sdr_color_range`
  - `auto` – use what the Moonlight client asks for
  - `limited` – always 16–235
  - `full` – always 0–255
  - `remap` – "HDMI Legal Range Remap": squeeze the picture into a safe range so nothing is crushed or clipped

---

## 2. HDR fixes

### HDR colour range
- **Problem:** Same range mismatch as SDR, but in HDR. This is the main bug with my glasses:
  black crush and white clipping.
- **Setting:** `hdr_color_range` with the same four choices: `auto`, `limited` (64–940), `full` (0–1023), `remap`.
- The old checkbox `hdr_limited_range` still works when the mode is `auto`.

### HDR black level lift
- **What it does:** Moves the darkest level up or down a little.
- **Setting:** `hdr_black_lift`, from -30 to +30. Default 0.
  Raise it if shadows are crushed. Lower it if blacks look grey.

### HDR shadow curve
- **What it is for:** Shadows look too bright compared to a reference monitor.
- **Setting:** `hdr_shadow_gamma`, from 0.8 to 2.5. Default 1.25. Values above 1.0 make the picture darker.
  Set 1.0 to turn it off.
- **Setting:** `hdr_shadow_slope`, from 0.25 to 4. Default 1.0 (off). It makes every brightness step below
  the knee bigger (above 1.0, darker shadows) or smaller (below 1.0). Gamma bends the curve; slope tilts it.
  On the EOTF chart: if the shadow bands all vanish at the same reading, use slope = 1 minus the reading.
- **Setting:** `hdr_shadow_knee` (in nits, default 10000). The curve only changes brightness below this level.
  10000 bends the whole range (the old behaviour). Try 100 to change only the shadows.
- Works in **every** HDR colour range mode (auto, limited, full, remap) and in 4:2:0 and 4:4:4.
- Changes show in the stream right away. Both controls are in the "HDR EOTF Check" box.

### HDR EOTF check chart (new)
- **Problem:** You cannot see by eye if a display follows the HDR brightness curve (EOTF).
- **Fix:** A test chart shown in the stream. It has 12 bands by default (you can pick 8 to 15), from bright (top) to dark (bottom).
  Every band is half as bright as the one above, so more bands reach darker levels.
  - Each band has fine lines of "black" and "level L". From a distance the eye sees exactly half of L.
    The display cannot get this wrong, so it is the reference.
  - Inside each band is a solid strip that goes from darker (left) to brighter (right).
  - The strip disappears where it matches the lines. **At the centre mark = correct.**
    Left of centre = this level is shown too bright. Right of centre = too dark.
  - The ruler on top shows the error in stops. You can zoom the ruler (±1, ±0.5, ±0.25 stop).
  - Each band shows the error of one halving step only, so small errors add up down the chart.
  - Top strip: bright steps (100 to 10000 nits) to find where the display clips.
    Bottom strip: dark steps (0.005 to 2 nits) to find black crush.
- **Where:** "HDR EOTF Check" in the web UI. Only **Esc** (or the button) hides the chart,
  so you can click and type in the web UI while it stays on screen.
- **Needs:** an HDR stream at the display's own resolution with no scaling, and a high bitrate.

### HDR brightness fix per band (new)
- **Fix:** One slider per chart band. Set it to the value you read on the ruler; the strip moves to the centre.
  Apollo adds the values up from the top band down and changes the brightness of each level.
  The top level is the fixed anchor (the eye cannot judge absolute brightness).
- Each slider goes from -5 to +5 stops.
- **Final Adjustment graph:** one line that shows the band fixes, shadow slope and shadow gamma added together:
  how much each brightness level is changed, in stops. Move the mouse over it to read a level.
- Press **Save EOTF Correction** to keep the values.
- **Settings in the config file:** `hdr_eotf_enabled`, `hdr_eotf_data`.

### HDR multi-point colour calibration
- **Problem:** Grey is not neutral at some brightness levels (for example a pink tint near 10 nits or 1200 nits).
- **Fix:** A list of brightness points (up to 16). Each point has a red, green and blue multiplier.
  Apollo blends smoothly between the points.
- **Where:** "HDR Calibration Studio" in the web UI. Changes show in the stream right away.
  Press **Save Calibration Profile** to keep them.
- **Settings in the config file:** `hdr_calibration_enabled`, `hdr_calibration_data`.

### In-stream test pattern
- **What it does:** Replaces the stream picture with a grey box at a chosen brightness (in nits),
  so you can judge the tint while moving the sliders.
- Two sizes: a small 10% window, or full screen.
- Any key press, mouse click or touch hides it again.
- Works only while streaming in HDR.

---

## 3. Other changes

- **New web API:** `GET /api/hdr-calibration`, `POST /api/hdr-calibration` (live change),
  `POST /api/hdr-calibration/save` (write to config file).
  The same three for the EOTF check: `/api/hdr-eotf` and `/api/hdr-eotf/save`.
- **Chart files without Apollo:** `tools/eotf_pattern/make_eotf_chart.py` writes the same chart as an
  HDR PNG and as a full range HDR10 video (add `--limited` for a limited range one too). Play them on
  the phone to test the glasses alone. Run `tools/eotf_pattern/setup.ps1` once first: it puts a portable Python inside that
  folder and installs nothing on the system.
- **Build:** a GitHub Actions workflow (`.github/workflows/build.yml`) to build the fork remotely.
- **Encoder:** one H.264 encoder's SDR profile is fixed to `high` (this was a build fix).
- **Logs:** the log now says which colour mode and colour range the stream uses.

---

## 4. Known problems

- The test patterns, the EOTF fixes and HDR calibration only work on Windows, and only in HDR streams.
- `hdr_shadow_gamma` now also applies in `auto`, `limited` and `full` mode. With the default 1.25 those
  modes look darker than before. Set 1.0 if you do not want that.
- Colour range mode and black lift still need a stream restart to change.
- The chart shows the shape of the curve, not absolute brightness.
