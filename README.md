# Hyprion

Hyprion is a Hyprland visual-effects plugin; the sampled border is its first effect.

Rounded inset and outset borders sample the pixels beneath them and apply a colour grade, giving a reactive glaze. SDR grades use OKLab; HDR grades use Jzazbz with absolute luminance. Each placement has independent `active`, `inactive`, `pulse`, `colorize` and `orbit` settings.

The inset overlays the window surface; the outset occupies reserved space outside it and samples the composited background from before the window's own shadow. Both can be enabled together. Coverage is composited in linear light, so edges are not bit-identical to Hyprland's native border antialiasing. Hyprland's native border is optional: when enabled it wraps the outset — set `general.border_size = 0` to omit it. Native shadow extents and corner radii include the outset.

Native window outsets are drawn after the shadow but **before the window's
surfaces**, while retaining their pre-shadow source. The window's rounded
coverage occludes the outset's inner antialiasing fringe. This prevents
backdrop-coloured pinholes inside opaque window corners (fixed in 0.4.1)
without changing the exterior border mask or adding a destination snapshot.
Inset grades still draw over the window; surface-protocol grades retain their
separate post-surface ordering.

Native insets refresh their strip snapshots from the current window
framebuffer immediately before drawing. They do not reuse the outset's
pre-window pixels. Version 0.4.2 fixes a missing inset capture that caused
absent or fragmented edges, especially when no outset had populated the cache.



Requires **Hyprland 0.57.1**, matching development headers, and the OpenGL renderer.
Hyprland's plugin ABI includes the compositor commit and its Aquamarine,
Hyprutils, Hyprgraphics, Hyprcursor and Hyprlang versions, so rebuild after any
Hyprland update (`hyprpm update`) rather than reusing an older library.

Hyprion is pronounced like **Hyperion**.

Hyprion's protocols are Hyprland-specific. They depend on Hyprland's renderer
and are not intended for other compositors; there are no current plans to
provide equivalents elsewhere. Applications that use them must implement their
own fallback for other compositors.

## Grade primitive protocol

Hyprion owns a plugin-local `hyprion_grade_manager_v1` global. A Wayland client
attaches a grade object to a `wl_surface`, supplies a surface-local box, an
area, a mode, and style parameters, and Hyprion draws the grade immediately
after that surface, before higher surfaces in the scene. The client receives
no pixels and needs no screencopy permission.

