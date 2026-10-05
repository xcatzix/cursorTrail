-- Author: xcatzix  
-- mailto: 3949745980@qq.com  
-- Version: test-v-1.1.0.0  
-- Using it in paying money....  
-- cursorTrail or cursorTrailOn based on windTrail,The README was only slightly modified....  
-- cursorTrailOn added text cursor trail, see effectTrailOn.jpg; settup in setupTrailOn.png....  
-- 基本可以正常使用, glow color由于种种原因,现在不能自主修改, 自行让AI修改吧.  

### Installation  
-- Downloading , unzipping and then:
```terminal
>$cd /whEre/yOur/file/downLoaDed/cursorTrailOn
>$sudo cmake --install build
```
or   
```terminal
>$cd /whEre/yOur/file/downLoaDed/cursorTrailOn
>$rm -rf ./build
>$cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
>$cmake --build build -j"$(nproc)"
>$sudo cmake --install build
```
### Uninstallation  
```terminal
>$sudo rm -rf /usr/lib/qt6/plugins/kwin/effects/plugins/cursortrailon.so
>$sudo rm -rf /usr/lib/qt6/plugins/kwin/effects/configs/kwin_cursortrailon_config.so
```

## detail below  

###  Source homepage: wesleyyach/windtrail 

## cursorTrailOn

**cursorTrailOn** is a native KWin effect that draws a smooth, speed-reactive trail behind the **mouse pointer** and behind the **text caret** on KDE Plasma Wayland.

It is a fork of [WindTrail](https://github.com/wesleyyach/windtrail) by Wesley (ProxyX), renamed and extended (see [CHANGELOG](CHANGELOG.md)).

Unlike classic mouse trails that repeat cursor images, cursorTrailOn generates a continuous curved ribbon whose width and lifetime respond to movement speed.

### Preview

![cursorTrailOn demo](media/cursortrailon-demo.gif)

### Features

- Continuous curved ribbon instead of repeated cursor images
- **Text caret trail**: the caret leaves a fading smear when it moves while typing, navigating or clicking
- **Three independent colors** (main, center, glow, or no glow) — set separately for the pointer trail and for the caret trail
- **Adjustable trail width** for the mouse ribbon and for the caret trail
- Crimson Slash, Wind White, Ice Blue and custom-color presets
- Adjustable intensity, duration, activation speed and smoothing
- Mouse trail and caret trail can be switched on/off independently
- Native preview and configuration button in Plasma's Desktop Effects page
- Optional automatic disablement in full-screen apps and games
- Multi-monitor support
- No network access, telemetry, or background service

### Text caret trail: how it works and its limits

Wayland does not let a compositor see inside an application, so cursorTrailOn uses the one thing clients do tell it: the caret rectangle reported through the Wayland **text-input** protocol (v1/v2/v3, the same data KWin uses to place input-method popups). Consequently:

- It works with applications that talk text-input to the compositor, e.g. GTK and Qt text fields and Chromium/Electron apps running natively on Wayland.
- It does **not** track XWayland applications, or terminals/editors that draw their own caret without reporting it. For those, only the mouse trail applies.
- The effect polls the caret (every 16 ms by default; configurable with "Caret query interval"), but only while the caret trail is enabled.
- The caret trail starts at the left/right (or top/bottom) edge of the caret and never covers it: horizontal moves draw an isosceles triangle, vertical moves a wider triangle whose base is the caret trail width.
- It needs KWin's Wayland headers from `kwin-devel`. If CMake cannot find them it builds **without** caret support and prints a warning (the installer repeats it). Force a mouse-only build with `-DCURSORTRAILON_TEXT_CURSOR=OFF`.

### Compatibility

- KDE Plasma / KWin **6.7 or newer**
- Wayland session
- OpenGL compositing
- Linux

The original WindTrail 1.0.0 was validated on Fedora 44 / Plasma 6.7.3. **cursorTrailOn 1.2.0 has not been compiled or run on a real KWin yet** — see [TESTING.md](TESTING.md). Native KWin plugins are linked against KWin and may need recompilation after KWin upgrades.

### Install from source

Extract the archive, then run:

```bash
cd cursorTrailOn
./scripts/install.sh
```

On Fedora, install the required development packages with:

```bash
sudo dnf install \
  gcc-c++ cmake extra-cmake-modules ninja-build \
  qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kconfig-devel kf6-kcmutils-devel \
  kwin-devel libepoxy-devel libdrm-devel
```

The installer builds the effect, installs both native plugin modules into the Qt 6 plugin directory, enables cursorTrailOn, and reports whether a logout/login is required.

**Upgrading from WindTrail:** the installer detects an old WindTrail install, copies its settings (Thickness is converted to the new Trail width), and removes the old modules so the two don't both draw a trail. Set `CURSORTRAILON_KEEP_LEGACY=1` to keep the old modules.

### Configure

Open:

**System Settings → Apps & Windows → Window Management → Desktop Effects → cursorTrailOn**

Then click the configuration button.

| Setting | Meaning |
| --- | --- |
| Trail width | Maximum width of the mouse ribbon, 2–80 px |
| Trail length / duration | How long the pointer trail lasts |
| Minimum speed | Pointer slower than this draws no trail |
| Caret trail width | Width of the trail for vertical caret moves, 1–60 px |
| Caret trail max height | Maximum height of the trail for horizontal caret moves, 4–120 px |
| Caret minimum speed | Caret moves slower than this draw no trail (0 = no limit) |
| Caret trail length / duration | How long the caret trail lasts (independent of the pointer) |
| Caret query interval | How often the caret position is polled, 4–200 ms |
| Main / Center / Glow color, Outer glow, Intensity | Provided **separately** for the pointer trail and the caret trail |

Settings are stored in `~/.config/kwinrc` under `[Effect-cursortrailon]`.

### Verify installation

```bash
./scripts/verify.sh
```

### Uninstall

```bash
./scripts/uninstall.sh
```

To remove saved settings too:

```bash
./scripts/uninstall.sh --purge-settings
```

### Building manually

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

### Reporting bugs

Include the output of:

```bash
./scripts/verify.sh
journalctl --user -b --no-pager | grep -iE 'cursortrailon|kwin'
```
