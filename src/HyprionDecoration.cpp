#include "HyprionDecoration.hpp"
#include "HyprionConfig.hpp"
#include "HyprionShader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <unordered_map>

#include <drm_fourcc.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

using namespace Hyprutils::Memory;
using namespace Render;
using namespace Render::GL;

namespace {
    constexpr uint64_t EDGES = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;

    constexpr const char* VERTEX_SHADER = R"glsl(#version 320 es
precision highp float;
in vec2 pos;
in vec2 texcoord;
uniform mat3 proj;
void main() {
    gl_Position = vec4((proj * vec3(pos, 1.0)).xy, 0.0, 1.0);
}
)glsl";

    constexpr const char* FRAGMENT_SHADER = R"glsl(#version 320 es
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

uniform sampler2D uHorizontal;
uniform sampler2D uVertical;
uniform vec4 uWindowBox;
uniform float uThickness;
uniform bool uOutset;
uniform int uSnapshotExtent;
uniform float uRounding;
uniform float uInnerRounding;
uniform float uRoundingPower;
uniform float uStrength;
uniform float uOpacity;
uniform float uLightness;
uniform float uSaturation;
uniform float uChroma;
uniform float uHue;
uniform vec2 uTintChroma;
uniform float uTintStrength;
uniform float uOrbitPhase;
uniform float uOrbitLength;
uniform float uOrbitLightness;
uniform float uOrbitSaturation;
uniform float uOrbitChroma;
uniform float uOrbitHue;
uniform float uOrbitStrength;
uniform float uOrbitOpacity;
uniform float uOrbitAmount;
uniform vec2 uOrbitTintChroma;
uniform float uOrbitTintStrength;
uniform float uCornerArcTable[65];

