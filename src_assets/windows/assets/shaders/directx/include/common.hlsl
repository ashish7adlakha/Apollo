// This is a fast sRGB approximation from Microsoft's ColorSpaceUtility.hlsli
float3 ApplySRGBCurve(float3 x)
{
    return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
}

float3 NitsToPQ(float3 L)
{
    // Constants from SMPTE 2084 PQ
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;

    float3 Lp = pow(saturate(L / 10000.0), m1);
    return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 Rec709toRec2020(float3 rec709)
{
    static const float3x3 ConvMat =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(ConvMat, rec709);
}

#if defined(PERCEPTUAL_QUANTIZER)
cbuffer hdr_calibration_cbuffer : register(b2) {
    float4 hdr_calib_flags;       // x: color calibration enabled (1.0/0.0), y: count (up to 16), z: shadow gamma (1.0 = off), w: PQ value of the shadow knee
    float4 hdr_calib_points[16];  // each is {pq, mult_r, mult_g, mult_b}
    float4 hdr_eotf_flags;        // x: EOTF gain enabled (1.0/0.0), y: count (up to 16), z: shadow slope (1.0 = off), w: 0
    float4 hdr_eotf_points[16];   // each is {pq, gain, 0, 0}
};

// Scalar PQ encode: luminance in nits -> PQ signal (0..1)
float LuminanceToPQ(float L)
{
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;

    float Lp = pow(saturate(L / 10000.0f), m1);
    return pow((c1 + c2 * Lp) / (1.0f + c3 * Lp), m2);
}

// Scalar PQ decode: PQ signal (0..1) -> luminance in nits
float PQToLuminance(float P)
{
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;

    float Pp = pow(saturate(P), 1.0f / m2);
    float num = max(Pp - c1, 0.0f);
    float den = max(c2 - c3 * Pp, 0.000001f);
    return 10000.0f * pow(num / den, 1.0f / m1);
}

// Piecewise-linear lookup (in PQ space) through a sorted list of points.
// Returns the interpolated .yzw of the points; .x of each point is its PQ position.
float3 LookupCalibrationPoints(float4 pts[16], int num_pts, float P)
{
    float3 result = float3(1.0f, 1.0f, 1.0f);

    if (num_pts > 0) {
        if (P <= pts[0].x) {
            result = pts[0].yzw;
        } else if (P >= pts[num_pts - 1].x) {
            result = pts[num_pts - 1].yzw;
        } else {
            [unroll]
            for (int i = 0; i < 15; i++) {
                if (i < num_pts - 1 && P >= pts[i].x && P < pts[i + 1].x) {
                    float span = pts[i + 1].x - pts[i].x;
                    float t = (span > 0.0001f) ? saturate((P - pts[i].x) / span) : 0.0f;
                    result = lerp(pts[i].yzw, pts[i + 1].yzw, t);
                    break;
                }
            }
        }
    }
    return result;
}

float Rec2020Luminance(float3 nits)
{
    return dot(nits, float3(0.2627f, 0.6780f, 0.0593f));
}

float3 ApplyHDRCalibration(float3 nits)
{
    // 1. EOTF gain points: brightness multiplier per luminance level (R=G=B)
    if (hdr_eotf_flags.x > 0.5f) {
        float P = LuminanceToPQ(Rec2020Luminance(nits));
        float gain = LookupCalibrationPoints(hdr_eotf_points, int(hdr_eotf_flags.y), P).x;
        nits *= gain;
    }

    // 2. Shadow curve: PQ' = PQk * (PQ / PQk) ^ gamma below the knee, unchanged above it.
    //    Applied to luminance and RGB is scaled by the same ratio, so saturation is kept.
    //    Shadow slope: L' = Lk * (L / Lk) ^ slope below the knee, in light (nits).
    //    A slope of 2 makes every halving step below the knee twice as large (in stops).
    //    Both are applied to luminance and RGB is scaled by the same ratio.
    float shadow_gamma = hdr_calib_flags.z;
    float knee_pq = hdr_calib_flags.w;
    float shadow_slope = hdr_eotf_flags.z;
    bool has_gamma = shadow_gamma > 0.0f && abs(shadow_gamma - 1.0f) > 0.0005f;
    bool has_slope = shadow_slope > 0.0f && abs(shadow_slope - 1.0f) > 0.0005f;
    if ((has_gamma || has_slope) && knee_pq > 0.0f) {
        float L = Rec2020Luminance(nits);
        if (L > 0.000001f && LuminanceToPQ(L) < knee_pq) {
            float L2 = L;
            if (has_slope) {
                float knee_nits = max(PQToLuminance(knee_pq), 0.000001f);
                L2 = knee_nits * pow(max(L2 / knee_nits, 0.0f), shadow_slope);
            }
            if (has_gamma) {
                float P2 = knee_pq * pow(max(LuminanceToPQ(L2) / knee_pq, 0.0f), shadow_gamma);
                L2 = PQToLuminance(P2);
            }
            nits *= L2 / L;
        }
    }

    // 3. Multi-point color calibration: R/G/B multipliers per luminance level
    if (hdr_calib_flags.x > 0.5f) {
        float P = LuminanceToPQ(Rec2020Luminance(nits));
        float3 mult = LookupCalibrationPoints(hdr_calib_points, int(hdr_calib_flags.y), P);
        nits = max(0.0f, nits * mult);
    }
    return nits;
}
#endif

float3 scRGBTo2100PQ(float3 rgb)
{
    // Convert from Rec 709 primaries (used by scRGB) to Rec 2020 primaries (used by Rec 2100)
    rgb = Rec709toRec2020(rgb);

    // 1.0f is defined as 80 nits in the scRGB colorspace
    rgb *= 80;

#if defined(PERCEPTUAL_QUANTIZER)
    rgb = ApplyHDRCalibration(rgb);
#endif

    // Apply the PQ transfer function on the raw color values in nits
    return NitsToPQ(rgb);
}
