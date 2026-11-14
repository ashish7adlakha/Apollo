/**
 * @file src/video_colorspace.h
 * @brief Declarations for colorspace functions.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace video {

  enum class colorspace_e {
    rec601,  ///< Rec. 601
    rec709,  ///< Rec. 709
    display_p3,  ///< Display P3 (SMPTE 432 / D65)
    bt2020sdr,  ///< Rec. 2020 SDR
    bt2020,  ///< Rec. 2020 HDR
  };

  struct sunshine_colorspace_t {
    colorspace_e colorspace;
    bool full_range;
    unsigned bit_depth;
    bool legal_remap = false;
    int black_lift = 0;
    float sdr_gamma_power = 1.0f;
  };

  bool colorspace_is_hdr(const sunshine_colorspace_t &colorspace);

  // Declared in video.h
  struct config_t;

  sunshine_colorspace_t colorspace_from_client_config(const config_t &config, bool hdr_display);

  struct avcodec_colorspace_t {
    AVColorPrimaries primaries;
    AVColorTransferCharacteristic transfer_function;
    AVColorSpace matrix;
    AVColorRange range;
    int software_format;
  };

  avcodec_colorspace_t avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace);

  struct alignas(16) color_t {
    float color_vec_y[4];
    float color_vec_u[4];
    float color_vec_v[4];
    float range_y[2];
    float range_uv[2];
    float gamma_params[4];
  };

  const color_t *color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace);

  const color_t *color_vectors_from_colorspace(colorspace_e colorspace, bool full_range);

  /**
   * @brief New version of `color_vectors_from_colorspace()` function that better adheres to the standards.
   *        Returned vectors are used to perform RGB->YUV conversion.
   *        Unlike its predecessor, color vectors will produce output in `UINT` range, not `UNORM` range.
   *        Input is still in `UNORM` range. Returned vectors won't modify color primaries and color
   *        transfer function.
   * @param colorspace Targeted YUV colorspace.
   * @return `const color_t*` that contains RGB->YUV transformation vectors.
   *         Components `range_y` and `range_uv` are there for backwards compatibility
   *         and can be ignored in the computation.
   */
  const color_t *new_color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace);

  struct hdr_calibration_point_t {
    float nits = 0.0f;
    float r_mult = 1.0f;  ///< Red multiplier (1.0 = neutral)
    float g_mult = 1.0f;  ///< Green multiplier (1.0 = neutral)
    float b_mult = 1.0f;  ///< Blue multiplier (1.0 = neutral)
  };

  /**
   * @brief One EOTF gain point: a brightness multiplier (R=G=B) at a luminance level.
   */
  struct eotf_point_t {
    float nits = 0.0f;
    float gain = 1.0f;  ///< Luminance multiplier (1.0 = neutral)
  };

  struct alignas(16) hdr_calibration_cbuffer_t {
    float flags[4];  ///< x: color calibration enabled (1.0 or 0.0), y: count (up to 16), z: shadow gamma (1.0 = off), w: PQ value of the shadow knee
    float points[16][4];  ///< each is {pq, r_mult, g_mult, b_mult}
    float eotf_flags[4];  ///< x: EOTF gain enabled (1.0 or 0.0), y: count (up to 16), z: shadow slope (1.0 = off), w: 0
    float eotf_points[16][4];  ///< each is {pq, gain, 0, 0}
  };

  enum class test_pattern_e {
    patch,  ///< Flat grey patch at a chosen luminance (any input hides it)
    eotf_chart,  ///< EOTF null chart (only Esc or the web UI hides it)
  };

  struct active_calibration_t {
    mutable std::mutex mtx;
    bool enabled = false;
    std::vector<hdr_calibration_point_t> points;

    // EOTF (brightness curve) correction
    bool eotf_enabled = false;
    std::vector<eotf_point_t> eotf_points;
    float shadow_gamma = 1.0f;
    float shadow_slope = 1.0f;
    float shadow_knee_nits = 10000.0f;

    // In-stream test pattern
    bool test_pattern_active = false;
    test_pattern_e test_pattern_type = test_pattern_e::patch;
    float test_pattern_nits = 10.0f;
    bool test_pattern_fullscreen = false;
    std::chrono::steady_clock::time_point test_pattern_activated_at;

    // EOTF chart options
    float chart_peak_nits = 1000.0f;
    int chart_line_px = 2;
    bool chart_end_strips = true;
    float chart_range_stops = 1.0f;
    int chart_bands = 12;

    std::atomic<uint32_t> version {0};
  };

  float nits_to_pq(float nits);
  std::vector<hdr_calibration_point_t> default_hdr_calibration_points();
  /**
   * @brief Build the shader constant buffer from the live calibration state.
   * @note The caller must hold `state.mtx`.
   */
  hdr_calibration_cbuffer_t build_hdr_calibration_cbuffer(const active_calibration_t &state);
  std::string serialize_hdr_calibration(const std::vector<hdr_calibration_point_t> &points);
  std::vector<hdr_calibration_point_t> deserialize_hdr_calibration(const std::string &json_str);
  std::string serialize_eotf_points(const std::vector<eotf_point_t> &points);
  std::vector<eotf_point_t> deserialize_eotf_points(const std::string &json_str);
  std::vector<eotf_point_t> eotf_points_from_json(const nlohmann::json &j);

  extern active_calibration_t active_hdr_calibration;

  /**
   * @brief Hide the in-stream test pattern because of user input.
   * @param is_escape True when the input was the Esc key. The EOTF chart only reacts to Esc.
   */
  void dismiss_hdr_test_pattern(bool is_escape = false);
}  // namespace video
