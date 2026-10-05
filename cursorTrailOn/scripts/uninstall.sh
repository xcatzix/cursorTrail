#!/usr/bin/env bash
set -Eeuo pipefail

readonly EFFECT_ID="cursortrailon"
readonly CONFIG_ID="kwin_cursortrailon_config"

command -v qtpaths6 >/dev/null 2>&1 || { echo "qtpaths6 not found" >&2; exit 1; }
command -v kwriteconfig6 >/dev/null 2>&1 || { echo "kwriteconfig6 not found" >&2; exit 1; }

purge_settings=false
if [[ "${1:-}" == "--purge-settings" ]]; then
    purge_settings=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--purge-settings]" >&2
    exit 2
fi

gdbus call --session \
    --dest org.kde.KWin \
    --object-path /Effects \
    --method org.kde.kwin.Effects.unloadEffect \
    "$EFFECT_ID" >/dev/null 2>&1 || true

qt_plugin_dir="$(qtpaths6 --plugin-dir)"
sudo rm -f \
    "${qt_plugin_dir}/kwin/effects/plugins/${EFFECT_ID}.so" \
    "${qt_plugin_dir}/kwin/effects/configs/${CONFIG_ID}.so"

kwriteconfig6 --file kwinrc --group Plugins \
    --key "${EFFECT_ID}Enabled" --delete || true

if $purge_settings; then
    for key in Color CoreColor GlowColor GlowEnabled MouseTrailEnabled TextCaretEnabled \
               TrailWidth CaretTrailWidth Thickness Intensity TrailDuration \
               ActivationSpeed Smoothness DisableInFullscreen \
               CaretColor CaretCoreColor CaretGlowColor CaretGlowEnabled \
               CaretIntensity CaretTrailHeight CaretMinSpeed \
               CaretTrailDuration CaretPollInterval; do
        kwriteconfig6 --file kwinrc \
            --group "Effect-${EFFECT_ID}" \
            --key "$key" --delete || true
    done
fi

gdbus call --session \
    --dest org.kde.KWin \
    --object-path /KWin \
    --method org.kde.KWin.reconfigure \
    >/dev/null 2>&1 || true

echo "cursorTrailOn removed."
$purge_settings && echo "Saved settings were removed too."