uniform int uSourceTF;
uniform int uTargetTF;
uniform vec2 uSourceRange;
uniform vec2 uTargetRange;
uniform float uDefaultReference;
uniform mat3 uToDefaultPrimaries;
uniform mat3 uFromDefaultPrimaries;

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
    return pow(max(e - PQ_C1, vec3(0.0)) / (PQ_C2 - PQ_C3 * e), vec3(PQ_INV_M1));
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
bool inGamut(vec3 rgb) {
    return all(greaterThanEqual(rgb, vec3(0.0))) && all(lessThanEqual(rgb, vec3(1.0)));
}
vec3 compressChroma(float lightness, vec2 chroma) {
    vec3 candidate = oklabToLinear(vec3(lightness, chroma));
    if (inGamut(candidate))
        return candidate;
    float low = 0.0;
    float high = 1.0;
    for (int i = 0; i < 10; ++i) {
        float middle = (low + high) * 0.5;
        if (inGamut(oklabToLinear(vec3(lightness, chroma * middle))))
            low = middle;
        else
            high = middle;
    }
    return clamp(oklabToLinear(vec3(lightness, chroma * low)), vec3(0.0), vec3(1.0));
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
vec4 transformSample(vec4 sampleColor, float lightness, float saturation, float chroma, float hue, vec2 tintChroma, float tintStrength) {
    if (sampleColor.a <= 0.001)
        return sampleColor;
    vec3 unpremultiplied = sampleColor.rgb / max(sampleColor.a, 0.001);
    vec3 linearColor = decodeTF(unpremultiplied, uSourceTF);
    if (uSourceTF != TF_LINEAR)
        linearColor = linearColor * (uSourceRange.y - uSourceRange.x) + uSourceRange.x;
    linearColor = uToDefaultPrimaries * linearColor;

    float reference = max(uDefaultReference, 0.001);
    vec3 normalized = max(linearColor / reference, vec3(0.0));
    float peak = max(max(max(normalized.r, normalized.g), normalized.b), 1.0);
    normalized = clamp(normalized / peak, vec3(0.0), vec3(1.0));
    vec3 lab = linearToOklab(normalized);
    lab.x = clamp(lab.x + lightness, 0.0, 1.0);
    // Rotate hue in the OKLab (a, b) plane, then scale the chroma magnitude and
    // offset it by `chroma`. The offset applies only to chroma the sample
    // already has: a neutral sample has no hue, so it stays neutral instead of
    // being pushed toward an arbitrary one. Gating the offset keeps that
    // transition smooth for near-neutral samples.
    float magnitude = length(lab.yz);
    vec2 direction = magnitude > 0.000001 ? lab.yz / magnitude : vec2(0.0);
    float angle = radians(hue);
    float cosine = cos(angle);
    float sine = sin(angle);
    vec2 rotated = vec2(cosine * direction.x - sine * direction.y, sine * direction.x + cosine * direction.y);
    float offset = chroma * smoothstep(0.0, 0.02, magnitude);
    lab.yz = rotated * max(magnitude * saturation + offset, 0.0);
    if (tintStrength > 0.0)
        lab.yz = mix(lab.yz, tintChroma, tintStrength);
    vec3 adjusted = compressChroma(lab.x, lab.yz) * (peak * reference);
    adjusted = uFromDefaultPrimaries * adjusted;

    if (uTargetTF == TF_LINEAR)
        adjusted = encodeTF(adjusted, uTargetTF);
    else {
        adjusted = (adjusted - vec3(uTargetRange.x)) / (uTargetRange.y - uTargetRange.x);
        adjusted = encodeTF(adjusted, uTargetTF);
    }
    return vec4(adjusted * sampleColor.a, sampleColor.a);
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
    float coverage = borderCoverage(point, halfSize, uRounding, uThickness, power);
    if (coverage <= 0.0)
        discard;

    // Opacity is how much of the border covers what is beneath it, resolved per
    // fragment: the state's coverage outside the orbit, and the mix of the two
    // where the orbit segment travels.
    float orbitMask = orbitCoverage(point, halfSize, uRounding, uThickness, power);
    float band = coverage * clamp(mix(uOpacity, uOrbitOpacity, orbitMask), 0.0, 1.0);
    if (band <= 0.0)
        discard;

    vec4 sampleColor;
    if (pixel.y < bottom + snapshotExtent) {
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
    vec4 color = sampleColor;
    if (uStrength > 0.0 && (uLightness != 0.0 || uSaturation != 1.0 || uChroma != 0.0 || uHue != 0.0 || uTintStrength > 0.0)) {
        vec4 transformed = transformSample(sampleColor, uLightness, uSaturation, uChroma, uHue, uTintChroma, uTintStrength);
        // Strength is the transform amount; above 1 it extrapolates past the
        // transformed colour and clips into the gamut when written out.
        color = mix(sampleColor, transformed, uStrength);
    }
    if (orbitMask > 0.0 && uOrbitStrength > 0.0) {
        vec4 orbitTransformed = transformSample(sampleColor, uOrbitLightness, uOrbitSaturation, uOrbitChroma, uOrbitHue, uOrbitTintChroma, uOrbitTintStrength);
        vec4 orbitColor = mix(sampleColor, orbitTransformed, uOrbitStrength);
        color = mix(color, orbitColor, orbitMask);
    }
    if (uOutset) {
        // Native borders blend their coverage over the already-rendered surface.
        // The outset's source is pre-shadow, but its destination must remain live:
        // opacity lowers the premultiplied coverage so the live backdrop shows
        // through the frost.
        fragColor = color * band;
    } else {
        // The inset replaces pixels whose backdrop is the sample itself, so the
        // coverage falls back to the sample instead of blending.
        fragColor = mix(sampleColor, color, band);
    }
}
)glsl";

    struct SUniforms {
        GLint horizontal = -1;
        GLint vertical = -1;
        GLint windowBox = -1;
        GLint thickness = -1;
        GLint outset = -1;
        GLint snapshotExtent = -1;
        GLint rounding = -1;
        GLint innerRounding = -1;
        GLint roundingPower = -1;
        GLint strength = -1;
        GLint opacity = -1;
        GLint lightness = -1;
        GLint saturation = -1;
        GLint chroma = -1;
        GLint hue = -1;
        GLint tintChroma = -1;
        GLint tintStrength = -1;
        GLint orbitPhase = -1;
        GLint orbitLength = -1;
        GLint orbitLightness = -1;
        GLint orbitSaturation = -1;
        GLint orbitChroma = -1;
        GLint orbitHue = -1;
        GLint orbitStrength = -1;
        GLint orbitOpacity = -1;
        GLint orbitAmount = -1;
        GLint orbitTintChroma = -1;
        GLint orbitTintStrength = -1;
        GLint cornerArcTable = -1;
        GLint sourceTF = -1;
        GLint targetTF = -1;
        GLint sourceRange = -1;
        GLint targetRange = -1;
        GLint defaultReference = -1;
        GLint toDefaultPrimaries = -1;
        GLint fromDefaultPrimaries = -1;
    };


    struct SMonitorSnapshots {
        SP<IFramebuffer> horizontal;
        SP<IFramebuffer> vertical;
        int              width = 0;
        int              height = 0;
        int              maxStrip = 0;
        DRMFormat        format = DRM_FORMAT_INVALID;
        uint64_t         imageDescriptionId = 0;

        int              failedWidth = 0;
        int              failedHeight = 0;
        int              failedStrip = 0;
        DRMFormat        failedFormat = DRM_FORMAT_INVALID;
        uint64_t         failedImageDescriptionId = 0;
    };

    SP<CShader>                                shader;
    SUniforms                                  uniforms;
    std::unordered_map<uint64_t, SMonitorSnapshots> monitorSnapshots;

    std::array<GLfloat, 9> matrixFor(const std::array<std::array<double, 3>, 3>& matrix) {
        return {
            static_cast<GLfloat>(matrix[0][0]), static_cast<GLfloat>(matrix[1][0]), static_cast<GLfloat>(matrix[2][0]),
            static_cast<GLfloat>(matrix[0][1]), static_cast<GLfloat>(matrix[1][1]), static_cast<GLfloat>(matrix[2][1]),
            static_cast<GLfloat>(matrix[0][2]), static_cast<GLfloat>(matrix[1][2]), static_cast<GLfloat>(matrix[2][2]),
        };
    }

    std::array<GLfloat, 2> toOklabChroma(const std::array<float, 3>& srgbColor, const std::array<std::array<double, 3>, 3>& toDefault) {
        std::array<double, 3> linear{};
        for (size_t i = 0; i < linear.size(); ++i) {
            const double channel = std::clamp(static_cast<double>(srgbColor[i]), 0.0, 1.0);
            linear[i] = channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        }

        std::array<double, 3> rgb{};
        for (size_t row = 0; row < rgb.size(); ++row) {
            for (size_t column = 0; column < linear.size(); ++column)
                rgb[row] += toDefault[row][column] * linear[column];
        }

        const auto cubeRoot = [](double value) { return std::cbrt(std::max(value, 0.0)); };
        const double l = cubeRoot(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
        const double m = cubeRoot(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
        const double s = cubeRoot(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
        return {
            static_cast<GLfloat>(1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s),
            static_cast<GLfloat>(0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s),
        };
    }

    bool configureTexture(const SP<ITexture>& texture) {
        if (!texture)
            return false;
        texture->bind();
        texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        texture->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        texture->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texture->unbind();
        return true;
    }

    // Hyprland picks the blur source for a window: the shared pre-window cache
    // (tiled windows with new_optimizations, or xray) or a live blur of the
    // framebuffer. Ask it so the outset matches whatever the window itself gets.
    bool outsetUsesSharedBlur(Render::CRenderContext& ctx, const PHLWINDOW& window) {
        return g_pHyprRenderer && ctx.m_data.pMonitor && window && g_pHyprRenderer->shouldUseNewBlurOptimizations(ctx, nullptr, window);
    }

    bool allocateFramebuffer(SP<IFramebuffer>& framebuffer, const std::string& name, int width, int height, DRMFormat format,
                             const NColorManagement::PImageDescription& imageDescription) {
        framebuffer = g_pHyprRenderer->createFB(name);
        if (!framebuffer || !framebuffer->alloc(width, height, format))
            return false;
        framebuffer->setImageDescription(imageDescription);
        return configureTexture(framebuffer->getTexture());
    }

    void copyClippedStrip(int targetWidth, int targetHeight, int textureWidth, int textureHeight, int64_t destinationX, int64_t destinationY, int64_t sourceX,
                          int64_t sourceY, int64_t width, int64_t height) {
        if (width <= 0 || height <= 0)
            return;

        const int64_t destinationLeft   = std::max<int64_t>(destinationX, 0);
        const int64_t destinationBottom = std::max<int64_t>(destinationY, 0);
        const int64_t destinationRight  = std::min<int64_t>(destinationX + width, textureWidth);
        const int64_t destinationTop    = std::min<int64_t>(destinationY + height, textureHeight);
        if (destinationRight <= destinationLeft || destinationTop <= destinationBottom)
            return;

        const int64_t requestedWidth  = destinationRight - destinationLeft;
        const int64_t requestedHeight = destinationTop - destinationBottom;
        const int64_t requestedSourceX = sourceX + destinationLeft - destinationX;
        const int64_t requestedSourceY = sourceY + destinationBottom - destinationY;
        const int64_t clippedLeft   = std::max<int64_t>(requestedSourceX, 0);
        const int64_t clippedBottom = std::max<int64_t>(requestedSourceY, 0);
        const int64_t clippedRight  = std::min<int64_t>(requestedSourceX + requestedWidth, targetWidth);
        const int64_t clippedTop    = std::min<int64_t>(requestedSourceY + requestedHeight, targetHeight);
        if (clippedRight <= clippedLeft || clippedTop <= clippedBottom)
            return;

        const int64_t copyWidth  = clippedRight - clippedLeft;
        const int64_t copyHeight = clippedTop - clippedBottom;
        const int64_t destX      = destinationLeft + clippedLeft - requestedSourceX;
        const int64_t destY      = destinationBottom + clippedBottom - requestedSourceY;
        if (destX < 0 || destY < 0 || destX + copyWidth > textureWidth || destY + copyHeight > textureHeight ||
            clippedLeft > std::numeric_limits<GLint>::max() || clippedBottom > std::numeric_limits<GLint>::max() || copyWidth > std::numeric_limits<GLsizei>::max() ||
            copyHeight > std::numeric_limits<GLsizei>::max())
            return;

        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(destX), static_cast<GLint>(destY), static_cast<GLint>(clippedLeft),
                            static_cast<GLint>(clippedBottom), static_cast<GLsizei>(copyWidth), static_cast<GLsizei>(copyHeight));
    }

    bool ensureSnapshots(PHLMONITOR monitor, const SP<IFramebuffer>& sourceFramebuffer, const NColorManagement::PImageDescription& imageDescription, int width,
                         int height, int strip) {
        auto& cache = monitorSnapshots[static_cast<uint64_t>(monitor->m_id)];
        const auto descriptionId = imageDescription->id();
        const bool sameTarget = cache.width == width && cache.height == height && cache.format == sourceFramebuffer->m_drmFormat &&
            cache.imageDescriptionId == descriptionId;
        const bool ready = sameTarget && cache.horizontal && cache.vertical && cache.maxStrip >= strip;
        if (ready)
            return true;

        const bool previouslyFailed = cache.failedWidth == width && cache.failedHeight == height && cache.failedStrip == strip &&
            cache.failedFormat == sourceFramebuffer->m_drmFormat && cache.failedImageDescriptionId == descriptionId;
        if (previouslyFailed)
            return false;

        const int allocationStrip = sameTarget ? std::max(cache.maxStrip, strip) : strip;
        if (allocationStrip < 1 || allocationStrip > std::numeric_limits<int>::max() / 2) {
            LOG(Log::ERR, "hyprion: invalid snapshot strip size for monitor {}", monitor->m_id);
            return false;
        }

        SP<IFramebuffer> horizontal;
        SP<IFramebuffer> vertical;
        const auto monitorName = std::to_string(monitor->m_id);
        const bool allocated = allocateFramebuffer(horizontal, "hyprion-horizontal-" + monitorName, width, allocationStrip * 2, sourceFramebuffer->m_drmFormat, imageDescription) &&
            allocateFramebuffer(vertical, "hyprion-vertical-" + monitorName, allocationStrip * 2, height, sourceFramebuffer->m_drmFormat, imageDescription);
        if (!allocated) {
            cache.failedWidth = width;
            cache.failedHeight = height;
            cache.failedStrip = strip;
            cache.failedFormat = sourceFramebuffer->m_drmFormat;
            cache.failedImageDescriptionId = descriptionId;
            LOG(Log::ERR, "hyprion: failed to allocate pixel snapshots for monitor {}", monitor->m_id);
            return false;
        }

        cache.horizontal = std::move(horizontal);
        cache.vertical = std::move(vertical);
        cache.width = width;
        cache.height = height;
        cache.maxStrip = allocationStrip;
        cache.format = sourceFramebuffer->m_drmFormat;
        cache.imageDescriptionId = descriptionId;
        cache.failedWidth = 0;
        cache.failedHeight = 0;
        cache.failedStrip = 0;
        cache.failedFormat = DRM_FORMAT_INVALID;
        cache.failedImageDescriptionId = 0;
        return true;
    }

    void copyWindowSnapshots(SMonitorSnapshots& cache, int targetWidth, int targetHeight, int left, int bottom, int width, int height, int strip) {
        const auto horizontal = cache.horizontal->getTexture();
        const auto vertical = cache.vertical->getTexture();
        if (!horizontal || !vertical)
            return;

        horizontal->bind();
        // Keep the long texture axis in framebuffer coordinates. Window-local
        // offsets can exceed these monitor-sized textures for offscreen windows.
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache.maxStrip * 2, left, 0, left, bottom, width, strip);
        copyClippedStrip(targetWidth, targetHeight, targetWidth, cache.maxStrip * 2, left, strip, left, static_cast<int64_t>(bottom) + height - strip, width, strip);
        horizontal->unbind();

        const int64_t middleHeight = std::max<int64_t>(0, static_cast<int64_t>(height) - 2LL * strip);
        if (middleHeight == 0)
            return;
        vertical->bind();
        const int64_t middleStart = static_cast<int64_t>(bottom) + strip;
        copyClippedStrip(targetWidth, targetHeight, cache.maxStrip * 2, targetHeight, 0, middleStart, left, middleStart, strip, middleHeight);
        copyClippedStrip(targetWidth, targetHeight, cache.maxStrip * 2, targetHeight, strip, middleStart, static_cast<int64_t>(left) + width - strip,
                         middleStart, strip, middleHeight);
        vertical->unbind();
    }


    bool safeRoundedInt(double value, int& result) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min() / 2) || value > static_cast<double>(std::numeric_limits<int>::max() / 2))
            return false;
        result = static_cast<int>(std::lround(value));
        return true;
    }

    void setMat3(GLint location, const std::array<GLfloat, 9>& matrix) {
        if (location >= 0)
            glUniformMatrix3fv(location, 1, GL_FALSE, matrix.data());
    }

    void setFloat(GLint location, GLfloat value) {
        if (location >= 0)
            glUniform1f(location, value);
    }

    void setInt(GLint location, GLint value) {
        if (location >= 0)
            glUniform1i(location, value);
    }

}

