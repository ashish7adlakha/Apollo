/**
 * @file src/eotf_chart.cpp
 * @brief Definitions for the EOTF null chart test pattern.
 * @note tools/eotf_pattern/make_eotf_chart.py draws the same layout. Keep them in sync.
 */
// class header include
#include "eotf_chart.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace video::eotf_chart {
  namespace {
    // 5x7 font, one row per byte, bit 4 is the leftmost pixel
    struct glyph_t {
      char ch;
      unsigned char rows[7];
    };

    constexpr glyph_t font[] = {
      {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
      {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
      {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
      {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
      {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
      {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
      {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
      {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
      {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
      {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
      {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
      {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    };

    // Highlight steps (top strip): neighbours that look the same are clipped by the display
    constexpr float highlight_steps[] = {100, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 4000, 10000};
    // Near-black steps (bottom strip): steps that cannot be told from black are crushed
    constexpr float black_steps[] = {0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    constexpr int step_count = 12;

    struct canvas_t {
      int w;
      int h;
      std::vector<float> &px;

      void fill(int x0, int y0, int x1, int y1, float nits) {
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, w);
        y1 = std::min(y1, h);
        for (int y = y0; y < y1; ++y) {
          for (int x = x0; x < x1; ++x) {
            px[(size_t) y * w + x] = nits;
          }
        }
      }

      void text(int x, int y, const std::string &str, int scale, float nits) {
        for (char c : str) {
          for (const auto &g : font) {
            if (g.ch != c) {
              continue;
            }
            for (int row = 0; row < 7; ++row) {
              for (int col = 0; col < 5; ++col) {
                if (g.rows[row] & (0x10 >> col)) {
                  fill(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, nits);
                }
              }
            }
            break;
          }
          x += 6 * scale;
        }
      }
    };

    int text_width(const std::string &str, int scale) {
      return str.empty() ? 0 : (int) str.size() * 6 * scale - scale;
    }

    std::string format_nits(float v) {
      char buf[32];
      if (v >= 100.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
      } else if (v >= 10.0f) {
        std::snprintf(buf, sizeof(buf), "%.1f", v);
      } else if (v >= 1.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f", v);
      } else {
        std::snprintf(buf, sizeof(buf), "%.3f", v);
      }
      return buf;
    }

    // Signed value with up to 3 decimals and no trailing zeros, e.g. "+0.5", "-0.125", "0"
    std::string format_stops(float v) {
      if (std::abs(v) < 0.0005f) {
        return "0";
      }
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%+.3f", v);
      std::string s = buf;
      while (s.back() == '0') {
        s.pop_back();
      }
      if (s.back() == '.') {
        s.pop_back();
      }
      return s;
    }
  }  // namespace

  std::vector<float> render(int width, int height, const params_t &params) {
    std::vector<float> px((size_t) std::max(width, 0) * std::max(height, 0), 0.0f);
    if (width < 64 || height < 64) {
      return px;
    }

    canvas_t c {width, height, px};

    const float peak = std::clamp(params.peak_nits, 10.0f, 10000.0f);
    int line_px = std::clamp(params.line_px, 1, 8);
    const float range = std::clamp(params.range_stops, 0.1f, 2.0f);
    const int band_count = std::clamp(params.bands, 4, max_band_count);

    const int fs = std::max(1, height / 540);  // font scale
    const int glyph_h = 7 * fs;
    const int pad = std::max(2, height / 90);
    const int margin_x = 6 * 6 * fs + 2 * pad;
    const int x0 = margin_x;
    const int x1 = width - margin_x;
    const int chart_w = x1 - x0;
    if (chart_w < 32) {
      return px;
    }
    const int strip_h = std::max(8, height / 20);
    const int slot_w = chart_w / step_count;
    const int patch_w = slot_w * 3 / 4;

    int y = pad;

    // Top strip: highlight steps with their nits value written below
    if (params.end_strips) {
      for (int i = 0; i < step_count; ++i) {
        int px0 = x0 + i * slot_w + (slot_w - patch_w) / 2;
        c.fill(px0, y, px0 + patch_w, y + strip_h, highlight_steps[i]);
        auto label = format_nits(highlight_steps[i]);
        c.text(px0 + (patch_w - text_width(label, fs)) / 2, y + strip_h + pad / 2, label, fs, 30.0f);
      }
      y += strip_h + pad / 2 + glyph_h + pad;
    }

    // Ruler: error in stops. `pos` runs from -1 (left edge) to +1 (right edge).
    const auto pos_to_x = [&](float pos) {
      return x0 + (int) std::lround((pos + 1.0f) * 0.5f * (float) (chart_w - 1));
    };
    {
      for (int i = 0; i < 5; ++i) {
        const float pos = -1.0f + 0.5f * (float) i;
        auto label = format_stops(pos * range);
        int lx = pos_to_x(pos) - text_width(label, fs) / 2;
        c.text(lx, y, label, fs, 30.0f);
      }
      y += glyph_h + pad / 2;
    }

    const int bottom_reserve = pad + (params.end_strips ? strip_h + pad : 0);
    const int avail = height - y - bottom_reserve;
    const int gap = std::max(4, height / 90);

    // Each band is three equal parts: lines, solid strip, lines.
    // Every part holds a whole number of line pairs so that the average of the lines is exact.
    int third = 0;
    for (;;) {
      int band_raw = avail / band_count - gap;
      third = (band_raw / 3 / (2 * line_px)) * (2 * line_px);
      if (third > 0 || line_px == 1) {
        break;
      }
      line_px /= 2;
    }
    if (third <= 0) {
      return px;
    }
    const int band_h = 3 * third;
    const int total = band_count * (band_h + gap) + gap;
    const int y_start = y + std::max(0, (avail - total) / 2);

    // The solid strip sweeps -range..+range stops across the chart width
    std::vector<float> sweep(chart_w);
    for (int i = 0; i < chart_w; ++i) {
      float pos = -1.0f + 2.0f * (float) i / (float) (chart_w - 1);
      sweep[i] = std::pow(2.0f, pos * range);
    }

    // 20 divisions across the chart, long ticks where the ruler has a number
    const auto draw_ticks = [&](int ty, float nits) {
      const int tick_w = std::max(1, fs);
      for (int i = -10; i <= 10; ++i) {
        int tx = pos_to_x((float) i / 10.0f) - tick_w / 2;
        int th = (i % 5 == 0) ? gap : gap / 2;
        c.fill(tx, ty + gap - th, tx + tick_w, ty + gap, nits);
      }
    };

    for (int k = 0; k < band_count; ++k) {
      const float ref = peak / std::pow(2.0f, (float) k);  // level of the bright lines
      const float target = ref * 0.5f;  // what the lines average to
      const float mark = std::clamp(ref, 4.0f, 40.0f);  // labels and ticks, kept dim near dark bands

      const int gy = y_start + k * (band_h + gap);
      const int by = gy + gap;
      draw_ticks(gy, mark);

      for (int r = 0; r < band_h; ++r) {
        const int yy = by + r;
        if (yy >= height) {
          break;
        }
        float *row = &px[(size_t) yy * width];
        if (r >= third && r < 2 * third) {
          for (int i = 0; i < chart_w; ++i) {
            row[x0 + i] = target * sweep[i];
          }
        } else if ((r / line_px) % 2 == 0) {
          std::fill(row + x0, row + x1, ref);
        }
      }

      // Left: centre value of the solid strip. Right: level of the reference lines.
      auto left = format_nits(target);
      auto right = format_nits(ref);
      int ly = by + (band_h - glyph_h) / 2;
      c.text(x0 - pad - text_width(left, fs), ly, left, fs, mark);
      c.text(x1 + pad, ly, right, fs, mark);
    }

    // Bottom strip: near-black steps, no labels so nothing bright sits next to them
    if (params.end_strips) {
      const int sy = height - pad - strip_h;
      for (int i = 0; i < step_count; ++i) {
        int px0 = x0 + i * slot_w + (slot_w - patch_w) / 2;
        c.fill(px0, sy, px0 + patch_w, sy + strip_h, black_steps[i]);
      }
    }

    return px;
  }
}  // namespace video::eotf_chart
