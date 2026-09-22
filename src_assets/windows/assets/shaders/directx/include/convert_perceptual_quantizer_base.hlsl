#include "include/common.hlsl"

#define CONVERT_FUNCTION(rgb) scRGBTo2100PQ(rgb, gamma_params.z)
#define PERCEPTUAL_QUANTIZER 1