class CHyprionPassElement final : public IPassElement {
  public:
    CHyprionPassElement(CHyprionDecoration* decoration, float alpha, CBox box, bool outset, bool snapshotOnly = false) :
        m_decoration(decoration), m_alpha(alpha), m_box(box), m_outset(outset), m_snapshotOnly(snapshotOnly) {}

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override {
        if (m_decoration)
            m_decoration->drawPass(ctx, m_alpha, m_box, m_outset, m_snapshotOnly);
        return {};
    }

    bool needsLiveBlur(Render::CRenderContext& ctx) override { return snapshotUsesBlur() && !outsetSnapshotUsesCache(ctx); }
    bool needsPrecomputeBlur(Render::CRenderContext& ctx) override { return snapshotUsesBlur() && outsetSnapshotUsesCache(ctx); }
    const char* passName() override { return "hyprion"; }
    ePassElementType type() override { return EK_CUSTOM; }
    std::optional<CBox> boundingBox(Render::CRenderContext&) override { return m_box; }
    bool disableSimplification(Render::CRenderContext&) override { return true; }

  private:
    bool snapshotUsesBlur() const { return m_outset && m_snapshotOnly && hyprionConfig().outsetBlur; }
    bool outsetSnapshotUsesCache(Render::CRenderContext& ctx) const { return m_decoration && outsetUsesSharedBlur(ctx, m_decoration->m_window.lock()); }

