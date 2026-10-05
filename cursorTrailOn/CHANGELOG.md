# Changelog

## 1.2.0 — 2026-10-05

Caret-trail rework and independent pointer/caret settings.

### Changed

- Caret trail starts at the left/right edge of the caret instead of its center,
  so a wide trail no longer covers the text cursor
- Horizontal caret moves are drawn as a simple isosceles triangle (base on the
  caret edge, apex at the previous position)
- Vertical caret moves use a new, wider triangle whose base is the caret trail
  width, replacing the thin hull that looked too narrow
- Caret trail width range is now 1–60 px (default 8 px)
- Pointer trail and caret trail now have fully independent color settings
  (preset, main / center / glow color, outer-glow switch, intensity) and
  independent trail durations
- About box and plugin metadata: cursorTrailOn - Animated Pointer And Cursor
  Trail, author xcatzix

### Added

- Caret trail max height (4–120 px, default 40)
- Caret trail minimum speed (0–3000 px/s, 0 = no limit), like the pointer
  trail's minimum speed
- Caret query interval (4–200 ms, default 16) — how often the caret position
  is polled
- New `kwinrc` keys: `CaretColor`, `CaretCoreColor`, `CaretGlowColor`,
  `CaretGlowEnabled`, `CaretIntensity`, `CaretTrailHeight`, `CaretMinSpeed`,
  `CaretTrailDuration`, `CaretPollInterval`. When absent (settings from 1.1.x)
  the caret trail starts from the pointer-trail colors, intensity and duration

## 1.1.0 — 2026-10-04

Renamed from **WindTrail** to **cursorTrailOn** and extended.

### Added

- Text caret trail: a fading smear follows the caret, using the caret rectangle
  clients report through Wayland text-input v1/v2/v3
- Independent Main, Center and Glow colors (presets set all three)
- "Outer glow" switch to remove the surrounding shadow
- "Trail width" in pixels (2–80) replacing the old Thickness multiplier, plus a
  separate "Caret trail width" (1–24 px)
- Switches to enable the mouse trail and the caret trail independently
- `-DCURSORTRAILON_TEXT_CURSOR=OFF` for a mouse-only build; the build falls back
  automatically (with a warning) if KWin's Wayland headers are not installed
- Installer migrates settings from, and removes, an old WindTrail installation
- Simplified Chinese plugin name/description

### Changed

- Plugin id `proxyx_windtrail` → `cursortrailon`; config module
  `kwin_proxyx_windtrail_config` → `kwin_cursortrailon_config`; settings group
  `Effect-proxyx_windtrail` → `Effect-cursortrailon`
- Source files `windtrail*` → `cursortrailon*`
- Old `Thickness` values are still read (×18 px) when no `TrailWidth` is stored;
  missing Center/Glow colors fall back to the previous derived values, so
  existing color choices look the same

### Fixed

- `uninstall.sh` passed `kwriteconfig6 --delete` without `--key`, so
  `--purge-settings` and un-enabling the effect silently did nothing

### Not yet verified

- 1.1.0 has not been compiled against a real KWin; see TESTING.md

## 1.0.0 — 2026-07-30 (WindTrail)

First stable public release.

### Added

- Continuous, speed-reactive wind-ribbon cursor trail
- Native KWin effect and native Plasma configuration module
- Crimson Slash, Wind White, Ice Blue, and custom-color presets
- Configurable thickness, intensity, duration, activation speed, and smoothing
- Full-screen suppression option
- English public UI and Portuguese documentation
- Multi-monitor support
- Install, uninstall, verification, and diagnostics scripts
- KDE Store listing draft, screenshots, testing report, and troubleshooting guide

### Fixed

- Native modules install into the Qt 6 plugin directory used by KWin
- Installer normalizes archive timestamps to prevent Ninja regeneration loops
- Color alpha is normalized because opacity is controlled by Intensity
- CMake automoc recognizes `KWIN_EFFECT_FACTORY`

### Compatibility

- Minimum KWin version: 6.7
- Validated on Fedora 44 / Plasma 6.7.3 / Wayland / x86_64

## 1.0.0-rc1 — 2026-07-30

Release candidate validated before the stable release.
