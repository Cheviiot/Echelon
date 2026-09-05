#!/usr/bin/env bash
# Verify that the windowless launcher supervisor replaces an abruptly terminated UI worker.
# The complete test runs inside an isolated headless Wayland compositor.
#
# Usage:
#   ./scripts/qa/smoke/test-generals-arsenal-supervisor-wayland.sh [BUILD_PRESET]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
launcher="${repo_root}/build/${build_preset}/GeneralsArsenal/GeneralsArsenal"
dxvk_dir="${repo_root}/build/${build_preset}/_deps/dxvk-src/lib"

if [[ -n "${WAYLAND_DISPLAY:-}" ]] || [[ -n "${DISPLAY:-}" ]]; then
	echo "INFO: Host display variables will not be forwarded to the supervisor test"
fi
if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
if ! command -v mutter >/dev/null 2>&1; then
	echo "ERROR: Mutter is required for the isolated Wayland test" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/generals-arsenal-supervisor.XXXXXX)"
qa_runtime="${qa_root}/runtime"
qa_home="${qa_root}/home"
log_file="${qa_root}/supervisor.log"
mkdir -p "${qa_runtime}" "${qa_home}/.GeneralsArsenal/Profiles" \
	"${qa_home}/.GeneralsArsenal/UserData/Generals" "${qa_home}/.GeneralsArsenal/UserData/GeneralsZH" \
	"${qa_home}/.GeneralsArsenal/Launcher" "${qa_home}/.GeneralsArsenal/Mods" \
	"${qa_home}/.GeneralsArsenal/Generals" "${qa_home}/.GeneralsArsenal/GeneralsZH"
chmod 700 "${qa_runtime}"

# shellcheck disable=SC2016 # Variables are intentionally expanded by the nested shell.
qa_root="${qa_root}" launcher="${launcher}" dxvk_dir="${dxvk_dir}" dbus-run-session -- bash -c '
	set -euo pipefail
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_RUNTIME_DIR="${qa_root}/runtime"
	mutter --headless --wayland --no-x11 --wayland-display=arsenal-supervisor-wl --virtual-monitor 1280x800 \
		>"${qa_root}/mutter.log" 2>&1 &
	compositor_pid=$!
	trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
	for _ in $(seq 1 100); do
		[[ -S "${XDG_RUNTIME_DIR}/arsenal-supervisor-wl" ]] && break
		sleep 0.05
	done
	[[ -S "${XDG_RUNTIME_DIR}/arsenal-supervisor-wl" ]]
	env HOME="${qa_root}/home" WAYLAND_DISPLAY=arsenal-supervisor-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
		DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
		LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" timeout 120s "${launcher}" \
		--launcher --internal-test-supervisor-recovery >"${qa_root}/supervisor.log" 2>&1
'

if ! rg -q 'supervisor test is terminating the initial UI worker abruptly' "${log_file}"; then
	echo "ERROR: Initial worker termination marker is missing" >&2
	tail -n 120 "${log_file}" >&2
	exit 1
fi
if ! rg -q 'UI worker exited with code -[0-9]+; restarting the launcher' "${log_file}"; then
	echo "ERROR: Supervisor did not recognize the abrupt worker termination" >&2
	tail -n 120 "${log_file}" >&2
	exit 1
fi
if ! rg -q 'supervisor recovery test completed' "${log_file}"; then
	echo "ERROR: Replacement UI worker did not initialize" >&2
	tail -n 120 "${log_file}" >&2
	exit 1
fi

echo "PASS: Supervisor replaced an abruptly terminated UI worker in isolated Wayland"
echo "INFO: Logs: ${qa_root}"