    CHyprionDecoration* m_decoration = nullptr;
    float            m_alpha = 1.F;
    CBox             m_box;
    bool             m_outset;
    bool             m_snapshotOnly;
};

CHyprionDecoration::CHyprionDecoration(PHLWINDOW window) :
    IHyprWindowDecoration(window), m_window(window),
    m_animations{CHyprionAnimation(Desktop::focusState()->window() == window, (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE),
        CHyprionAnimation(Desktop::focusState()->window() == window, (window->m_hints & Desktop::View::WINDOW_HINT_URGENT) != Desktop::View::WINDOW_HINT_NONE)} {
    for (auto& animation : m_animations)
        animation.bind(window);
}

CHyprionDecoration::~CHyprionDecoration() {
    damageEntire();
}

void CHyprionDecoration::stateChanged(bool focused, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].stateChanged(focused, urgent, *profiles[i], now);
        else
            m_animations[i].reset(focused, urgent, now);
    }
    damageBorder();
}

void CHyprionDecoration::refreshAnimation(bool focused, bool urgent, const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    for (size_t i = 0; i < m_animations.size(); ++i) {
        if (config.enabled && profiles[i]->enabled)
            m_animations[i].refresh(focused, urgent, *profiles[i], now);
        else
            m_animations[i].reset(focused, urgent, now);
    }
    damageBorder();
}

bool CHyprionDecoration::animate(const SHyprionConfig& config, CHyprionAnimation::Time now) {
    const SHyprionBorderConfig* profiles[] = {&config.inset, &config.outset};
    bool running = false;
    for (size_t i = 0; i < m_animations.size(); ++i)
        running = m_animations[i].advance(*profiles[i], now) || running;
    if (running)
        damageBorder();
    return running;
}

void CHyprionDecoration::resetAnimation(bool focused, bool urgent, CHyprionAnimation::Time now) {
    for (auto& animation : m_animations)
        animation.reset(focused, urgent, now);
    damageBorder();
}

SDecorationPositioningInfo CHyprionDecoration::getPositioningInfo() {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    const bool decorated = window && Fullscreen::controller()->getFullscreenModes(window).internal != Fullscreen::FSMODE_FULLSCREEN && window->m_ruleApplicator->decorate().valueOrDefault() &&
        !window->backend().traits().suggestsNoBorder;
    const int extent = decorated && config.enabled && config.outset.enabled ? config.outset.size : 0;
    SDecorationPositioningInfo info;
    info.policy = DECORATION_POSITION_ABSOLUTE;
    info.reserved = extent > 0;
    // Before the native border (10000), so it wraps our outset ring.
    info.priority = 10010;
    info.edges = EDGES;
    info.desiredExtents = {{extent, extent}, {extent, extent}};
    m_outsetExtent = extent;
    return info;
}
void CHyprionDecoration::onPositioningReply(const SDecorationPositioningReply&) {
    damageEntire();
}

CBox CHyprionDecoration::monitorLocalBox(PHLMONITOR monitor, const Render::SWindowRenderPresentation& presentation) const {
    const auto window = m_window.lock();
    if (!window || !monitor)
        return {};

    // Decoration assignments include preceding outer outlines; sample the surface itself.
    CBox box = window->getWindowMainSurfaceBox();
    box.translate(presentation.floatingOffset + presentation.workspaceOffset - monitor->m_position);
    return box;
}
void CHyprionDecoration::updateOrbitArcTable(float power) {
    if (m_orbitArcPower == power)
        return;

    constexpr int tableSize = 64;
    constexpr int subdivisions = 4;
    constexpr double halfPi = 1.57079632679489661923;
    const double exponent = 2.0 / static_cast<double>(power);
    double previousX = 1.0;
    double previousY = 0.0;
    double length = 0.0;
    m_orbitArcTable[0] = 0.F;
    for (int index = 1; index <= tableSize; ++index) {
        for (int subdivision = 1; subdivision <= subdivisions; ++subdivision) {
            const int sample = (index - 1) * subdivisions + subdivision;
            const double theta = halfPi * static_cast<double>(sample) / static_cast<double>(tableSize * subdivisions);
            const double x = std::pow(std::max(std::cos(theta), 0.0), exponent);
            const double y = std::pow(std::max(std::sin(theta), 0.0), exponent);
            length += std::hypot(x - previousX, y - previousY);
            previousX = x;
            previousY = y;
        }
        m_orbitArcTable[static_cast<std::size_t>(index)] = static_cast<float>(length);
    }
    m_orbitArcPower = power;
}


void CHyprionDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR monitor, float const& alpha, const Render::SWindowRenderPresentation& presentation) {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    if (!window || !validMapped(window) || !monitor || !config.enabled || !std::isfinite(alpha) || alpha <= 0.0F)
        return;
    const auto box = monitorLocalBox(monitor, presentation);
    if (box.width <= 0.0 || box.height <= 0.0)
        return;
    // Consume the pre-shadow snapshot before inset rendering reuses the cache.
    if (config.outset.enabled)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box.copy().expand(config.outset.size), true));
    if (config.inset.enabled)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, false));
}

