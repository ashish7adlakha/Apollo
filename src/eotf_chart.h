/**
 * @file src/eotf_chart.h
 * @brief Declarations for the EOTF null chart test pattern.
 */
#pragma once

#include <vector>

namespace video::eotf_chart {
  /// Most bands the chart can have. The stream correction has 16 points: the anchor plus one per band.
  constexpr int max_band_count = 15;

  struct params_t {
    int bands = 12;  ///< Number of brightness bands (4 to 15). Band k uses the reference level peak / 2^k.
    float peak_nits = 1000.0f;  ///< Reference level of the top band
    int line_px = 2;  ///< Thickness of the alternating reference lines in pixels
    bool end_strips = true;  ///< Draw the highlight (top) and near-black (bottom) step strips
    float range_stops = 1.0f;  ///< The solid strip sweeps from -range to +range stops (smaller = finer reading)
  };

  /**
   * @brief Render the EOTF null chart.
   *
   * Every band has a reference made of alternating lines of 0 and L nits. Seen from a distance
   * this is exactly half of the light the display emits for L, whatever its EOTF is.
   * A solid strip inside the band sweeps from -range (left) to +range stops (right) around L / 2.
   * The strip blends into the reference where the display tracks the curve: at the centre
   * if the EOTF is followed, left or right of it if not. The reading of a band is the error
   * of that one halving step (L to L / 2), so errors add up from the top band down.
   *
   * The image must be shown pixel for pixel (no scaling), otherwise the lines are averaged
   * in the encoded domain and the result is wrong.
   *
   * @param width Width in pixels.
   * @param height Height in pixels.
   * @param params Chart options.
   * @return Luminance in nits for each pixel, row by row (`width * height` values).
   */
  std::vector<float> render(int width, int height, const params_t &params);
}  // namespace video::eotf_chart
