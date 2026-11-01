# Hyprion

Hyprion is a Hyprland visual-effects plugin; the sampled border is its first effect.

Rounded inset and outset borders sample the pixels beneath them, transform them in OKLab, and paint the result back over the live border, giving a reactive glaze. Each placement has independent `active`, `inactive`, `pulse`, `tint` and `orbit` settings.

The inset overlays the window surface; the outset occupies reserved space outside it and samples the composited background from before the window's own shadow. Both can be enabled together, and both composite using Hyprland's native border antialiasing. Hyprland's native border is optional: when enabled it wraps the outset — set `general.border_size = 0` to omit it. Native shadow extents and corner radii include the outset.

Requires **Hyprland 0.57.0**, matching development headers, and the OpenGL renderer.

Hyprion is pronounced like **Hyperion**.

Hyprion's protocols are Hyprland-specific. They depend on Hyprland's renderer
and are not intended for other compositors; there are no current plans to
provide equivalents elsewhere. Applications that use them must implement their
own fallback for other compositors.

## Grade primitive protocol

Hyprion owns a plugin-local `hyprion_grade_manager_v1` global. A Wayland client
attaches a grade object to a `wl_surface`, supplies a surface-local box, an
area mode, a source mode and the same OKLab style parameters as the border, and
Hyprion draws the grade while that surface is rendered. The client receives no
pixels and needs no screencopy permission; the compositor samples the exact
framebuffer it already uses for window effects.

A grade is the primitive; inset/outset bands, glazes, shades, tints and washes
are effects built from it. The first implementation advertises the inset area
and fill source on layer-shell surfaces. The client side lives in
[irides](https://github.com/suvikyi/irides): its `Grade` component uses this path
when the global and requested modes are present, and keeps its local
ShaderEffect as the fallback for other compositors.

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
            tint = { color = accent, strength = 1 },
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

These keys exist on every state: `active` and `inactive` for the focused and unfocused border, `pulse` for the one-shot focus pulse's peak, and the four `orbit.*` profiles for the traveling highlight.

- **`lightness`** — additive OKLab lightness, `-1..1`. `0` unchanged, positive brightens.
- **`saturation`** — OKLab chroma multiplier, `0..2`. `0` greyscale, `1` unchanged, `1.2` +20%.
- **`chroma`** — additive OKLab chroma, `-1..1`. Positive adds chroma, negative drains it. Only reshapes chroma the sample already has, so neutrals stay neutral; use `tint` for those.
- **`hue`** — OKLab hue rotation in degrees, `-180..180`. Turns the sampled colours without introducing new ones.
- **`strength`** — transform amount, `0..2`. `1` applies the transform as configured; higher overshoots past it and clips at the gamut edge.
- **`opacity`** — how much of the border covers the pixels beneath it, `0..1`. Fading it lets the backdrop through, so it is what fades the outset's frost; on the inset those pixels are the window's own, so it blends the border toward them.
- **`tint.color`** / **`tint.strength`** — blend the sample's chroma toward `tint.color` in OKLab, keeping its lightness. `strength` is `0..1`: `1` is the tint colour at the sample's lightness, `0` disables tinting. `color` uses Hyprland colour syntax.

`active.tint` and `inactive.tint` follow their state through focus transitions, interpolating colour and strength. `pulse.tint` is an absolute peak target like the rest of the pulse, so setting it equal to the state's tint leaves the tint unchanged.

### Pulse

- **`pulse.enabled`** — run the one-shot focus pulse.
- **`pulse.time`** — pulse duration in milliseconds, `0..2000`.
- **`pulse.lightness`** / **`pulse.saturation`** / **`pulse.chroma`** / **`pulse.hue`** / **`pulse.strength`** / **`pulse.opacity`** — absolute peak targets, reached halfway through the pulse and faded back toward the ongoing active transition.

Setting `pulse.enabled = false`, or a duration of zero, disables the pulse. For a white pulse even over black, set `pulse.lightness = 1.0`, `pulse.saturation = 0.0` and both `strength` values to `1.0`.

### Orbit

- **`orbit.<profile>.enabled`** — run that profile: `active` for the focused window, `inactive` for non-urgent unfocused ones, `urgent` for any urgent window, and `flare` ignites once on focus gain.
- **`orbit.<profile>.clockwise`** — travel clockwise instead of counter-clockwise.
- **`orbit.<profile>.time`** — milliseconds per full lap, `1..120000`.
- **`orbit.<profile>.length`** — highlighted fraction of the perimeter, `0..1`. The segment follows rounded and superellipse corners.
- **`orbit.<profile>.motion.amount`** — perimeter speed variation, `0..0.95`. `0` is uniform travel; larger values strengthen acceleration and deceleration.
- **`orbit.<profile>.motion.cycles`** — integer `1..4`, speed waves per lap.
- **`orbit.flare.fade.start`** / **`orbit.flare.fade.finish`** — *flare only.* Fade the single ignition lap in and out, in milliseconds. `finish` is anchored to the end of the lap, so presence reaches zero exactly as the lap completes; both are scaled down together if they would overrun it.

Each profile also takes the state-colour keys above, as absolute targets blended over the ongoing border transform. Activating a profile cross-fades over the same `border` animation without resetting position; changing `clockwise` or `motion` preserves the current position. `flare` laps exactly once, then hands back to the focused window's profile — or stays gone if that profile is disabled — so it works even with `orbit.active.enabled = false`.

### Defaults

`saturation` is `1`, `hue` is `0` and `opacity` is `1` in every state. `tint.color` is `rgb(33ccff)` — Hyprland's own glow blue — everywhere, and `tint.strength` is `0` for every `inset` state (untinted) and `1` for every `outset` state.

| State | `lightness` | `chroma` | `strength` |
| --- | ---: | ---: | ---: |
| `active` | 0.175 / 0.4 | 0.05 | 1 / 0.85 |
| `inactive` | 0.12 / 0.225 | 0.05 | 1 / 0.75 |
| `pulse` | 0.45 / 0.375 | 0.1 / 0.05 | 1 |
| `orbit.active` | 0.5 / 0.425 | 0.1 / 0.05 | 0.618 / 1 |
| `orbit.urgent` | 0.5 / 0.425 | 0.05 | 0.5 / 1 |
| `orbit.inactive` | 0.5 / 0.425 | 0.1 / 0.05 | 0.618 / 1 |
| `orbit.flare` | 0.5 / 0.475 | 0.1 | 0.75 / 1 |

| Placement | `enabled` | `size` | `blur` |
| --- | --- | ---: | --- |
| `inset` | false | 1 | — |
| `outset` | true | 2 | true |

`pulse.enabled` is `true` in both placements.

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