void CHyprionDecoration::drawOutset(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha, const Render::SWindowRenderPresentation& presentation) {
    const auto window = m_window.lock();
    const auto config = hyprionConfig();
    if (!window || !validMapped(window) || !monitor || !config.enabled || !config.outset.enabled || !std::isfinite(alpha) || alpha <= 0.F)
        return;
    auto box = monitorLocalBox(monitor, presentation);
    box.expand(config.outset.size);
    if (box.width > 0.0 && box.height > 0.0)
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CHyprionPassElement>(this, alpha, box, true, true));
}
void CHyprionDecoration::drawPass(Render::CRenderContext& ctx, float alpha, const CBox& monitorLocalBox_, bool outset, bool snapshotOnly) {
    if (snapshotOnly)
        m_outsetSnapshotReady = false;
    else if (outset && !m_outsetSnapshotReady)
        return;
    const auto monitor = ctx.m_data.pMonitor.lock();
    if (!monitor || !g_pHyprRenderer || !g_pHyprOpenGL)
        return;
    if (!shader && !initializeHyprionRenderer())
        return;

    auto& renderData = ctx.m_data;
    const auto window = m_window.lock();
    if (!window || !validMapped(window) || renderData.damage.empty() || !renderData.currentFB)
        return;
    const auto config = hyprionConfig();
    const auto& profile = outset ? config.outset : config.inset;
    if (!config.enabled || !profile.enabled || !std::isfinite(alpha) || alpha <= 0.0F)
        return;
    const auto frameStyle = m_animations[outset ? 1 : 0].style(profile, CHyprionAnimation::Clock::now());
    const auto& style = frameStyle.border;
    const auto& orbit = frameStyle.orbit;
    const bool baseTransformVisible = style.strength > 0.F && (style.lightness != 0.F || style.saturation != 1.F || style.chroma != 0.F || style.hue != 0.F || style.tintStrength > 0.F);
    const bool baseVisible = style.opacity > 0.F && (baseTransformVisible || (outset && config.outsetBlur));
    const bool orbitVisible = orbit.amount > 0.F && orbit.length > 0.F && orbit.strength > 0.F;
    if (!baseVisible && !orbitVisible)
        return;
    const float overallStrength = style.strength;
    const float overallOpacity = std::clamp(style.opacity * alpha, 0.0F, 1.0F);
    const float overallOrbitStrength = orbit.strength;
    const float overallOrbitOpacity = std::clamp(orbit.opacity * alpha, 0.0F, 1.0F);

    const auto framebuffer = renderData.currentFB;
    const auto imageDescription = framebuffer->imageDescription();
    if (!imageDescription.valid() || imageDescription->value().icc.present)
        return;

    const double rawWidth = framebuffer->m_size.x;
    const double rawHeight = framebuffer->m_size.y;
    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || rawWidth < 1 || rawHeight < 1 ||
        rawWidth > std::numeric_limits<int>::max() || rawHeight > std::numeric_limits<int>::max())
        return;
    const int targetWidth = static_cast<int>(rawWidth);
    const int targetHeight = static_cast<int>(rawHeight);
    const int maximumStrip = std::min({targetWidth, targetHeight, std::numeric_limits<int>::max() / 2});
    if (maximumStrip < 1)
        return;

    CBox renderBox = monitorLocalBox_;
    renderBox.scale(monitor->m_scale).round();
    renderData.renderModif.applyToBox(renderBox);
    if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
        return;

    const double monitorScale = monitor->m_scale;
    const double modificationScale = renderData.renderModif.combinedScale();
    if (!std::isfinite(monitorScale) || monitorScale <= 0.0 || !std::isfinite(modificationScale) || modificationScale <= 0.0)
        return;

    const double requestedThickness = std::round(std::round(static_cast<double>(profile.size) * monitorScale) * modificationScale);
    const double boundedThickness = std::clamp(requestedThickness, 1.0, static_cast<double>(maximumStrip));
    const int thickness = static_cast<int>(boundedThickness);

    const double surfaceRounding = std::max(0.0, static_cast<double>(window->presentation().rounding()) * monitorScale - 1.0) * modificationScale;
    const float roundingPower = std::clamp(std::isfinite(window->presentation().roundingPower()) ? window->presentation().roundingPower() : 2.0F, 1.0F, 10.0F);
    // Native borders expand the radius along with the box, with a correction for
    // powers below two (CHyprBorderDecoration::draw).
    const double baseRounding = static_cast<double>(window->presentation().rounding()) * monitorScale * modificationScale;
    const double correction = thickness * (std::sqrt(2.0) - 1.0) * std::max(2.0F - roundingPower, 0.0F);
    const double nativeRounding = outset ? (baseRounding > 0.0 ? baseRounding + thickness - correction : 0.0) : surfaceRounding;
    if (!std::isfinite(nativeRounding))
        return;
    const float rounding = static_cast<float>(std::clamp(nativeRounding, 0.0, std::min(renderBox.width, renderBox.height) * 0.5));
    if (orbitVisible)
        updateOrbitArcTable(roundingPower);

    const double requestedSnapshotExtent = std::ceil(std::max(static_cast<double>(thickness), static_cast<double>(rounding) + 1.0));
    const int snapshotExtent = static_cast<int>(std::clamp(requestedSnapshotExtent, 1.0, static_cast<double>(maximumStrip)));

    const auto defaultDescription = NColorManagement::getDefaultImageDescription();
    if (!defaultDescription.valid() || defaultDescription->value().icc.present)
        return;

    CBox targetBox = renderBox;
    targetBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    targetBox.round();

    // Hyprland's work framebuffer uses the transformed box coordinates directly,
    // as in Hyprland's native outline shader; do not flip Y to output-image coordinates.
    int left = 0, bottom = 0, width = 0, height = 0;
    if (!safeRoundedInt(targetBox.x, left) || !safeRoundedInt(targetBox.y, bottom) || !safeRoundedInt(targetBox.width, width) ||
        !safeRoundedInt(targetBox.height, height) || width <= 0 || height <= 0)
        return;

    CRegion borderRegion = renderData.damage.copy().intersect(renderBox);
    const double borderExtent = static_cast<double>(thickness) + rounding;
    if (renderBox.width > 2.0 * borderExtent && renderBox.height > 2.0 * borderExtent)
        borderRegion.subtract(renderBox.copy().expand(-borderExtent));
    if (renderData.clipBox.width != 0 && renderData.clipBox.height != 0)
        borderRegion.intersect(renderData.clipBox);
    if (borderRegion.empty())
        return;

    // The snapshot is only read where the border is damaged, so only now pick the
    // source: keeps the live blur below off windows that are not being redrawn.
    // Both passes must agree on the source description, or the shared pixel cache
    // is reallocated between them, so the cache framebuffer is the reference for
    // either branch and only the snapshot pass runs the blur.
    auto snapshotFramebuffer = framebuffer;
    SP<IFramebuffer> liveBlurFramebuffer;
    if (outset && config.outsetBlur) {
        // Hyprland's own choice for this window: tiled windows (with
        // new_optimizations) and xray rules read the shared pre-window cache,
        // everything else blurs the framebuffer as it stands.
        const bool sharedCache = outsetUsesSharedBlur(ctx, window);
        snapshotFramebuffer = monitor->resources()->m_blurFB;
        if (!snapshotFramebuffer || !snapshotFramebuffer->getTexture() || snapshotFramebuffer->m_size != framebuffer->m_size)
            return;
        if (snapshotOnly) {
            if (sharedCache) {
                if (monitor->m_blurFBDirty)
                    return;
            } else {
                CRegion blurDamage = renderData.damage.copy().intersect(renderBox);
                liveBlurFramebuffer = g_pHyprRenderer->blurMainFramebuffer(ctx, 1.F, blurDamage);
                if (!liveBlurFramebuffer || !liveBlurFramebuffer->getTexture() || liveBlurFramebuffer->m_size != framebuffer->m_size)
                    return;
            }
        }
    }
    const auto sourceDescription = snapshotFramebuffer->imageDescription();
    if (!sourceDescription.valid() || sourceDescription->value().icc.present)
        return;

    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(snapshotFramebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return;

    glActiveTexture(GL_TEXTURE0);
    const bool snapshotsReady = ensureSnapshots(monitor, snapshotFramebuffer, sourceDescription, targetWidth, targetHeight, snapshotExtent);
    // Allocation can change the bound framebuffer, including on failure.
    framebuffer->bind();
    if (!snapshotsReady)
        return;
    auto& snapshots = monitorSnapshots.at(static_cast<uint64_t>(monitor->m_id));
    if (!outset || snapshotOnly) {
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);

        // The live blur result lives in a recycled monitor work buffer, so read
        // it through a throwaway framebuffer before anything else reuses it.
        GLuint readFramebuffer = glFramebuffer->getFBID();
        GLuint temporaryFramebuffer = 0;
        const auto liveBlurTexture = liveBlurFramebuffer ? liveBlurFramebuffer->getTexture() : nullptr;
        if (liveBlurTexture && liveBlurTexture->m_texID != 0) {
            glGenFramebuffers(1, &temporaryFramebuffer);
            if (temporaryFramebuffer != 0) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, temporaryFramebuffer);
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, liveBlurTexture->m_texID, 0);
                readFramebuffer = temporaryFramebuffer;
            }
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        copyWindowSnapshots(snapshots, targetWidth, targetHeight, left, bottom, width, height, snapshotExtent);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        if (temporaryFramebuffer != 0)
            glDeleteFramebuffers(1, &temporaryFramebuffer);
    }
    if (snapshotOnly) {
        m_outsetSnapshotReady = true;
        return;
    }
    if (outset)
        m_outsetSnapshotReady = false;

    const auto inputSettings = g_pHyprRenderer->getCMSettings(ctx, sourceDescription, defaultDescription);
    const auto outputSettings = g_pHyprRenderer->getCMSettings(ctx, defaultDescription, imageDescription);
    const float reference = inputSettings.dstRefLuminance;
    if (!std::isfinite(reference) || reference <= 0.0F)
        return;

    const auto toDefault = matrixFor(inputSettings.convertMatrix);
    const auto fromDefault = matrixFor(outputSettings.convertMatrix);
    const auto projection = g_pHyprRenderer->projectBoxToTarget(ctx, renderBox);

    auto activeShader = g_pHyprOpenGL->useShader(shader);
    activeShader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
    glUniform1i(uniforms.horizontal, 0);
    glUniform1i(uniforms.vertical, 1);
    glUniform4f(uniforms.windowBox, static_cast<float>(left), static_cast<float>(bottom), static_cast<float>(width), static_cast<float>(height));
    setFloat(uniforms.thickness, static_cast<float>(thickness));
    setInt(uniforms.outset, outset ? 1 : 0);
    setInt(uniforms.snapshotExtent, snapshotExtent);
    setFloat(uniforms.rounding, rounding);
    setFloat(uniforms.innerRounding, outset ? static_cast<float>(baseRounding) : std::max(rounding - thickness, 0.F));
    setFloat(uniforms.roundingPower, roundingPower);
    setFloat(uniforms.strength, overallStrength);
    setFloat(uniforms.opacity, overallOpacity);
    setFloat(uniforms.lightness, style.lightness);
    setFloat(uniforms.saturation, style.saturation);
    setFloat(uniforms.chroma, style.chroma);
    setFloat(uniforms.hue, style.hue);
    if (style.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(style.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.tintChroma, tintChroma[0], tintChroma[1]);
    }
    setFloat(uniforms.tintStrength, style.tintStrength);
    setFloat(uniforms.orbitPhase, orbit.phase);
    setFloat(uniforms.orbitLength, orbit.length);
    setFloat(uniforms.orbitLightness, orbit.lightness);
    setFloat(uniforms.orbitSaturation, orbit.saturation);
    setFloat(uniforms.orbitChroma, orbit.chroma);
    setFloat(uniforms.orbitHue, orbit.hue);
    setFloat(uniforms.orbitStrength, overallOrbitStrength);
    setFloat(uniforms.orbitOpacity, overallOrbitOpacity);
    setFloat(uniforms.orbitAmount, orbit.amount);
    if (orbit.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(orbit.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.orbitTintChroma, tintChroma[0], tintChroma[1]);
    }
    setFloat(uniforms.orbitTintStrength, orbit.tintStrength);
    if (uniforms.cornerArcTable >= 0)
        glUniform1fv(uniforms.cornerArcTable, static_cast<GLsizei>(m_orbitArcTable.size()), m_orbitArcTable.data());
    setInt(uniforms.sourceTF, static_cast<GLint>(inputSettings.sourceTF));
    setInt(uniforms.targetTF, static_cast<GLint>(outputSettings.targetTF));
    if (uniforms.sourceRange >= 0)
        glUniform2f(uniforms.sourceRange, inputSettings.srcTFRange.min, inputSettings.srcTFRange.max);
    if (uniforms.targetRange >= 0)
        glUniform2f(uniforms.targetRange, outputSettings.dstTFRange.min, outputSettings.dstTFRange.max);
    setFloat(uniforms.defaultReference, reference);
    setMat3(uniforms.toDefaultPrimaries, toDefault);
    setMat3(uniforms.fromDefaultPrimaries, fromDefault);

    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->bind();
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(activeShader->getUniformLocation(SHADER_SHADER_VAO));

    const bool previousBlend = glIsEnabled(GL_BLEND);
    g_pHyprOpenGL->blend(outset);
    borderRegion.forEachRect([&](const auto& rect) {
        g_pHyprOpenGL->scissor(ctx, &rect, renderData.transformDamage);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    g_pHyprOpenGL->disableScissor();
    g_pHyprOpenGL->blend(previousBlend);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->unbind();
    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->unbind();
}

bool hyprionDrawInsetGlaze(Render::CRenderContext& ctx, PHLMONITOR monitor, const SHyprionGlazeParams& params) {
    if (!g_pHyprRenderer || !g_pHyprOpenGL || !monitor || !std::isfinite(params.thickness) || params.thickness <= 0.F)
        return false;
    if (!shader && !initializeHyprionRenderer())
        return false;

    auto& renderData = ctx.m_data;
    if (renderData.damage.empty() || !renderData.currentFB)
        return false;

    const auto framebuffer = renderData.currentFB;
    const auto imageDescription = framebuffer->imageDescription();
    if (!imageDescription.valid() || imageDescription->value().icc.present)
        return false;

    const double modificationScale = renderData.renderModif.combinedScale();
    const double requestedThickness = std::round(std::round(static_cast<double>(params.thickness) * monitor->m_scale) * modificationScale);
    const double boundedThickness = std::clamp(requestedThickness, 1.0, 4096.0);
    const int thickness = static_cast<int>(boundedThickness);
    const double surfaceRounding = std::max(0.0, static_cast<double>(params.radius) * monitor->m_scale - 1.0) * modificationScale;
    const float roundingPower = std::clamp(std::isfinite(params.roundingPower) ? params.roundingPower : 2.0F, 1.0F, 10.0F);
    if (!std::isfinite(surfaceRounding) || surfaceRounding < 0.0)
        return false;

    CBox renderBox = params.box;
    renderBox.scale(monitor->m_scale).round();
    renderData.renderModif.applyToBox(renderBox);
    if (renderBox.width <= 0.0 || renderBox.height <= 0.0 || !std::isfinite(renderBox.width) || !std::isfinite(renderBox.height))
        return false;

    const float rounding = static_cast<float>(std::clamp(surfaceRounding, 0.0, std::min(renderBox.width, renderBox.height) * 0.5));
    const double requestedSnapshotExtent = std::ceil(std::max(boundedThickness, static_cast<double>(rounding) + 1.0));
    const int maximumStrip = std::min({static_cast<int>(framebuffer->m_size.x), static_cast<int>(framebuffer->m_size.y), std::numeric_limits<int>::max() / 2});
    if (maximumStrip < 1)
        return false;
    const int snapshotExtent = static_cast<int>(std::clamp(requestedSnapshotExtent, 1.0, static_cast<double>(maximumStrip)));

    CBox targetBox = renderBox;
    targetBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    targetBox.round();

    int left = 0, bottom = 0, width = 0, height = 0;
    if (!safeRoundedInt(targetBox.x, left) || !safeRoundedInt(targetBox.y, bottom) || !safeRoundedInt(targetBox.width, width) ||
        !safeRoundedInt(targetBox.height, height) || width <= 0 || height <= 0)
        return false;

    CRegion borderRegion = renderData.damage.copy().intersect(renderBox);
    const double borderExtent = static_cast<double>(thickness) + rounding;
    if (renderBox.width > 2.0 * borderExtent && renderBox.height > 2.0 * borderExtent)
        borderRegion.subtract(renderBox.copy().expand(-borderExtent));
    if (borderRegion.empty())
        return false;

    const auto defaultDescription = NColorManagement::getDefaultImageDescription();
    if (!defaultDescription.valid() || defaultDescription->value().icc.present)
        return false;
    const auto sourceDescription = framebuffer->imageDescription();
    if (!sourceDescription.valid() || sourceDescription->value().icc.present)
        return false;

    auto* const glFramebuffer = dynamic_cast<CGLFramebuffer*>(framebuffer.get());
    if (!glFramebuffer || glFramebuffer->getFBID() == 0)
        return false;

    glActiveTexture(GL_TEXTURE0);
    if (!ensureSnapshots(monitor, framebuffer, sourceDescription, static_cast<int>(framebuffer->m_size.x), static_cast<int>(framebuffer->m_size.y), snapshotExtent))
        return false;
    framebuffer->bind();
    auto& snapshots = monitorSnapshots.at(static_cast<uint64_t>(monitor->m_id));
    if (!snapshots.horizontal || !snapshots.horizontal->getTexture() || !snapshots.vertical || !snapshots.vertical->getTexture())
        return false;

    GLint previousReadFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, glFramebuffer->getFBID());
    copyWindowSnapshots(snapshots, static_cast<int>(framebuffer->m_size.x), static_cast<int>(framebuffer->m_size.y), left, bottom, width, height,
                        snapshotExtent);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));

    const auto inputSettings = g_pHyprRenderer->getCMSettings(ctx, sourceDescription, defaultDescription);
    const auto outputSettings = g_pHyprRenderer->getCMSettings(ctx, defaultDescription, imageDescription);
    const float reference = inputSettings.dstRefLuminance;
    if (!std::isfinite(reference) || reference <= 0.0F)
        return false;

    const auto toDefault = matrixFor(inputSettings.convertMatrix);
    const auto fromDefault = matrixFor(outputSettings.convertMatrix);
    const auto projection = g_pHyprRenderer->projectBoxToTarget(ctx, renderBox);

    auto activeShader = g_pHyprOpenGL->useShader(shader);
    activeShader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
    glUniform1i(uniforms.horizontal, 0);
    glUniform1i(uniforms.vertical, 1);
    glUniform4f(uniforms.windowBox, static_cast<float>(left), static_cast<float>(bottom), static_cast<float>(width), static_cast<float>(height));
    setFloat(uniforms.thickness, static_cast<float>(thickness));
    setInt(uniforms.outset, 0);
    setInt(uniforms.snapshotExtent, snapshotExtent);
    setFloat(uniforms.rounding, rounding);
    setFloat(uniforms.innerRounding, std::max(rounding - static_cast<float>(thickness), 0.F));
    setFloat(uniforms.roundingPower, roundingPower);
    setFloat(uniforms.strength, params.style.strength);
    setFloat(uniforms.opacity, std::clamp(params.style.opacity * params.alpha, 0.F, 1.F));
    setFloat(uniforms.lightness, params.style.lightness);
    setFloat(uniforms.saturation, params.style.saturation);
    setFloat(uniforms.chroma, params.style.chroma);
    setFloat(uniforms.hue, params.style.hue);
    if (params.style.tintStrength > 0.F) {
        const auto tintChroma = toOklabChroma(params.style.tintColor, inputSettings.convertMatrix);
        glUniform2f(uniforms.tintChroma, tintChroma[0], tintChroma[1]);
    } else
        glUniform2f(uniforms.tintChroma, 0.F, 0.F);
    setFloat(uniforms.tintStrength, params.style.tintStrength);
    setFloat(uniforms.orbitPhase, 0.F);
    setFloat(uniforms.orbitLength, 0.F);
    setFloat(uniforms.orbitLightness, 0.F);
    setFloat(uniforms.orbitSaturation, 1.F);
    setFloat(uniforms.orbitChroma, 0.F);
    setFloat(uniforms.orbitHue, 0.F);
    setFloat(uniforms.orbitStrength, 0.F);
    setFloat(uniforms.orbitOpacity, 0.F);
    setFloat(uniforms.orbitAmount, 0.F);
    glUniform2f(uniforms.orbitTintChroma, 0.F, 0.F);
    setFloat(uniforms.orbitTintStrength, 0.F);
    if (uniforms.cornerArcTable >= 0) {
        const std::array<GLfloat, 65> zeroTable{};
        glUniform1fv(uniforms.cornerArcTable, static_cast<GLsizei>(zeroTable.size()), zeroTable.data());
    }
    setInt(uniforms.sourceTF, static_cast<GLint>(inputSettings.sourceTF));
    setInt(uniforms.targetTF, static_cast<GLint>(outputSettings.targetTF));
    if (uniforms.sourceRange >= 0)
        glUniform2f(uniforms.sourceRange, inputSettings.srcTFRange.min, inputSettings.srcTFRange.max);
    if (uniforms.targetRange >= 0)
        glUniform2f(uniforms.targetRange, outputSettings.dstTFRange.min, outputSettings.dstTFRange.max);
    setFloat(uniforms.defaultReference, reference);
    setMat3(uniforms.toDefaultPrimaries, toDefault);
    setMat3(uniforms.fromDefaultPrimaries, fromDefault);

    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->bind();
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(activeShader->getUniformLocation(SHADER_SHADER_VAO));

    const bool previousBlend = glIsEnabled(GL_BLEND);
    g_pHyprOpenGL->blend(false);
    borderRegion.forEachRect([&](const auto& rect) {
        g_pHyprOpenGL->scissor(ctx, &rect, renderData.transformDamage);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    g_pHyprOpenGL->disableScissor();
    g_pHyprOpenGL->blend(previousBlend);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    snapshots.vertical->getTexture()->unbind();
    glActiveTexture(GL_TEXTURE0);
    snapshots.horizontal->getTexture()->unbind();
    return true;
}

eDecorationType CHyprionDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CHyprionDecoration::updateWindow(PHLWINDOW window) {
    m_window = window;
    // Rebinding also picks up the animation tree a config reload rebuilt.
    for (auto& animation : m_animations)
        animation.bind(window);
    damageEntire();
    const int previousExtent = m_outsetExtent;
    getPositioningInfo();
    if (previousExtent != m_outsetExtent)
        g_pDecorationPositioner->repositionDeco(this);
}

void CHyprionDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (window && validMapped(window) && g_pHyprRenderer)
        g_pHyprRenderer->damageWindow(window);
}
void CHyprionDecoration::damageBorder() {
    const auto window = m_window.lock();
    if (!window || !validMapped(window) || !g_pHyprRenderer)
        return;

    CBox box = window->getWindowMainSurfaceBox();
    box.translate(window->presentation().floatingOffset() + window->presentation().renderPresentation().workspaceOffset);

    const auto config = hyprionConfig();
    const double borderExtent = static_cast<double>(std::max(config.inset.size, config.outset.size)) + static_cast<double>(window->presentation().rounding());
    if (!std::isfinite(borderExtent) || borderExtent <= 0.0 || box.width <= 0.0 || box.height <= 0.0)
        return;

    if (config.enabled && config.outset.enabled)
        box.expand(config.outset.size);
    CRegion borderRegion(box);
    if (box.width > 2.0 * borderExtent && box.height > 2.0 * borderExtent)
        borderRegion.subtract(box.copy().expand(-borderExtent));
    borderRegion.expand(2);
    g_pHyprRenderer->damageRegion(borderRegion);
}

