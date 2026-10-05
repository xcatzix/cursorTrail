# Troubleshooting

## `loadEffect` returns `(false,)`

Confirm both modules were installed under the directory printed by:

```bash
qtpaths6 --plugin-dir
```

Then run `./scripts/verify.sh`.

## Changes compile but the old appearance remains

Qt can keep a native plugin library loaded inside KWin even after unloading the effect. Log out and back in once after replacing the `.so` file. Normal settings changes do not require a logout.

## Ninja says `manifest 'build.ninja' still dirty after 100 tries`

The archive may contain timestamps newer than the local clock. The installer normalizes timestamps automatically. For a manual build:

```bash
find . -path ./build -prune -o -type f -exec touch {} +
rm -rf build
```

## The settings button does not appear

Check that `kwin_cursortrailon_config.so` exists in:

```text
$(qtpaths6 --plugin-dir)/kwin/effects/configs/
```

Then log out and back in.

## Build dependency error

On Fedora 44:

```bash
sudo dnf install \
  gcc-c++ cmake extra-cmake-modules ninja-build \
  qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kconfig-devel kf6-kcmutils-devel \
  kwin-devel libepoxy-devel libdrm-devel
```

## The text caret leaves no trail

- Check the installer/CMake output for "Text caret trail: enabled". If it says the headers were not found, install `kwin-devel` (or the equivalent for your distribution) and re-run `./scripts/install.sh`.
- Only applications that report their caret via the Wayland text-input protocol are tracked. XWayland applications and terminals do not report it.
- Make sure "Show trail when the text caret moves" is ticked in the settings.

## Both WindTrail and cursorTrailOn draw trails

An old WindTrail install is still loaded. Re-run `./scripts/install.sh` (it removes the old modules) and log out and back in once.
