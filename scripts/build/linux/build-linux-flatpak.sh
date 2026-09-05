#!/usr/bin/env bash
# GeneralsX @build GitHubCopilot 13/04/2026 Build Flatpak bundles by compiling inside org.freedesktop.Sdk.
# Usage:
#   ./scripts/build/linux/build-linux-flatpak.sh [preset] [product]
#   product: Arsenal (default)
set -euo pipefail

# GeneralsX @build GitHubCopilot 14/04/2026 Timestamp helper for progress tracking.
ts() { date '+%H:%M:%S'; }
elapsed() {
    local start="$1"
    local end
    end=$(date +%s)
    local diff=$(( end - start ))
    printf '%dm%02ds' $(( diff / 60 )) $(( diff % 60 ))
}

PRESET="${1:-linux64-deploy}"
PRODUCT="${2:-Arsenal}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FLATPAK_DIR="${PROJECT_ROOT}/flatpak"
FLATPAK_BUILD_DIR="${PROJECT_ROOT}/build/flatpak-builddir"
FLATPAK_REPO_DIR="${PROJECT_ROOT}/build/flatpak-repo"
FLATPAK_STATE_DIR="${PROJECT_ROOT}/.flatpak-builder"
RUNTIME_REPO_URL="${RUNTIME_REPO_URL:-https://flathub.org/repo/flathub.flatpakrepo}"
# GeneralsX @build GitHubCopilot 13/04/2026 Optional hard purge for troubleshooting (drops flatpak-builder cache + workdirs).
GENERALS_ARSENAL_FLATPAK_PURGE_CACHE="${GENERALS_ARSENAL_FLATPAK_PURGE_CACHE:-0}"
# GeneralsX @build GitHubCopilot 14/04/2026 Enable flatpak-builder ccache by default for cross-session object reuse.
GENERALS_ARSENAL_FLATPAK_USE_CCACHE="${GENERALS_ARSENAL_FLATPAK_USE_CCACHE:-1}"
# GeneralsArsenal @build Codex 13/08/2026 Permit reproducible rebuilds from the populated source cache during network outages.
GENERALS_ARSENAL_FLATPAK_OFFLINE="${GENERALS_ARSENAL_FLATPAK_OFFLINE:-0}"

case "${PRODUCT}" in
    Arsenal)
        MANIFEST="${FLATPAK_DIR}/io.github.cheviiot.GeneralsArsenal.yml"
        APP_ID="io.github.cheviiot.GeneralsArsenal"
        OUTPUT_BUNDLE="${PROJECT_ROOT}/build/GeneralsArsenal-${PRESET}.flatpak"
        ;;
    *)
        echo "ERROR: Unsupported product '${PRODUCT}'. Use Arsenal." >&2
        exit 1
        ;;
esac

BUILD_START=$(date +%s)
echo "[$(ts)] Building Generals: Arsenal Flatpak (preset label: ${PRESET})"

if [[ ! -f "${MANIFEST}" ]]; then
    echo "ERROR: Missing Flatpak manifest ${MANIFEST}" >&2
    exit 1
fi
if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "ERROR: flatpak-builder is not installed." >&2
    echo "Install with: sudo apt-get install flatpak flatpak-builder" >&2
    exit 1
fi
if ! command -v flatpak >/dev/null 2>&1; then
    echo "ERROR: flatpak is not installed." >&2
    echo "Install with: sudo apt-get install flatpak" >&2
    exit 1
fi

if ! flatpak --user remote-list | awk '{print $1}' | grep -qx "flathub"; then
    echo "[$(ts)] Adding flathub remote for current user..."
    flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
fi

if ! flatpak --user info org.freedesktop.Platform//25.08 >/dev/null 2>&1 || \
   ! flatpak --user info org.freedesktop.Sdk//25.08 >/dev/null 2>&1; then
    echo "[$(ts)] Installing required Flatpak runtime and SDK (25.08) for current user..."
    flatpak --user install -y flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08
fi

if [[ "${GENERALS_ARSENAL_FLATPAK_PURGE_CACHE}" == "1" ]]; then
    echo "[$(ts)] Full purge requested (GENERALS_ARSENAL_FLATPAK_PURGE_CACHE=1): removing Flatpak build dirs and local flatpak-builder cache..."
    rm -rf "${FLATPAK_BUILD_DIR}" "${FLATPAK_REPO_DIR}"
    rm -rf "${FLATPAK_STATE_DIR}"
fi

mkdir -p "${FLATPAK_BUILD_DIR}" "${FLATPAK_REPO_DIR}" "${FLATPAK_STATE_DIR}"

echo "[$(ts)] Running flatpak-builder (build inside SDK sandbox)..."
FLATPAK_BUILDER_START=$(date +%s)
BUILDER_ARGS=(
    --verbose
    --user
    --force-clean
    --state-dir="${FLATPAK_STATE_DIR}"
    --repo="${FLATPAK_REPO_DIR}"
    --install-deps-from=flathub
)

if [[ "${GENERALS_ARSENAL_FLATPAK_OFFLINE}" == "1" ]]; then
    BUILDER_ARGS+=(--disable-download)
    echo "[$(ts)] Flatpak source downloads disabled; using the populated local cache."
fi

if [[ -n "${GENERALS_GIT_OVERRIDE_TAG:-}" ]] || [[ -n "${GENERALS_GIT_OVERRIDE_TSTAMP:-}" ]]; then
    cat > "${PROJECT_ROOT}/.git-override.cmake" <<EOF
set(GENERALS_GIT_OVERRIDE_TAG "${GENERALS_GIT_OVERRIDE_TAG:-}")
set(GENERALS_GIT_OVERRIDE_TSTAMP "${GENERALS_GIT_OVERRIDE_TSTAMP:-0}")
EOF
else
    rm -f "${PROJECT_ROOT}/.git-override.cmake"
fi

if [[ "${GENERALS_ARSENAL_FLATPAK_USE_CCACHE}" == "1" ]]; then
    BUILDER_ARGS+=(--ccache)
    echo "[$(ts)] flatpak-builder ccache enabled (GENERALS_ARSENAL_FLATPAK_USE_CCACHE=1)."
else
    echo "[$(ts)] flatpak-builder ccache disabled (GENERALS_ARSENAL_FLATPAK_USE_CCACHE=0)."
fi

echo "Using flatpak-builder state/cache dir: ${FLATPAK_STATE_DIR}"
echo "Set GENERALS_ARSENAL_FLATPAK_PURGE_CACHE=1 for full purge."
echo "Note: flatpak-builder can stay quiet for a while during finalization/export."
echo "Do not interrupt after the last module command unless an explicit error is shown."

flatpak-builder "${BUILDER_ARGS[@]}" \
    "${FLATPAK_BUILD_DIR}" \
    "${MANIFEST}"

echo "[$(ts)] flatpak-builder completed in $(elapsed "${FLATPAK_BUILDER_START}"). Building final .flatpak bundle..."
BUNDLE_START=$(date +%s)
flatpak build-bundle --runtime-repo="${RUNTIME_REPO_URL}" "${FLATPAK_REPO_DIR}" "${OUTPUT_BUNDLE}" "${APP_ID}"

echo "[$(ts)] .flatpak bundle created in $(elapsed "${BUNDLE_START}") (total: $(elapsed "${BUILD_START}")). Output: ${OUTPUT_BUNDLE}"
echo "Install example:"
echo "  flatpak --user install -y \"${OUTPUT_BUNDLE}\""
