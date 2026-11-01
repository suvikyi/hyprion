#pragma once

namespace HyprionGradeShader {
    inline constexpr const char* VERTEX = R"glsl(#version 320 es
precision highp float;
in vec2 pos;
in vec2 texcoord;
uniform mat3 proj;
void main() {
    gl_Position = vec4((proj * vec3(pos, 1.0)).xy, 0.0, 1.0);
}
)glsl";

    inline constexpr const char* FRAGMENT = R"glsl(#version 320 es
precision highp float;
precision highp int;
precision highp sampler2D;
out vec4 fragColor;

#define TF_LINEAR 0
#define TF_BT1886 1
#define TF_GAMMA22 2
#define TF_GAMMA28 3
#define TF_ST240 4
#define TF_EXT_LINEAR 5
#define TF_LOG100 6
#define TF_LOG316 7
#define TF_XVYCC 8
#define TF_SRGB 9
#define TF_EXT_SRGB 10
#define TF_PQ 11
#define TF_ST428 12
#define TF_HLG 13

#define MODE_IN_PLACE 1
#define MODE_OVER 2

#define SRGB_POW 2.4
#define SRGB_CUT 0.0031308
#define SRGB_SCALE 12.92
#define SRGB_ALPHA 1.055
#define BT1886_POW (1.0 / 0.45)
#define BT1886_CUT 0.018053968510807
#define BT1886_SCALE 4.5
#define BT1886_ALPHA (1.0 + 5.5 * BT1886_CUT)
#define ST240_POW (1.0 / 0.45)
#define ST240_CUT 0.0228
#define ST240_SCALE 4.0
#define ST240_ALPHA 1.1115
#define ST428_POW 2.6
#define ST428_SCALE (52.37 / 48.0)
#define PQ_M1 0.1593017578125
#define PQ_M2 78.84375
#define PQ_INV_M1 (1.0 / PQ_M1)
#define PQ_INV_M2 (1.0 / PQ_M2)
#define PQ_C1 0.8359375
#define PQ_C2 18.8515625
#define PQ_C3 18.6875
#define HLG_D_CUT (1.0 / 12.0)
#define HLG_E_CUT 0.5
#define HLG_A 0.17883277
#define HLG_B 0.28466892
#define HLG_C 0.55991073

// Jzazbz uses the HDR PQ variant from Safdar et al. (2017):
// n = 2610 / 2^14, p = 1.7 * 2523 / 2^5.
#define JZ_N 0.1593017578125
#define JZ_P 134.034375
#define JZ_C1 0.8359375
#define JZ_C2 18.8515625
#define JZ_C3 18.6875
#define JZ_D -0.56
#define JZ_D0 1.6295499532821566e-11

uniform sampler2D uHorizontal;
uniform sampler2D uVertical;
uniform sampler2D uSnapshot;
uniform int uFullSnapshot;
uniform int uAreaMode;
uniform int uMode;
uniform vec4 uWindowBox;
uniform float uThickness;
uniform bool uOutset;
uniform int uSnapshotExtent;
uniform float uRounding;
uniform float uInnerRounding;
uniform float uRoundingPower;
uniform float uOpacity;
uniform float uLightness;
uniform float uSaturation;
uniform float uChroma;
uniform float uHue;
uniform vec4 uColorizeGradient[10];
uniform int uColorizeGradientLength;
uniform float uColorizeGradientAngle;
uniform float uColorizeStrength;
uniform float uOrbitPhase;
uniform float uOrbitLength;
uniform float uOrbitLightness;
uniform float uOrbitSaturation;
uniform float uOrbitChroma;
uniform float uOrbitHue;
uniform float uOrbitOpacity;
uniform float uOrbitAmount;
uniform vec4 uOrbitColorizeGradient[10];
uniform int uOrbitColorizeGradientLength;
uniform float uOrbitColorizeGradientAngle;
uniform float uOrbitColorizeStrength;
uniform float uCornerArcTable[65];

uniform int uSourceTF;
uniform int uTargetTF;
uniform vec2 uSourceRange;
uniform vec2 uTargetRange;
uniform mat3 uToWorkingPrimaries;
uniform mat3 uFromWorkingPrimaries;
uniform vec3 uTargetLuma;
uniform float uTargetMaxNits;
uniform float uReference;
uniform int uHdr;