A grade is the primitive; inset/outset bands, glazes, shades, colorizations and washes
are effects built from it. The implementation advertises the inset, outset and
fill areas, plus `in_place` and `over` modes, on layer-shell surfaces, ordinary
Wayland windows, subsurfaces and popups. Colorize
targets use a separate `set_colorize_gradient` request with up to ten sRGB
stops and an angle in degrees; a single stop is a flat color. `in_place`
applies the transform to the sampled pixels and preserves their alpha; `over`
composites the graded sample with premultiplied source-over and coverage alpha.
On an opaque framebuffer they coincide for the same coverage. Blur is requested
separately through `ext-background-effect-v1`; the grade samples the composited
result. The client side lives in [irides](https://github.com/suvikyi/irides): its
`Grade` component uses this path when the global and requested area/mode are
present, and keeps its local ShaderEffect as the fallback for other compositors.
That fallback is SDR-only.

Grades sample the current composite: opaque client pixels, translucent client
pixels blended with their backdrop, or the backdrop alone in transparent
space. An outset is outside its **box**, not outside its owning `wl_surface`;
provide transparent surface padding for an exterior border. Clipping never
changes the original shape geometry.

Grades on one surface execute in creation order and overlapping grades sample
earlier results. Within one grade object, selected areas execute as fill,
outset, then inset. A parent surface's grade precedes its higher subsurfaces;
it does not act as an overlay over the completed surface tree. Items flattened
into one Qt surface cannot be interleaved with compositor effects individually.

Snapshots copy only conservative mask regions intersecting the current
damage. Disjoint requests share a capture; overlapping requests introduce a
new capture boundary. Reusable compact scratch pages hold the copied
rectangles, with separate pools for horizontal bands, vertical bands and fills.
Pages retain their peak capacity until the monitor is removed or the plugin
is unloaded. A large fill therefore costs proportionally more than a thin
border; there is no compulsory full-screen scene copy or cursor-driven redraw.
Hyprland's normal damage reconstruction and occlusion pruning remain enabled.

### Migration from 0.3.x to 0.4.0

Root-layer grades no longer overlay the whole subsurface tree, and overlapping
grades on the same surface no longer use an independent shared source. Attach
a grade to the surface that should own it, and create overlapping grades in
the intended effect order. Existing inset/outset/fill settings and protocol
requests are unchanged.


## Install

Add the published Git repository, then enable Hyprion:

```sh
hyprpm add https://github.com/suvikyi/hyprion
hyprpm enable hyprion
```

Hyprpm builds the plugin against headers matching your Hyprland installation; run `hyprpm update` to refresh them.

### Manual build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The output is `build/libhyprion.so`. A manual build does not register the plugin with Hyprpm.

## Configure

Everything registers under `plugin.hyprion`. `inset` and `outset` are independent — neither inherits from the other, and anything you leave out keeps its shipped default.

```lua
hl.config({
  plugin = {
    hyprion = {
      border = {
        outset = {
          enabled = true,
          size = 2,
          active = {
            lightness = 0.4,
            colorize = { gradient = { colors = { accent }, angle = 0 }, strength = 1 },
          },
        },
      },
    },
  },
})
```

## Settings

Defaults are written `inset / outset` where the two placements differ.

### Placement

- **`enabled`** — draw this border.
- **`size`** — border thickness in logical pixels, `0..20`. `0` disables the placement like `enabled = false`.
- **`blur`** — *outset only.* Take the backdrop from Hyprland's blur, from the same source Hyprland picks for that window: with `decoration.blur.new_optimizations` a tiled window reads the shared pre-window cache (desktop and layers, without application windows or the window's own shadow), otherwise the framebuffer is blurred live, including the windows underneath. Requires `decoration.blur.enabled`; `xray` rules are honoured.

Focus transitions and orbit crossfades are timed by Hyprland's own `border` animation, the same one the native border colour fade uses, so they take its curve and speed from `animation = border, ...` and become instant when that animation is off or `animations:enabled = false`. A transition starts from the currently displayed style, so switching focus mid-transition or mid-pulse never snaps to a preset.

### State colour

These keys exist on every state: `active` and `inactive` for the active and inactive border, `pulse.active`, `pulse.inactive` and `pulse.urgent` for one-shot state pulses, and the four `orbit.*` profiles for the traveling highlight.

- **`lightness`** — additive perceptual lightness, `-1..1`. `0` unchanged, positive brightens. The grade space is OKLab on SDR and Jzazbz on HDR; HDR lightness is normalised to the 80-nit SDR reference white so the same numeric offset gives a comparable lift.
- **`saturation`** — chroma multiplier in the grade space, `0..2`. `0` greyscale, `1` unchanged, `1.2` +20%.
- **`chroma`** — additive chroma in the grade space, `-1..1`. Positive adds chroma, negative drains it. Only reshapes chroma the sample already has, so neutrals stay neutral; use `colorize` for those.
- **`hue`** — hue rotation in degrees, `-180..180`. Turns the sampled colours without introducing new ones.
- **`opacity`** — how much of the border covers the pixels beneath it, `0..1`. Fading it lets the backdrop through, so it is what fades the outset's frost; on the inset those pixels are the window's own, so it blends the border toward them.
- **`colorize.gradient`** — native Hyprland gradient syntax: a colour string or `{ colors = { ... }, angle = degrees }`. Stops are uniformly spaced, up to 10.
- **`colorize.strength`** — how much of the sampled chroma is blended toward the gradient, `0..1`. `0` disables colorizing; `1` replaces the chroma target at the sampled position. Lightness is preserved.

`active.colorize` and `inactive.colorize` follow their state through transitions, interpolating gradient stops and strength. `pulse.<state>.colorize` is an absolute pulse target.

### Pulse

Pulse profiles are one-shot state-entry overlays:

- **`pulse.active`** — fires when the window becomes active.
- **`pulse.inactive`** — fires when it becomes inactive.
- **`pulse.urgent`** — fires when the urgent hint becomes true.

Each profile has:

- **`enabled`** — enable this pulse profile.
- **`time`** — pulse duration in milliseconds, `0..2000`.
- **`interval`** — repeat interval in milliseconds, `0..120000`. `0` is one-shot; greater values repeat while the corresponding state holds. Values below `time` are clamped to `time`.
- **`lightness`** / **`saturation`** / **`chroma`** / **`hue`** / **`opacity`** / **`colorize`** — absolute peak targets, reached halfway through the pulse and faded back toward the ongoing active transition.

Setting a profile's `enabled = false`, or its duration to zero, disables that pulse. For a white pulse even over black, set `lightness = 1.0` and `saturation = 0.0`.

### Orbit

- **`orbit.<profile>.enabled`** — run that profile: `active` for the active window, `inactive` for non-urgent inactive ones, `urgent` for any urgent window, and `flare` ignites once on active gain.
- **`orbit.<profile>.clockwise`** — travel clockwise instead of counter-clockwise.
- **`orbit.<profile>.time`** — milliseconds per full lap, `1..120000`.
- **`orbit.<profile>.length`** — highlighted fraction of the perimeter, `0..1`. The segment follows rounded and superellipse corners.
- **`orbit.<profile>.motion.amount`** — perimeter speed variation, `0..0.95`. `0` is uniform travel; larger values strengthen acceleration and deceleration.
- **`orbit.<profile>.motion.cycles`** — integer `1..4`, speed waves per lap.
- **`orbit.flare.fade.start`** / **`orbit.flare.fade.finish`** — *flare only.* Fade the single ignition lap in and out, in milliseconds. `finish` is anchored to the end of the lap, so presence reaches zero exactly as the lap completes; both are scaled down together if they would overrun it.

Each profile also takes the state-colour keys above, as absolute targets blended over the ongoing border transform. Activating a profile cross-fades over the same `border` animation without resetting position; changing `clockwise` or `motion` preserves the current position. `flare` laps exactly once, then hands back to the active window's profile — or stays gone if that profile is disabled — so it works even with `orbit.active.enabled = false`.

### Defaults

`saturation` is `1`, `hue` is `0` and `opacity` is `1` in every state. `colorize.gradient` is a single stop of `rgb(33ccff)` — Hyprland's own glow blue — everywhere, and `colorize.strength` is `0` for every `inset` state (colorize disabled); the outset values are listed below.

| State | `lightness` | `chroma` | `colorize.strength` |
| --- | ---: | ---: | ---: |
| `active` | 0.175 / 0.34 | 0.05 / 0.0425 | 0 / 0.85 |
| `inactive` | 0.12 / 0.16875 | 0.05 / 0.0375 | 0 / 0.75 |
| `pulse.active` | 0.45 / 0.375 | 0.1 / 0.05 | 0 / 1 |
| `orbit.active` | 0.309 / 0.425 | 0.0618 / 0.05 | 0 / 1 |
| `orbit.urgent` | 0.25 / 0.425 | 0.025 / 0.05 | 0 / 1 |
| `orbit.inactive` | 0.309 / 0.425 | 0.0618 / 0.05 | 0 / 1 |
| `orbit.flare` | 0.375 / 0.475 | 0.075 / 0.1 | 0 / 1 |

| Placement | `enabled` | `size` | `blur` |
| --- | --- | ---: | --- |
| `inset` | false | 1 | — |
| `outset` | true | 2 | true |

`pulse.active.enabled` is `true` in both placements; `pulse.inactive` and `pulse.urgent` are disabled.

| Orbit profile | `enabled` | `time` | `length` | `motion.amount` | `motion.cycles` |
| --- | --- | ---: | ---: | ---: | ---: |
| `orbit.active` | true | 30000 | 0.154 | 0.75 / 0.5 | 2 |
| `orbit.urgent` | true | 3000 | 0.154 | 0.75 / 0.5 | 2 |
| `orbit.inactive` | false | 30000 | 0.154 | 0.75 / 0.5 | 2 |
| `orbit.flare` | true | 667 | 0.309 | 0.75 / 0.618 | 2 |

`clockwise` is `true` in every orbit profile, and `orbit.flare.fade.start` and `fade.finish` are both `0`.

## Credits

[Suvikyi](https://github.com/suvikyi)  
GPT-6.1 Sol  
GPT-6 Luna
