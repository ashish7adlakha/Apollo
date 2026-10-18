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
cbuffer hdr_calibration_cbuffer : register(b1) {
    float4 hdr_calib_flags;       // x: enabled (1.0/0.0), y: count (up to 16), z: 0, w: 0
    float4 hdr_calib_points[16];  // each is {pq, delta_r, delta_g, delta_b}
};

float3 ApplyHDRCalibration(float3 nits)
{
    if (hdr_calib_flags.x > 0.5f) {
        float L = dot(nits, float3(0.2627f, 0.6780f, 0.0593f));
        static const float m1 = 2610.0 / 4096.0 / 4;
        static const float m2 = 2523.0 / 4096.0 * 128;
        static const float c1 = 3424.0 / 4096.0;
        static const float c2 = 2413.0 / 4096.0 * 32;
        static const float c3 = 2392.0 / 4096.0 * 32;

        float Lp = pow(saturate(L / 10000.0f), m1);
        float P = pow((c1 + c2 * Lp) / (1.0f + c3 * Lp), m2);

        int num_pts = int(hdr_calib_flags.y);
        float3 delta = float3(0.0f, 0.0f, 0.0f);

        if (num_pts > 0) {
            if (P <= hdr_calib_points[0].x) {
                delta = hdr_calib_points[0].yzw;
            } else if (P >= hdr_calib_points[num_pts - 1].x) {
                delta = hdr_calib_points[num_pts - 1].yzw;
            } else {
                [unroll]
                for (int i = 0; i < 15; i++) {
                    if (i < num_pts - 1 && P >= hdr_calib_points[i].x && P < hdr_calib_points[i + 1].x) {
                        float span = hdr_calib_points[i + 1].x - hdr_calib_points[i].x;
                        float t = (span > 0.0001f) ? ((P - hdr_calib_points[i].x) / span) : 0.0f;
                        delta = lerp(hdr_calib_points[i].yzw, hdr_calib_points[i + 1].yzw, t);
                        break;
                    }
                }
            }
        }
        nits = max(0.0f, nits * (1.0f + delta));
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