vec3 tfInvLinPow(vec3 color, float gamma, float threshold, float scale, float alpha) {
    bvec3 low = lessThanEqual(color, vec3(threshold * scale));
    vec3 lo = color / scale;
    vec3 hi = pow((color + alpha - 1.0) / alpha, vec3(gamma));
    return mix(hi, lo, low);
}
vec3 tfLinPow(vec3 color, float gamma, float threshold, float scale, float alpha) {
    bvec3 low = lessThanEqual(color, vec3(threshold));
    vec3 lo = color * scale;
    vec3 hi = pow(max(color, vec3(0.0)), vec3(1.0 / gamma)) * alpha - (alpha - 1.0);
    return mix(hi, lo, low);
}
vec3 tfInvPQ(vec3 color) {
    vec3 e = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(PQ_INV_M2));
    return pow(max(e - PQ_C1, vec3(0.0)) / max(PQ_C2 - PQ_C3 * e, vec3(0.000001)), vec3(PQ_INV_M1));
}
vec3 tfPQ(vec3 color) {
    vec3 e = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(PQ_M1));
    return pow((vec3(PQ_C1) + PQ_C2 * e) / (vec3(1.0) + PQ_C3 * e), vec3(PQ_M2));
}
vec3 tfInvHLG(vec3 color) {
    bvec3 low = lessThanEqual(color, vec3(HLG_E_CUT));
    vec3 lo = color * color / 3.0;
    vec3 hi = (exp((color - HLG_C) / HLG_A) + HLG_B) / 12.0;
    return mix(hi, lo, low);
}
vec3 tfHLG(vec3 color) {
    bvec3 low = lessThanEqual(color, vec3(HLG_D_CUT));
    vec3 lo = sqrt(max(color, vec3(0.0)) * 3.0);
    vec3 hi = HLG_A * log(max(12.0 * color - HLG_B, vec3(0.0001))) + HLG_C;
    return mix(hi, lo, low);
}
vec3 tfInvSRGB(vec3 color) { return tfInvLinPow(color, SRGB_POW, SRGB_CUT, SRGB_SCALE, SRGB_ALPHA); }
vec3 tfInvBT1886(vec3 color) { return tfInvLinPow(color, BT1886_POW, BT1886_CUT, BT1886_SCALE, BT1886_ALPHA); }
vec3 tfInvST240(vec3 color) { return tfInvLinPow(color, ST240_POW, ST240_CUT, ST240_SCALE, ST240_ALPHA); }
vec3 tfSRGB(vec3 color) { return tfLinPow(color, SRGB_POW, SRGB_CUT, SRGB_SCALE, SRGB_ALPHA); }
vec3 tfBT1886(vec3 color) { return tfLinPow(color, BT1886_POW, BT1886_CUT, BT1886_SCALE, BT1886_ALPHA); }
vec3 tfST240(vec3 color) { return tfLinPow(color, ST240_POW, ST240_CUT, ST240_SCALE, ST240_ALPHA); }

vec3 decodeTF(vec3 color, int tf) {
    switch (tf) {
        case TF_LINEAR:
        case TF_EXT_LINEAR: return color;
        case TF_BT1886: return tfInvBT1886(color);
        case TF_GAMMA22: return pow(max(color, vec3(0.0)), vec3(2.2));
        case TF_GAMMA28: return pow(max(color, vec3(0.0)), vec3(2.8));
        case TF_ST240: return tfInvST240(color);
        case TF_LOG100: return mix(exp((color - 1.0) * 2.0 * log(10.0)), vec3(0.0), lessThanEqual(color, vec3(0.0)));
        case TF_LOG316: return mix(exp((color - 1.0) * 2.5 * log(10.0)), vec3(0.0), lessThanEqual(color, vec3(0.0)));
        case TF_XVYCC: return sign(color) * tfInvBT1886(abs(color));
        case TF_EXT_SRGB: return sign(color) * tfInvSRGB(abs(color));
        case TF_PQ: return tfInvPQ(color);
        case TF_ST428: return pow(max(color, vec3(0.0)), vec3(ST428_POW)) * ST428_SCALE;
        case TF_HLG: return tfInvHLG(color);
        case TF_SRGB:
        default: return tfInvSRGB(color);
    }
}
vec3 encodeTF(vec3 color, int tf) {
    switch (tf) {
        case TF_EXT_LINEAR: return color;
        case TF_BT1886: return tfBT1886(color);
        case TF_GAMMA22: return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
        case TF_GAMMA28: return pow(max(color, vec3(0.0)), vec3(1.0 / 2.8));
        case TF_ST240: return tfST240(color);
        case TF_LOG100: return mix(1.0 + log(color) / log(10.0) / 2.0, vec3(0.0), lessThanEqual(color, vec3(0.01)));
        case TF_LOG316: return mix(1.0 + log(color) / log(10.0) / 2.5, vec3(0.0), lessThanEqual(color, vec3(sqrt(10.0) / 1000.0)));
        case TF_XVYCC: return sign(color) * tfBT1886(abs(color));
        case TF_SRGB: return tfSRGB(color);
        case TF_EXT_SRGB: return sign(color) * tfSRGB(abs(color));
        case TF_PQ: return tfPQ(color);
        case TF_ST428: return pow(max(color, vec3(0.0)) / ST428_SCALE, vec3(1.0 / ST428_POW));
        case TF_HLG: return tfHLG(color);
        case TF_LINEAR:
        default: return color;
    }
}

