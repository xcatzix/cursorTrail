# Testing report template

> cursorTrailOn 1.3.0 (gradient, light effect, 400 px settings page) was written without access to a KWin build environment and has not been compiled or run yet. Please fill this in on a real Plasma 6.7+ Wayland session.

- Distribution:
- Architecture:
- Plasma version:
- KWin version:
- Qt version:
- Session type:
- GPU:
- Driver:
- Monitor count / layout:

## Results

- Build: pass / fail
- Install: pass / fail
- Effect loads: pass / fail
- Settings module opens: pass / fail
- Settings apply live: pass / fail
- Multi-monitor movement: pass / fail
- Text caret trail in a GTK app: pass / fail
- Text caret trail in a Qt app: pass / fail
- Text caret trail does not join different windows/fields: pass / fail
- Three colors + glow off apply live: pass / fail
- Trail width / caret trail width apply live: pass / fail
- Upgrade from WindTrail (settings migrated, old modules removed): pass / fail
- Full-screen suppression: pass / fail
- Screen lock/unlock: pass / fail
- Uninstall: pass / fail

## Notes / logs

Use:

```bash
./scripts/verify.sh
journalctl --user -b --no-pager | grep -iE 'cursortrailon|kwin'
```
