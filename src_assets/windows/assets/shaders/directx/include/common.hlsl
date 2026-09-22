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

// Smoothly shapes the shadow toe strictly below 100 nits (y_split = 0.50 in PQ)
// using smooth Hermite blending so midtones and highlights (>= 100 nits) are 100% untouched.
float ApplyHDRShadowToe(float y, float gamma)
{
    const float y_split = 0.50f; // 100 nits
    if (y >= y_split || gamma <= 0.0f || gamma == 1.0f) {
        return y;
    }
    float u = max(0.0f, y / y_split);
    float w = (1.0f - u) * (1.0f - u);
    float u_toe = pow(u, gamma);
    float u_smooth = lerp(u, u_toe, w);
    return u_smooth * y_split;
}

// Smoothly compresses highlights to target peak luminance (max_nits) with a smooth shoulder rolloff
float3 CompressHighlights(float3 rgb_nits, float max_nits)
{
    if (max_nits <= 0.0f) {
        return rgb_nits;
    }
    float shoulder = 0.65f * max_nits;
    float3 excess = max(0.0f, rgb_nits - shoulder);
    float scale = max_nits - shoulder;
    float3 compressed = shoulder + scale * (excess / (excess + scale));
    return (rgb_nits <= shoulder) ? rgb_nits : compressed;
}

float3 scRGBTo2100PQ(float3 rgb, float max_nits)
{
    // Convert from Rec 709 primaries (used by scRGB) to Rec 2020 primaries (used by Rec 2100)
    rgb = Rec709toRec2020(rgb);

    // 1.0f is defined as 80 nits in the scRGB colorspace
    rgb *= 80.0f;

    // Apply peak luminance tone-mapping if configured
    rgb = CompressHighlights(rgb, max_nits);

    // Apply the PQ transfer function on the raw color values in nits
    return NitsToPQ(rgb);
}
