# cursorTrailOn 1.1 release checklist

(Items below are carried over from WindTrail 1.0; re-verify them for 1.1.)

## Build

- [ ] Clean configure completes without cursorTrailOn-specific warnings
- [ ] Both `.so` modules build
- [ ] Install goes to the Qt 6 plugin directory
- [ ] Install script works from a freshly extracted archive
- [ ] Uninstall script tested from a clean installation

## Runtime

- [ ] Effect loads after a clean login
- [ ] Wind White preset works
- [ ] Crimson Slash preset works
- [ ] Ice Blue preset works
- [ ] Custom color works
- [ ] Apply updates settings without recompilation
- [ ] Defaults button restores defaults
- [ ] Full-screen disablement tested across multiple applications
- [ ] Locking/unlocking tested for artifacts
- [ ] Cross-monitor behavior formally recorded
- [ ] No visible trail remains after the cursor stops

## Publication

- [ ] Create public repository at the new repository
- [ ] Upload this source tree
- [ ] Create GitHub release `v1.1.0`
- [ ] Add two screenshots
- [ ] Capture one or two GIF demonstrations
- [ ] Create final `1.1.0` archives and SHA-256 checksums
- [ ] Publish KDE Store entry under KWin Effects

## 1.1 additions

- [ ] Builds against the installed KWin (with and without `-DCURSORTRAILON_TEXT_CURSOR=OFF`)
- [ ] Caret trail works in at least one GTK and one Qt Wayland app
- [ ] Main/Center/Glow colors and "Outer glow" apply live
- [ ] Trail width and caret trail width apply live
- [ ] Upgrade from WindTrail migrates settings
