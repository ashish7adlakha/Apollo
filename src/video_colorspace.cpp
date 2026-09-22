/**
 * @file src/video_colorspace.cpp
 * @brief Definitions for colorspace functions.
 */
// this include
#include "video_colorspace.h"

// local includes
#include "config.h"
#include "logging.h"
#include "video.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace video {
  using namespace std::literals;

  bool colorspace_is_hdr(const sunshine_colorspace_t &colorspace) {
    return colorspace.colorspace == colorspace_e::bt2020;
  }

  sunshine_colorspace_t colorspace_from_client_config(const config_t &config, bool hdr_display) {
    sunshine_colorspace_t colorspace;

    /* See video::config_t declaration for details */

    BOOST_LOG(info) << "Client dynamicRange: " << config.dynamicRange << ", Display is HDR: " << hdr_display;

    if (config.dynamicRange > 0 && hdr_display) {
      // Rec. 2020 with ST 2084 perceptual quantizer
      colorspace.colorspace = colorspace_e::bt2020;
    } else if (config::video.sdr_display_p3 || config::video.sdr_colorspace == "p3"sv || config::video.sdr_colorspace == "display_p3"sv) {
      colorspace.colorspace = colorspace_e::display_p3;
    } else if (config::video.sdr_colorspace == "rec709"sv) {
      colorspace.colorspace = colorspace_e::rec709;
    } else if (config::video.sdr_colorspace == "rec2020"sv) {
      colorspace.colorspace = colorspace_e::bt2020sdr;
    } else if (config::video.sdr_colorspace == "rec601"sv) {
      colorspace.colorspace = colorspace_e::rec601;
    } else {
      switch (config.encoderCscMode >> 1) {
        case 0:
          // Rec. 601
          colorspace.colorspace = colorspace_e::rec601;
          break;

        case 1:
          // Rec. 709
          colorspace.colorspace = colorspace_e::rec709;
          break;

        case 2:
          // Rec. 2020
          colorspace.colorspace = colorspace_e::bt2020sdr;
          break;

        default:
          BOOST_LOG(error) << "Unknown video colorspace in csc, falling back to Rec. 709";
          colorspace.colorspace = colorspace_e::rec709;
          break;
      }
    }

    colorspace.full_range = (config.encoderCscMode & 0x1);
    colorspace.legal_remap = false;
    colorspace.black_lift = 0;

    if (colorspace_is_hdr(colorspace)) {
      colorspace.black_lift = config::video.hdr_black_lift;

      if (config::video.hdr_color_range == "limited"sv) {
        BOOST_LOG(info) << "HDR Color Range: Forcing Limited Range (64-940)";
        colorspace.full_range = false;
      } else if (config::video.hdr_color_range == "full"sv) {
        BOOST_LOG(info) << "HDR Color Range: Forcing Full Range (0-1023)";
        colorspace.full_range = true;
      } else if (config::video.hdr_color_range == "remap"sv) {
        BOOST_LOG(info) << "HDR Color Range: HDMI Legal Range Remapping enabled (anti-clipping)";
        colorspace.full_range = false;
        colorspace.legal_remap = true;
        if (config::video.hdr_shadow_gamma > 0.1f) {
          colorspace.hdr_shadow_gamma = config::video.hdr_shadow_gamma;
          BOOST_LOG(info) << "HDR Shadow Curve Gamma: " << colorspace.hdr_shadow_gamma;
        }
      } else {
        // "auto" or legacy fallback
        if (config::video.hdr_limited_range) {
          BOOST_LOG(info) << "HDR Color Range: Forcing limited color range (legacy setting)";
          colorspace.full_range = false;
        } else {
          BOOST_LOG(info) << "HDR Color Range: Auto (Client requested: " << (colorspace.full_range ? "Full" : "Limited") << ")";
        }
      }
      if (colorspace.black_lift != 0) {
        BOOST_LOG(info) << "HDR Black Level Lift: " << (colorspace.black_lift > 0 ? "+" : "") << colorspace.black_lift;
      }
    } else {
      // SDR Gamma Calibration (madVR-style)
      if (config::video.sdr_display_gamma > 0.1f && config::video.sdr_target_gamma > 0.1f) {
        colorspace.sdr_gamma_power = config::video.sdr_target_gamma / config::video.sdr_display_gamma;
        if (std::abs(colorspace.sdr_gamma_power - 1.0f) > 0.001f) {
          BOOST_LOG(info) << "SDR Gamma Calibration: Display Gamma " << config::video.sdr_display_gamma
                          << " -> Target Gamma " << config::video.sdr_target_gamma
                          << " (correction power: " << colorspace.sdr_gamma_power << ")";
        }
      }
    }

    switch (config.dynamicRange) {
      case 0:
        colorspace.bit_depth = 8;
        break;

      case 1:
        colorspace.bit_depth = 10;
        break;

      default:
        BOOST_LOG(error) << "Unknown dynamicRange value, falling back to 10-bit color depth";
        colorspace.bit_depth = 10;
        break;
    }

    if (colorspace.colorspace == colorspace_e::bt2020sdr && colorspace.bit_depth != 10) {
      BOOST_LOG(error) << "BT.2020 SDR colorspace expects 10-bit color depth, falling back to Rec. 709";
      colorspace.colorspace = colorspace_e::rec709;
    }

    return colorspace;
  }

  avcodec_colorspace_t avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace) {
    avcodec_colorspace_t avcodec_colorspace;

    switch (sunshine_colorspace.colorspace) {
      case colorspace_e::rec601:
        // Rec. 601
        avcodec_colorspace.primaries = AVCOL_PRI_SMPTE170M;
        avcodec_colorspace.transfer_function = AVCOL_TRC_SMPTE170M;
        avcodec_colorspace.matrix = AVCOL_SPC_SMPTE170M;
        avcodec_colorspace.software_format = SWS_CS_SMPTE170M;
        break;

      case colorspace_e::rec709:
        // Rec. 709
        avcodec_colorspace.primaries = AVCOL_PRI_BT709;
        avcodec_colorspace.transfer_function = AVCOL_TRC_BT709;
        avcodec_colorspace.matrix = AVCOL_SPC_BT709;
        avcodec_colorspace.software_format = SWS_CS_ITU709;
        break;

      case colorspace_e::display_p3:
        // Clamped to Display P3 coordinates, signaled as BT.709 so Android and display don't apply an extra gamut boost
        avcodec_colorspace.primaries = AVCOL_PRI_BT709;
        avcodec_colorspace.transfer_function = AVCOL_TRC_BT709;
        avcodec_colorspace.matrix = AVCOL_SPC_BT709;
        avcodec_colorspace.software_format = SWS_CS_ITU709;
        break;

      case colorspace_e::bt2020sdr:
        // Rec. 2020
        avcodec_colorspace.primaries = AVCOL_PRI_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        avcodec_colorspace.transfer_function = AVCOL_TRC_BT2020_10;
        avcodec_colorspace.matrix = AVCOL_SPC_BT2020_NCL;
        avcodec_colorspace.software_format = SWS_CS_BT2020;
        break;

      case colorspace_e::bt2020:
        // Rec. 2020 with ST 2084 perceptual quantizer
        avcodec_colorspace.primaries = AVCOL_PRI_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        avcodec_colorspace.transfer_function = AVCOL_TRC_SMPTE2084;
        avcodec_colorspace.matrix = AVCOL_SPC_BT2020_NCL;
        avcodec_colorspace.software_format = SWS_CS_BT2020;
        break;
    }

    avcodec_colorspace.range = sunshine_colorspace.full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    return avcodec_colorspace;
  }

  const color_t *color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace) {
    if (colorspace.legal_remap || colorspace.black_lift != 0 || colorspace.sdr_gamma_power != 1.0f || colorspace.hdr_shadow_gamma != 1.0f) {
      using float2 = float[2];
      // sRGB to Display P3 (D65) transformation matrix coefficients.
      // Applied when colorspace is display_p3 to preserve the sRGB-clamp behavior
      // even when gamma correction is active.
      constexpr float m00 = 0.822462f, m01 = 0.177538f, m02 = 0.000000f;
      constexpr float m10 = 0.033194f, m11 = 0.966806f, m12 = 0.000000f;
      constexpr float m20 = 0.017083f, m21 = 0.072397f, m22 = 0.910520f;
      bool apply_p3 = (colorspace.colorspace == colorspace_e::display_p3);

      auto make_custom_matrix = [&](float Cr, float Cb, const float2 &range_Y, const float2 &range_UV) -> color_t {
        float Cg = 1.0f - Cr - Cb;

        float Cr_i = 1.0f - Cr;
        float Cb_i = 1.0f - Cb;

        float shift_y = range_Y[0] / 255.0f;
        float shift_uv = range_UV[0] / 255.0f;

        float scale_y = (range_Y[1] - range_Y[0]) / 255.0f;
        float scale_uv = (range_UV[1] - range_UV[0]) / 255.0f;

        float y0 = Cr, y1 = Cg, y2 = Cb;
        float u0 = -(Cr * 0.5f / Cb_i), u1 = -(Cg * 0.5f / Cb_i), u2 = 0.5f;
        float v0 = 0.5f, v1 = -(Cg * 0.5f / Cr_i), v2 = -(Cb * 0.5f / Cr_i);

        if (apply_p3) {
          // Transform color vectors into Display P3 space for sRGB-clamp
          float y0p = y0 * m00 + y1 * m10 + y2 * m20;
          float y1p = y0 * m01 + y1 * m11 + y2 * m21;
          float y2p = y0 * m02 + y1 * m12 + y2 * m22;
          y0 = y0p; y1 = y1p; y2 = y2p;

          float u0p = u0 * m00 + u1 * m10 + u2 * m20;
          float u1p = u0 * m01 + u1 * m11 + u2 * m21;
          float u2p = u0 * m02 + u1 * m12 + u2 * m22;
          u0 = u0p; u1 = u1p; u2 = u2p;

          float v0p = v0 * m00 + v1 * m10 + v2 * m20;
          float v1p = v0 * m01 + v1 * m11 + v2 * m21;
          float v2p = v0 * m02 + v1 * m12 + v2 * m22;
          v0 = v0p; v1 = v1p; v2 = v2p;
        }

        return {
          {y0, y1, y2, 0.0f},
          {u0, u1, u2, 0.5f},
          {v0, v1, v2, 0.5f},
          {scale_y, shift_y},
          {scale_uv, shift_uv},
          {colorspace.sdr_gamma_power, colorspace.hdr_shadow_gamma, 0.0f, 0.0f},
        };
      };

      float Cr = 0.2627f, Cb = 0.0593f;
      if (colorspace.colorspace == colorspace_e::rec709 || colorspace.colorspace == colorspace_e::display_p3) {
        Cr = 0.2126f;
        Cb = 0.0722f;
      } else if (colorspace.colorspace == colorspace_e::rec601) {
        Cr = 0.299f;
        Cb = 0.114f;
      }

      float y_min, y_max;
      float uv_min, uv_max;

      if (colorspace.legal_remap) {
        // Baseline 29.75 (119 in 10-bit), adjustable via black_lift (-30 to +30).
        // Negative black_lift lowers y_min towards standard limited range 16.0 (64 in 10-bit),
        // allowing fine-tuning to find the exact hardware crush threshold.
        y_min = std::clamp(29.75f + ((float)colorspace.black_lift * 0.5f), 16.0f, 64.0f);
        y_max = 217.25f;

        float scale_y = (y_max - y_min) / 255.0f;
        float scale_uv = scale_y * (224.0f / 219.0f);
        float shift_uv = (128.0f / 255.0f) - 0.5f * scale_uv;
        uv_min = shift_uv * 255.0f;
        uv_max = uv_min + scale_uv * 255.0f;
      } else {
        // Standard range with optional black lift
        float base_y_min = colorspace.full_range ? 0.0f : 16.0f;
        float base_y_max = colorspace.full_range ? 255.0f : 235.0f;
        y_min = std::clamp(base_y_min + ((float)colorspace.black_lift * 0.5f), 0.0f, 64.0f);
        y_max = base_y_max;
        uv_min = colorspace.full_range ? 0.0f : 16.0f;
        uv_max = colorspace.full_range ? 255.0f : 240.0f;
      }

      static thread_local color_t custom_color;
      custom_color = make_custom_matrix(Cr, Cb, {y_min, y_max}, {uv_min, uv_max});
      return &custom_color;
    }

    return color_vectors_from_colorspace(colorspace.colorspace, colorspace.full_range);
  }

  const color_t *color_vectors_from_colorspace(colorspace_e colorspace, bool full_range) {
    using float2 = float[2];
    auto make_color_matrix = [](float Cr, float Cb, const float2 &range_Y, const float2 &range_UV) -> color_t {
      float Cg = 1.0f - Cr - Cb;

      float Cr_i = 1.0f - Cr;
      float Cb_i = 1.0f - Cb;

      float shift_y = range_Y[0] / 255.0f;
      float shift_uv = range_UV[0] / 255.0f;

      float scale_y = (range_Y[1] - range_Y[0]) / 255.0f;
      float scale_uv = (range_UV[1] - range_UV[0]) / 255.0f;
      return {
        {Cr, Cg, Cb, 0.0f},
        {-(Cr * 0.5f / Cb_i), -(Cg * 0.5f / Cb_i), 0.5f, 0.5f},
        {0.5f, -(Cg * 0.5f / Cr_i), -(Cb * 0.5f / Cr_i), 0.5f},
        {scale_y, shift_y},
        {scale_uv, shift_uv},
        {1.0f, 1.0f, 0.0f, 0.0f},
      };
    };

    auto make_clamped_color_matrix = [](float Cr, float Cb, const float2 &range_Y, const float2 &range_UV) -> color_t {
      float Cg = 1.0f - Cr - Cb;

      float Cr_i = 1.0f - Cr;
      float Cb_i = 1.0f - Cb;

      float shift_y = range_Y[0] / 255.0f;
      float shift_uv = range_UV[0] / 255.0f;

      float scale_y = (range_Y[1] - range_Y[0]) / 255.0f;
      float scale_uv = (range_UV[1] - range_UV[0]) / 255.0f;

      // sRGB to Display P3 (D65) transformation matrix coefficients
      constexpr float m00 = 0.822462f, m01 = 0.177538f, m02 = 0.000000f;
      constexpr float m10 = 0.033194f, m11 = 0.966806f, m12 = 0.000000f;
      constexpr float m20 = 0.017083f, m21 = 0.072397f, m22 = 0.910520f;

      float y0 = Cr, y1 = Cg, y2 = Cb;
      float y_p3[3] = {
        y0 * m00 + y1 * m10 + y2 * m20,
        y0 * m01 + y1 * m11 + y2 * m21,
        y0 * m02 + y1 * m12 + y2 * m22
      };

      float u0 = -(Cr * 0.5f / Cb_i), u1 = -(Cg * 0.5f / Cb_i), u2 = 0.5f;
      float u_p3[3] = {
        u0 * m00 + u1 * m10 + u2 * m20,
        u0 * m01 + u1 * m11 + u2 * m21,
        u0 * m02 + u1 * m12 + u2 * m22
      };

      float v0 = 0.5f, v1 = -(Cg * 0.5f / Cr_i), v2 = -(Cb * 0.5f / Cr_i);
      float v_p3[3] = {
        v0 * m00 + v1 * m10 + v2 * m20,
        v0 * m01 + v1 * m11 + v2 * m21,
        v0 * m02 + v1 * m12 + v2 * m22
      };

      return {
        {y_p3[0], y_p3[1], y_p3[2], 0.0f},
        {u_p3[0], u_p3[1], u_p3[2], 0.5f},
        {v_p3[0], v_p3[1], v_p3[2], 0.5f},
        {scale_y, shift_y},
        {scale_uv, shift_uv},
        {1.0f, 1.0f, 0.0f, 0.0f},
      };
    };

    static const color_t colors[] {
      make_color_matrix(0.299f, 0.114f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT601 MPEG
      make_color_matrix(0.299f, 0.114f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT601 JPEG
      make_color_matrix(0.2126f, 0.0722f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT709 MPEG
      make_color_matrix(0.2126f, 0.0722f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT709 JPEG
      make_color_matrix(0.2627f, 0.0593f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT2020 MPEG
      make_color_matrix(0.2627f, 0.0593f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT2020 JPEG
      make_clamped_color_matrix(0.2126f, 0.0722f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // P3 Clamped MPEG
      make_clamped_color_matrix(0.2126f, 0.0722f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // P3 Clamped JPEG
    };

    const color_t *result = nullptr;

    switch (colorspace) {
      case colorspace_e::rec601:
      default:
        result = &colors[0];
        break;
      case colorspace_e::rec709:
        result = &colors[2];
        break;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        result = &colors[4];
        break;
      case colorspace_e::display_p3:
        result = &colors[6];
        break;
    };

    if (full_range) {
      result++;
    }

    return result;
  }

  const color_t *new_color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace) {
    constexpr auto generate_color_vectors = [](const sunshine_colorspace_t &colorspace) -> color_t {
      double Kr, Kb;
      switch (colorspace.colorspace) {
        case colorspace_e::rec601:
          Kr = 0.299;
          Kb = 0.114;
          break;
        case colorspace_e::rec709:
        case colorspace_e::display_p3:
        default:
          Kr = 0.2126;
          Kb = 0.0722;
          break;
        case colorspace_e::bt2020:
        case colorspace_e::bt2020sdr:
          Kr = 0.2627;
          Kb = 0.0593;
          break;
      }
      double Kg = 1.0 - Kr - Kb;

      double y_mult, y_add;
      double uv_mult, uv_add;

      // "Matrix coefficients" section of ITU-T H.273
      if (colorspace.full_range) {
        y_mult = (1 << colorspace.bit_depth) - 1;
        y_add = 0;
        uv_mult = (1 << colorspace.bit_depth) - 1;
        uv_add = (1 << (colorspace.bit_depth - 1));
      } else if (colorspace.legal_remap) {
        double shift_y_val = (29.75 + colorspace.black_lift * 0.5) / 255.0;
        double scale_y_val = (217.25 - 29.75 - colorspace.black_lift * 0.5) / 255.0;
        y_mult = scale_y_val * ((1 << colorspace.bit_depth) - 1);
        y_add = shift_y_val * ((1 << colorspace.bit_depth) - 1);
        double scale_uv_val = scale_y_val * (224.0 / 219.0);
        double shift_uv_val = (128.0 / 255.0) - 0.5 * scale_uv_val;
        uv_mult = scale_uv_val * ((1 << colorspace.bit_depth) - 1);
        uv_add = shift_uv_val * ((1 << colorspace.bit_depth) - 1);
      } else {
        double y_min = 16.0 + (colorspace.black_lift * 0.5);
        y_mult = (1 << (colorspace.bit_depth - 8)) * (235.0 - y_min);
        y_add = (1 << (colorspace.bit_depth - 8)) * y_min;
        uv_mult = (1 << (colorspace.bit_depth - 8)) * 224;
        uv_add = (1 << (colorspace.bit_depth - 8)) * 128;
      }

      // For rounding
      y_add += 0.5;
      uv_add += 0.5;

      color_t color_vectors;

      color_vectors.color_vec_y[0] = Kr * y_mult;
      color_vectors.color_vec_y[1] = Kg * y_mult;
      color_vectors.color_vec_y[2] = Kb * y_mult;
      color_vectors.color_vec_y[3] = y_add;

      color_vectors.color_vec_u[0] = -0.5 * Kr / (1.0 - Kb) * uv_mult;
      color_vectors.color_vec_u[1] = -0.5 * Kg / (1.0 - Kb) * uv_mult;
      color_vectors.color_vec_u[2] = 0.5 * uv_mult;
      color_vectors.color_vec_u[3] = uv_add;

      color_vectors.color_vec_v[0] = 0.5 * uv_mult;
      color_vectors.color_vec_v[1] = -0.5 * Kg / (1.0 - Kr) * uv_mult;
      color_vectors.color_vec_v[2] = -0.5 * Kb / (1.0 - Kr) * uv_mult;
      color_vectors.color_vec_v[3] = uv_add;

      if (colorspace.colorspace == colorspace_e::display_p3) {
        // sRGB to Display P3 (D65) transformation matrix coefficients
        constexpr double m00 = 0.822462, m01 = 0.177538, m02 = 0.000000;
        constexpr double m10 = 0.033194, m11 = 0.966806, m12 = 0.000000;
        constexpr double m20 = 0.017083, m21 = 0.072397, m22 = 0.910520;

        double y0 = color_vectors.color_vec_y[0];
        double y1 = color_vectors.color_vec_y[1];
        double y2 = color_vectors.color_vec_y[2];
        color_vectors.color_vec_y[0] = (float)(y0 * m00 + y1 * m10 + y2 * m20);
        color_vectors.color_vec_y[1] = (float)(y0 * m01 + y1 * m11 + y2 * m21);
        color_vectors.color_vec_y[2] = (float)(y0 * m02 + y1 * m12 + y2 * m22);

        double u0 = color_vectors.color_vec_u[0];
        double u1 = color_vectors.color_vec_u[1];
        double u2 = color_vectors.color_vec_u[2];
        color_vectors.color_vec_u[0] = (float)(u0 * m00 + u1 * m10 + u2 * m20);
        color_vectors.color_vec_u[1] = (float)(u0 * m01 + u1 * m11 + u2 * m21);
        color_vectors.color_vec_u[2] = (float)(u0 * m02 + u1 * m12 + u2 * m22);

        double v0 = color_vectors.color_vec_v[0];
        double v1 = color_vectors.color_vec_v[1];
        double v2 = color_vectors.color_vec_v[2];
        color_vectors.color_vec_v[0] = (float)(v0 * m00 + v1 * m10 + v2 * m20);
        color_vectors.color_vec_v[1] = (float)(v0 * m01 + v1 * m11 + v2 * m21);
        color_vectors.color_vec_v[2] = (float)(v0 * m02 + v1 * m12 + v2 * m22);
      }

      // Unused
      color_vectors.range_y[0] = 1;
      color_vectors.range_y[1] = 0;
      color_vectors.range_uv[0] = 1;
      color_vectors.range_uv[1] = 0;
      color_vectors.gamma_params[0] = colorspace.sdr_gamma_power;
      color_vectors.gamma_params[1] = colorspace.hdr_shadow_gamma;
      color_vectors.gamma_params[2] = 0.0f;
      color_vectors.gamma_params[3] = 0.0f;

      return color_vectors;
    };

    if (colorspace.legal_remap || colorspace.black_lift != 0 || colorspace.sdr_gamma_power != 1.0f || colorspace.hdr_shadow_gamma != 1.0f) {
      static thread_local color_t custom_color;
      custom_color = generate_color_vectors(colorspace);
      return &custom_color;
    }

    static constexpr color_t colors[] = {
      generate_color_vectors({colorspace_e::rec601, false, 8}),
      generate_color_vectors({colorspace_e::rec601, true, 8}),
      generate_color_vectors({colorspace_e::rec601, false, 10}),
      generate_color_vectors({colorspace_e::rec601, true, 10}),
      generate_color_vectors({colorspace_e::rec709, false, 8}),
      generate_color_vectors({colorspace_e::rec709, true, 8}),
      generate_color_vectors({colorspace_e::rec709, false, 10}),
      generate_color_vectors({colorspace_e::rec709, true, 10}),
      generate_color_vectors({colorspace_e::bt2020, false, 8}),
      generate_color_vectors({colorspace_e::bt2020, true, 8}),
      generate_color_vectors({colorspace_e::bt2020, false, 10}),
      generate_color_vectors({colorspace_e::bt2020, true, 10}),
      generate_color_vectors({colorspace_e::display_p3, false, 8}),
      generate_color_vectors({colorspace_e::display_p3, true, 8}),
      generate_color_vectors({colorspace_e::display_p3, false, 10}),
      generate_color_vectors({colorspace_e::display_p3, true, 10}),
    };

    const color_t *result = nullptr;

    switch (colorspace.colorspace) {
      case colorspace_e::rec601:
        result = &colors[0];
        break;
      case colorspace_e::rec709:
      default:
        result = &colors[4];
        break;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        result = &colors[8];
        break;
      case colorspace_e::display_p3:
        result = &colors[12];
        break;
    }

    if (colorspace.bit_depth == 10) {
      result += 2;
    }
    if (colorspace.full_range) {
      result += 1;
    }

    return result;
  }
}  // namespace video