eDecorationLayer CHyprionDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CHyprionDecoration::getDecorationFlags() {
    return DECORATION_PART_OF_MAIN_WINDOW;
}

std::string CHyprionDecoration::getDisplayName() {
    return "hyprion";
}


bool initializeHyprionRenderer() {
    if (shader)
        return true;
    if (!g_pHyprRenderer || g_pHyprRenderer->type() != IHyprRenderer::RT_GL || !g_pHyprOpenGL || !g_pHyprOpenGL->m_eglContext)
        return false;

    g_pHyprOpenGL->makeEGLCurrent();
    auto candidate = makeShared<CShader>();
    if (!candidate->createProgram(VERTEX_SHADER, FRAGMENT_SHADER, true, true)) {
        LOG(Log::ERR, "hyprion: failed to compile the sampling shader");
        return false;
    }

    const auto program = candidate->program();
    uniforms = {
        .horizontal = glGetUniformLocation(program, "uHorizontal"),
        .vertical = glGetUniformLocation(program, "uVertical"),
        .windowBox = glGetUniformLocation(program, "uWindowBox"),
        .thickness = glGetUniformLocation(program, "uThickness"),
        .outset = glGetUniformLocation(program, "uOutset"),
        .snapshotExtent = glGetUniformLocation(program, "uSnapshotExtent"),
        .rounding = glGetUniformLocation(program, "uRounding"),
        .innerRounding = glGetUniformLocation(program, "uInnerRounding"),
        .roundingPower = glGetUniformLocation(program, "uRoundingPower"),
        .strength = glGetUniformLocation(program, "uStrength"),
        .opacity = glGetUniformLocation(program, "uOpacity"),
        .lightness = glGetUniformLocation(program, "uLightness"),
        .saturation = glGetUniformLocation(program, "uSaturation"),
        .chroma = glGetUniformLocation(program, "uChroma"),
        .hue = glGetUniformLocation(program, "uHue"),
        .tintChroma = glGetUniformLocation(program, "uTintChroma"),
        .tintStrength = glGetUniformLocation(program, "uTintStrength"),
        .orbitPhase = glGetUniformLocation(program, "uOrbitPhase"),
        .orbitLength = glGetUniformLocation(program, "uOrbitLength"),
        .orbitLightness = glGetUniformLocation(program, "uOrbitLightness"),
        .orbitSaturation = glGetUniformLocation(program, "uOrbitSaturation"),
        .orbitChroma = glGetUniformLocation(program, "uOrbitChroma"),
        .orbitHue = glGetUniformLocation(program, "uOrbitHue"),
        .orbitStrength = glGetUniformLocation(program, "uOrbitStrength"),
        .orbitOpacity = glGetUniformLocation(program, "uOrbitOpacity"),
        .orbitAmount = glGetUniformLocation(program, "uOrbitAmount"),
        .orbitTintChroma = glGetUniformLocation(program, "uOrbitTintChroma"),
        .orbitTintStrength = glGetUniformLocation(program, "uOrbitTintStrength"),
        .cornerArcTable = glGetUniformLocation(program, "uCornerArcTable[0]"),
        .sourceTF = glGetUniformLocation(program, "uSourceTF"),
        .targetTF = glGetUniformLocation(program, "uTargetTF"),
        .sourceRange = glGetUniformLocation(program, "uSourceRange"),
        .targetRange = glGetUniformLocation(program, "uTargetRange"),
        .defaultReference = glGetUniformLocation(program, "uDefaultReference"),
        .toDefaultPrimaries = glGetUniformLocation(program, "uToDefaultPrimaries"),
        .fromDefaultPrimaries = glGetUniformLocation(program, "uFromDefaultPrimaries"),
    };
    shader = std::move(candidate);
    return true;
}

void destroyHyprionRenderer() {
    if (g_pHyprOpenGL && g_pHyprOpenGL->m_eglContext)
        g_pHyprOpenGL->makeEGLCurrent();
    monitorSnapshots.clear();
    shader.reset();
    uniforms = {};
}

void destroyHyprionMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return;
    const auto it = monitorSnapshots.find(static_cast<uint64_t>(monitor->m_id));
    if (it != monitorSnapshots.end()) {
        if (g_pHyprOpenGL && g_pHyprOpenGL->m_eglContext)
            g_pHyprOpenGL->makeEGLCurrent();
        monitorSnapshots.erase(it);
    }
}
