/**
 * @file src/video_colorspace.h
 * @brief Declarations for colorspace functions.
 */
#pragma once

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
    float hdr_shadow_gamma = 1.0f;
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
}  // namespace video
