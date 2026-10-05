#!/usr/bin/env bash
set -Eeuo pipefail

readonly EFFECT_ID="cursortrailon"
readonly CONFIG_ID="kwin_cursortrailon_config"

# Pre-rename identifiers (the project used to be called WindTrail).
readonly LEGACY_EFFECT_ID="proxyx_windtrail"
readonly LEGACY_CONFIG_ID="kwin_proxyx_windtrail_config"
readonly ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly BUILD_DIR="${ROOT_DIR}/build"

die() {
    printf 'cursorTrailOn installer: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

for command_name in cmake ninja qtpaths6 kwriteconfig6 kreadconfig6 gdbus sudo; do
    require_command "$command_name"
done

printf '== cursorTrailOn 1.2.0 ==\n\n'

plasma_version="$(plasmashell --version 2>/dev/null | awk '{print $2}' || true)"
kwin_version="$(kwin_wayland --version 2>/dev/null | awk '{print $2}' || true)"
printf 'Plasma: %s\n' "${plasma_version:-unknown}"
printf 'KWin:   %s\n\n' "${kwin_version:-unknown}"

# Release archives can preserve timestamps from another machine. Normalizing
# them prevents Ninja's "manifest still dirty" regeneration loop.
find "$ROOT_DIR" -path "$BUILD_DIR" -prune -o -type f -exec touch {} +
rm -rf "$BUILD_DIR"

printf 'Configuring...\n'
if ! cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr; then
    cat >&2 <<'EOF'

CMake could not find all development dependencies.

Fedora 44:
  sudo dnf install gcc-c++ cmake extra-cmake-modules ninja-build \
    qt6-qtbase-devel kf6-kcoreaddons-devel kf6-kconfig-devel \
    kf6-kcmutils-devel kwin-devel libepoxy-devel libdrm-devel
EOF
    exit 1
fi

if grep -q '^CURSORTRAILON_TEXT_INPUT_ENABLED:INTERNAL=ON' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    printf '\nText caret trail: enabled\n'
else
    printf '\nText caret trail: NOT available in this build (KWin text-input headers\n'
    printf 'were not found); only the mouse pointer trail will work.\n'
fi

printf '\nBuilding...\n'
cmake --build "$BUILD_DIR" -j"$(nproc)"

qt_plugin_dir="$(qtpaths6 --plugin-dir)"
effect_path="${qt_plugin_dir}/kwin/effects/plugins/${EFFECT_ID}.so"
config_path="${qt_plugin_dir}/kwin/effects/configs/${CONFIG_ID}.so"
backup_dir="${HOME}/.local/share/cursortrailon/backups/$(date +%Y%m%d-%H%M%S)"

if [[ -f "$effect_path" || -f "$config_path" ]]; then
    mkdir -p "$backup_dir"
    [[ -f "$effect_path" ]] && cp -a "$effect_path" "$backup_dir/"
    [[ -f "$config_path" ]] && cp -a "$config_path" "$backup_dir/"
    printf '\nBacked up previous modules to:\n  %s\n' "$backup_dir"
fi

migrate_legacy_install() {
    local legacy_effect="${qt_plugin_dir}/kwin/effects/plugins/${LEGACY_EFFECT_ID}.so"
    local legacy_config="${qt_plugin_dir}/kwin/effects/configs/${LEGACY_CONFIG_ID}.so"
    local group_old="Effect-${LEGACY_EFFECT_ID}"
    local group_new="Effect-${EFFECT_ID}"
    local found=false

    [[ -f "$legacy_effect" || -f "$legacy_config" ]] && found=true
    [[ -n "$(kreadconfig6 --file kwinrc --group "$group_old" --key Color 2>/dev/null)" ]] && found=true
    $found || return 0

    printf '\nFound a previous WindTrail installation (renamed to cursorTrailOn).\n'

    # Copy old settings once, without overwriting anything already set.
    local key value
    for key in Color Intensity TrailDuration ActivationSpeed Smoothness DisableInFullscreen; do
        value="$(kreadconfig6 --file kwinrc --group "$group_old" --key "$key" 2>/dev/null || true)"
        if [[ -n "$value" && -z "$(kreadconfig6 --file kwinrc --group "$group_new" --key "$key" 2>/dev/null || true)" ]]; then
            kwriteconfig6 --file kwinrc --group "$group_new" --key "$key" "$value"
        fi
    done
    value="$(kreadconfig6 --file kwinrc --group "$group_old" --key Thickness 2>/dev/null || true)"
    if [[ -n "$value" && -z "$(kreadconfig6 --file kwinrc --group "$group_new" --key TrailWidth 2>/dev/null || true)" ]]; then
        kwriteconfig6 --file kwinrc --group "$group_new" --key TrailWidth \
            "$(awk -v t="$value" 'BEGIN { printf "%.0f", t * 18 }')"
    fi
    printf '  Settings copied to [%s] in kwinrc.\n' "$group_new"

    if [[ "${CURSORTRAILON_KEEP_LEGACY:-0}" == "1" ]]; then
        printf '  CURSORTRAILON_KEEP_LEGACY=1: old WindTrail modules left in place.\n'
        return 0
    fi

    gdbus call --session \
        --dest org.kde.KWin \
        --object-path /Effects \
        --method org.kde.kwin.Effects.unloadEffect \
        "$LEGACY_EFFECT_ID" >/dev/null 2>&1 || true
    sudo rm -f "$legacy_effect" "$legacy_config"
    kwriteconfig6 --file kwinrc --group Plugins \
        --key "${LEGACY_EFFECT_ID}Enabled" --delete 2>/dev/null || true
    printf '  Old WindTrail modules removed (set CURSORTRAILON_KEEP_LEGACY=1 to keep them).\n'
}

loaded_before="$(gdbus call --session \
    --dest org.kde.KWin \
    --object-path /Effects \
    --method org.kde.kwin.Effects.isEffectLoaded \
    "$EFFECT_ID" 2>/dev/null || true)"

migrate_legacy_install

printf '\nInstalling...\n'
sudo cmake --install "$BUILD_DIR"

[[ -f "$effect_path" ]] || die "effect module was not installed at $effect_path"
[[ -f "$config_path" ]] || die "configuration module was not installed at $config_path"

kwriteconfig6 --file kwinrc --group Plugins \
    --key "${EFFECT_ID}Enabled" true

gdbus call --session \
    --dest org.kde.KWin \
    --object-path /KWin \
    --method org.kde.KWin.reconfigure \
    >/dev/null 2>&1 || true

printf '\nInstalled files:\n  %s\n  %s\n' "$effect_path" "$config_path"

if [[ "$loaded_before" == "(true,)" ]]; then
    cat <<'EOF'

cursorTrailOn was already loaded before the upgrade.
Log out and back in once so KWin loads the new native library.
Your current session can continue safely until then.
EOF
else
    load_result="$(gdbus call --session \
        --dest org.kde.KWin \
        --object-path /Effects \
        --method org.kde.kwin.Effects.loadEffect \
        "$EFFECT_ID" 2>/dev/null || true)"
    printf '\nKWin load result: %s\n' "${load_result:-unavailable}"
    if [[ "$load_result" != "(true,)" ]]; then
        printf 'Log out and back in once to finish loading cursorTrailOn.\n'
    fi
fi

printf '\nDone. Run ./scripts/verify.sh to inspect the installation.\n'