vec3 linearToOklab(vec3 rgb) {
    float l = pow(max(dot(vec3(0.4122214708, 0.5363325363, 0.0514459929), rgb), 0.0), 1.0 / 3.0);
    float m = pow(max(dot(vec3(0.2119034982, 0.6806995451, 0.1073969566), rgb), 0.0), 1.0 / 3.0);
    float s = pow(max(dot(vec3(0.0883024619, 0.2817188376, 0.6299787005), rgb), 0.0), 1.0 / 3.0);
    return vec3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
}
vec3 oklabToLinear(vec3 lab) {
    float l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    return vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
               -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
               -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

// Linear BT.709 RGB in nits -> Jzazbz. The matrix includes the Jzazbz
// X'/Y' pre-processing and the BT.709 -> XYZ D65 transform, so the input is
// the same linear working space used by the rest of the shader.
vec3 linearToJzazbz(vec3 rgbNits) {
    vec3 lms = vec3(
        dot(vec3(0.358515604390249, 0.509182112313592, 0.104099482995855), rgbNits),
        dot(vec3(0.220448033922503, 0.592272879605446, 0.159543683073875), rgbNits),
        dot(vec3(0.079388342280360, 0.230332142660558, 0.663198985825040), rgbNits)
    ) / 10000.0;
    vec3 q = pow(clamp(lms, vec3(0.0), vec3(1.0)), vec3(JZ_N));
    vec3 p = pow((vec3(JZ_C1) + JZ_C2 * q) / (vec3(1.0) + JZ_C3 * q), vec3(JZ_P));
    float iz = 0.5 * (p.x + p.y);
    float az = 3.524000 * p.x - 4.066708 * p.y + 0.542708 * p.z;
    float bz = 0.199076 * p.x + 1.096799 * p.y - 1.295875 * p.z;
    float jz = ((1.0 + JZ_D) * iz) / (1.0 + JZ_D * iz) - JZ_D0;
    return vec3(jz, az, bz);
}
vec3 jzazbzToLinear(vec3 lab) {
    float iz = (lab.x + JZ_D0) / (1.0 + JZ_D - JZ_D * (lab.x + JZ_D0));
    vec3 p;
    p.x = iz + 0.1386050432715393 * lab.y + 0.0580473161561189 * lab.z;
    p.y = iz - 0.1386050432715393 * lab.y - 0.0580473161561189 * lab.z;
    p.z = iz - 0.0960192420263189 * lab.y - 0.8118918960560390 * lab.z;
    vec3 e = pow(clamp(p, vec3(0.0), vec3(1.0)), vec3(1.0 / JZ_P));
    vec3 lms = pow(max(e - JZ_C1, vec3(0.0)) / max(JZ_C2 - JZ_C3 * e, vec3(0.000001)), vec3(1.0 / JZ_N));
    lms *= 10000.0;
    return vec3(
        dot(vec3(5.929591324798722, -5.224543357198974, 0.326109513013450), lms),
        dot(vec3(-2.223887832959324, 3.822134206719980, -0.570404664326052), lms),
        dot(vec3(0.062564064199942, -0.702040466016277, 1.666910321798117), lms)
    );
}

vec3 gradeLab(vec3 lab, float lightness, float saturation, float chroma, float hue, vec2 colorizeChroma, float colorizeStrength) {
    lab.x = clamp(lab.x + lightness, 0.0, 1.0);

    float magnitude = length(lab.yz);
    vec2 direction = magnitude > 0.000001 ? lab.yz / magnitude : vec2(0.0);
    float angle = radians(hue);
    float cosine = cos(angle);
    float sine = sin(angle);
    vec2 rotated = vec2(cosine * direction.x - sine * direction.y, sine * direction.x + cosine * direction.y);
    float offset = chroma * smoothstep(0.0, 0.02, magnitude);
    lab.yz = rotated * max(magnitude * saturation + offset, 0.0);

    if (colorizeStrength > 0.0)
        lab.yz = mix(lab.yz, colorizeChroma, clamp(colorizeStrength, 0.0, 1.0));

    lab.x = clamp(lab.x, 0.0, 1.0);
    return lab;
}

vec4 sampleGradient(vec2 coord, vec4 gradient[10], int length, float angle) {
    if (length <= 0)
        return vec4(0.0);
    if (length == 1)
        return gradient[0];

    vec2 normalized = clamp(coord, vec2(0.0), vec2(1.0));
    float finalAngle = angle;
    if (angle > 4.71) {
        normalized.y = 1.0 - normalized.y;
        finalAngle = 6.28 - angle;
    } else if (angle > 3.14) {
        normalized = vec2(1.0) - normalized;
        finalAngle = angle - 3.14;
    } else if (angle > 1.57) {
        normalized.x = 1.0 - normalized.x;
        finalAngle = 3.14 - angle;
    }

    float progress = (normalized.y * sin(finalAngle) + normalized.x * (1.0 - sin(finalAngle))) * float(length - 1);
    if (progress >= float(length - 1))
        return gradient[length - 1];
    int lower = int(floor(progress));
    int upper = lower + 1;
    return mix(gradient[lower], gradient[upper], progress - float(lower));
}

vec2 colorizeChromaFromGradient(vec4 gradientColor) {
    if (uHdr != 0)
        return linearToJzazbz(oklabToLinear(gradientColor.xyz) * uReference).yz;
    return gradientColor.yz;
}

// Map a target-linear colour into the target gamut by desaturating toward its
// luminance. HDR targets keep highlights above SDR white; only negative and
// over-range components force a reduction.
vec3 gamutMap(vec3 rgb, float maxNits) {
    float luma = clamp(dot(rgb, uTargetLuma), 0.0, maxNits);
    if (all(greaterThanEqual(rgb, vec3(0.0))) && all(lessThanEqual(rgb, vec3(maxNits))))
        return rgb;

    float low = 0.0;
    float high = 1.0;
    for (int i = 0; i < 12; ++i) {
        float middle = (low + high) * 0.5;
        vec3 candidate = mix(vec3(luma), rgb, middle);
        if (all(greaterThanEqual(candidate, vec3(0.0))) && all(lessThanEqual(candidate, vec3(maxNits))))
            low = middle;
        else
            high = middle;
    }
    return clamp(mix(vec3(luma), rgb, low), vec3(0.0), vec3(maxNits));
}

float roundedBoxCoverage(vec2 point, vec2 halfSize, float radius, float power) {
    vec2 correction = vec2(1.0) / (halfSize * 2.0);
    vec2 inner = abs(point) - halfSize + vec2(radius) + correction;
    if (min(inner.x, inner.y) > 0.0 && radius > 0.0) {
        float dist = pow(pow(max(inner.x, 0.0), power) + pow(max(inner.y, 0.0), power), 1.0 / power);
        return 1.0 - smoothstep(radius - 1.0, radius + 1.0, dist);
    }
    return 1.0;
}

float borderCoverage(vec2 point, vec2 halfSize, float radiusOuter, float thickness, float power) {
    // Match Hyprland's border.glsl, including pixel centering and its separate
    // inner/outer corner branches. Subtracting two smoothed masks thins corners.
    const float smoothing = 3.14159265358979323846 / 5.34665792551;
    float radius = uOutset ? (uInnerRounding > 0.0 ? uInnerRounding + thickness : 0.0) : radiusOuter;
    vec2 correction = vec2(1.0) / (halfSize * 2.0);
    vec2 inner = abs(point) - halfSize + vec2(radius) + correction;
    vec2 outer = abs(point) - halfSize + vec2(radiusOuter) + correction;
    if (min(inner.x, inner.y) > 0.0 && radius > 0.0) {
        float dist = pow(pow(inner.x, power) + pow(inner.y, power), 1.0 / power);
        float distOuter = pow(pow(max(outer.x, 0.0), power) + pow(max(outer.y, 0.0), power), 1.0 / power);
        float h = thickness * 0.5;
        if (dist < radius - h)
            return smoothstep(0.0, 1.0, (dist - radius + thickness + smoothing) / (smoothing * 2.0));
        if (min(outer.x, outer.y) > 0.0)
            return 1.0 - smoothstep(0.0, 1.0, (distOuter - radiusOuter + smoothing) / (smoothing * 2.0));
        if (distOuter < radiusOuter - h)
            return 1.0;
    }
    vec2 edgeDistance = halfSize - abs(point);
    return min(edgeDistance.x, edgeDistance.y) <= thickness ? 1.0 : 0.0;
}
float cornerArc(float theta, float radius) {
    const float tableSize = 64.0;
    const float halfPi = 1.57079632679;
    float index = clamp(theta * tableSize / halfPi, 0.0, tableSize);
    int lower = int(floor(index));
    int upper = min(lower + 1, 64);
    return mix(uCornerArcTable[lower], uCornerArcTable[upper], index - float(lower)) * radius;
}
float orbitCoordinate(vec2 point, vec2 halfSize, float radius, float power, float quarter, float horizontal, float vertical) {
    float x = abs(point.x);
    float y = abs(point.y);
    float horizontalRadius = halfSize.x - radius;
    float verticalRadius = halfSize.y - radius;

    if (radius > 0.0 && x > horizontalRadius && y > verticalRadius) {
        float dx = clamp((x - horizontalRadius) / radius, 0.0, 1.0);
        float dy = clamp((y - verticalRadius) / radius, 0.0, 1.0);
        float theta = atan(pow(dy, power * 0.5), pow(dx, power * 0.5));
        float arc = cornerArc(theta, radius);
        if (point.x >= 0.0 && point.y >= 0.0)
            return horizontal + quarter - arc;
        if (point.x >= 0.0 && point.y < 0.0)
            return horizontal + quarter + vertical + arc;
        if (point.x < 0.0 && point.y < 0.0)
            return horizontal * 2.0 + quarter * 3.0 + vertical - arc;
        return horizontal * 2.0 + quarter * 3.0 + vertical * 2.0 + arc;
    }

    if (x - horizontalRadius > y - verticalRadius) {
        if (point.x >= 0.0)
            return horizontal + quarter + clamp(verticalRadius - point.y, 0.0, vertical);
        return horizontal * 2.0 + quarter * 3.0 + vertical + clamp(point.y + verticalRadius, 0.0, vertical);
    }
    if (point.y < 0.0)
        return horizontal + quarter * 2.0 + vertical + clamp(horizontalRadius - point.x, 0.0, horizontal);
    return clamp(point.x + horizontalRadius, 0.0, horizontal);
}
float orbitCoverage(vec2 point, vec2 halfSize, float rounding, float thickness, float power) {
    if (uOrbitLength <= 0.0 || uOrbitAmount <= 0.0)
        return 0.0;
    halfSize = max(halfSize - vec2(thickness * 0.5), vec2(0.0));
    float radius = clamp(rounding - thickness * 0.5, 0.0, min(halfSize.x, halfSize.y));
    float horizontal = max(2.0 * (halfSize.x - radius), 0.0);
    float vertical = max(2.0 * (halfSize.y - radius), 0.0);
    float quarter = uCornerArcTable[64] * radius;
    float perimeter = 2.0 * (horizontal + vertical) + 4.0 * quarter;
    if (perimeter <= 0.0)
        return 0.0;

    float coordinate = orbitCoordinate(point, halfSize, radius, power, quarter, horizontal, vertical);
    float start = uOrbitPhase * perimeter;
    float distance = mod(coordinate - start + perimeter, perimeter);
    float segmentLength = uOrbitLength * perimeter;
    float feather = min(max(1.0, perimeter * 0.01), segmentLength * 0.5);
    return smoothstep(0.0, feather, distance) * (1.0 - smoothstep(segmentLength - feather, segmentLength, distance)) * uOrbitAmount;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int snapshotExtent = uSnapshotExtent;
    int left = int(uWindowBox.x);
    int bottom = int(uWindowBox.y);
    int width = int(uWindowBox.z);
    int height = int(uWindowBox.w);
    int right = left + width;
    int top = bottom + height;

    vec2 halfSize = vec2(uWindowBox.zw) * 0.5;
    vec2 center = uWindowBox.xy + halfSize;
    vec2 point = gl_FragCoord.xy - center;
    float power = max(uRoundingPower, 1.0);
    float coverage = uAreaMode == 2 ? roundedBoxCoverage(point, halfSize, uRounding, power) : borderCoverage(point, halfSize, uRounding, uThickness, power);
    if (coverage <= 0.0)
        discard;

    float orbitMask = orbitCoverage(point, halfSize, uRounding, uThickness, power);
    float band = coverage * clamp(mix(uOpacity, uOrbitOpacity, orbitMask), 0.0, 1.0);
    if (band <= 0.0)
        discard;

    vec4 sampleColor;
    if (uFullSnapshot == 1) {
        sampleColor = texelFetch(uSnapshot, pixel, 0);
    } else if (pixel.y < bottom + snapshotExtent) {
        sampleColor = texelFetch(uHorizontal, ivec2(pixel.x, pixel.y - bottom), 0);
    } else if (pixel.y >= top - snapshotExtent) {
        sampleColor = texelFetch(uHorizontal, ivec2(pixel.x, snapshotExtent + pixel.y - (top - snapshotExtent)), 0);
    } else if (pixel.x < left + snapshotExtent) {
        sampleColor = texelFetch(uVertical, ivec2(pixel.x - left, pixel.y), 0);
    } else {
        sampleColor = texelFetch(uVertical, ivec2(snapshotExtent + pixel.x - (right - snapshotExtent), pixel.y), 0);
    }
    if (sampleColor.a <= 0.001)
        discard;

    vec3 encoded = sampleColor.rgb / max(sampleColor.a, 0.001);
    vec3 sourceLinear = decodeTF(encoded, uSourceTF);
    if (uSourceTF != TF_LINEAR)
        sourceLinear = sourceLinear * (uSourceRange.y - uSourceRange.x) + uSourceRange.x;
    sourceLinear = uToWorkingPrimaries * sourceLinear;

    vec2 gradientCoord = clamp((gl_FragCoord.xy - uWindowBox.xy) / max(uWindowBox.zw, vec2(1.0)), vec2(0.0), vec2(1.0));
    vec4 baseGradient = sampleGradient(gradientCoord, uColorizeGradient, uColorizeGradientLength, uColorizeGradientAngle);
    vec2 colorizeChroma = colorizeChromaFromGradient(baseGradient);

    vec3  baseLab = uHdr != 0 ? linearToJzazbz(sourceLinear) : linearToOklab(sourceLinear / uReference);
    float lightnessScale = uHdr != 0 ? linearToJzazbz(vec3(uReference)).x : 1.0;
    vec3  gradedLab = gradeLab(baseLab, uLightness * lightnessScale, uSaturation, uChroma, uHue, colorizeChroma, uColorizeStrength);

    if (orbitMask > 0.0) {
        vec4 orbitGradient = sampleGradient(gradientCoord, uOrbitColorizeGradient, uOrbitColorizeGradientLength, uOrbitColorizeGradientAngle);
        vec2 orbitColorizeChroma = colorizeChromaFromGradient(orbitGradient);
        vec3 orbitLab = gradeLab(baseLab, uOrbitLightness * lightnessScale, uOrbitSaturation, uOrbitChroma, uOrbitHue, orbitColorizeChroma,
                                 uOrbitColorizeStrength);
        gradedLab = mix(gradedLab, orbitLab, orbitMask);
    }

    vec3 gradedLinear = uHdr != 0 ? jzazbzToLinear(gradedLab) : oklabToLinear(gradedLab) * uReference;

    float outAlpha = sampleColor.a;
    vec3 outLinear;
    if (uMode == MODE_OVER) {
        float sourceAlpha = sampleColor.a * band;
        vec3 sourcePremul = gradedLinear * sourceAlpha;
        vec3 destinationPremul = sourceLinear * sampleColor.a;
        outAlpha = sourceAlpha + sampleColor.a * (1.0 - sourceAlpha);
        outLinear = (sourcePremul + destinationPremul * (1.0 - sourceAlpha)) / max(outAlpha, 0.001);
    } else {
        outLinear = mix(sourceLinear, gradedLinear, band);
    }

    vec3 targetLinear = uFromWorkingPrimaries * outLinear;
    targetLinear = gamutMap(targetLinear, uTargetMaxNits);

    vec3 targetEncoded;
    if (uTargetTF == TF_LINEAR) {
        targetEncoded = encodeTF(targetLinear, uTargetTF);
    } else {
        vec3 normalized = (targetLinear - vec3(uTargetRange.x)) / max(uTargetRange.y - uTargetRange.x, 0.001);
        targetEncoded = encodeTF(normalized, uTargetTF);
    }

    fragColor = vec4(targetEncoded * outAlpha, outAlpha);
}
)glsl";
}
