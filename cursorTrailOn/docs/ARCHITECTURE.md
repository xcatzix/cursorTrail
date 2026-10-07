# Architecture

cursorTrailOn is made of two native Qt/KDE plugins:

- `cursortrailon.so`: KWin compositing effect
- `kwin_cursortrailon_config.so`: Plasma configuration module

## Mouse trail

The effect receives pointer movement from KWin (`EffectsHandler::mouseChanged`), stores recent position/speed samples, applies Chaikin smoothing, converts the path into variable-width triangle strips, and draws up to three blended OpenGL passes: glow (optional), body, and bright center. Each pass has its own color.

"Trail width" is the maximum width of the body pass in pixels; the glow and center passes scale from it (2.05x and 0.27x).

## Text caret trail

Wayland compositors cannot inspect application windows, so the caret position comes from the text-input protocol. A 16 ms `QTimer` (running only while the caret trail is enabled) reads, through KWin's `SeatInterface`:

1. `focusedTextInputSurface()`
2. the enabled `textInputV1/V2/V3()` object attached to that surface and its `cursorRectangle()` (surface-local)
3. `effects->findWindow(surface)->bufferGeometry().topLeft()` to translate it to global coordinates

This is the same lookup KWin's own input-panel positioning code performs. When the caret rectangle changes, a sample is stored. Each pair of consecutive samples is drawn as the convex hull of the two caret rectangles (the area the caret "swept"), narrowing and fading with age. Focus changes, window moves and disabled text inputs reset the trail so unrelated positions are never joined.

The text-input headers come from `kwin-devel`. The top-level `CMakeLists.txt` checks which of them are installed and defines `CURSORTRAILON_HAVE_TI_V1/V2/V3` accordingly; with none present the effect builds without caret support.

## Settings

Settings are stored in `~/.config/kwinrc` under `Effect-cursortrailon`:

`MouseTrailEnabled`, `TextCaretEnabled`, `Color`, `CoreColor`, `GlowColor`, `GlowEnabled`, `TrailWidth`, `Intensity`, `TrailDuration`, `ActivationSpeed`, `Smoothness`, `DisableInFullscreen` (pointer trail); `CaretColor`, `CaretCoreColor`, `CaretGlowColor`, `CaretGlowEnabled`, `CaretIntensity`, `CaretTrailWidth`, `CaretTrailHeight`, `CaretMinSpeed`, `CaretTrailDuration`, `CaretPollInterval` (caret trail). Both trails also accept `GradientEnabled`, `GradientSmooth`, `GradientStops`, `LightEnabled`, `LightStrength`, `LightRadius`, `HeadLight` (with the `Caret` prefix for the caret trail).

The configuration module asks KWin to call `reconfigureEffect("cursortrailon")` over D-Bus after Apply, so ordinary setting changes do not require rebuilding or restarting the session.
