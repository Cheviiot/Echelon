#!/usr/bin/env bash
# Exercise repeated Generals: Arsenal launcher/engine handoffs in an isolated Wayland compositor.
#
# Usage:
#   ./scripts/qa/smoke/test-generals-arsenal-lifecycle-wayland.sh [BUILD_PRESET] [CYCLES] [UPDATES] [START_PROFILE] [WINDOW_MODE]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
cycle_count="${2:-10}"
update_count="${3:-60}"
start_profile="${4:-generals}"
window_mode="${5:-windowed}"
quick_start="${GENERALS_ARSENAL_QA_QUICK_START:-no}"
no_shell_map="${GENERALS_ARSENAL_QA_NO_SHELL_MAP:-no}"
launcher="${repo_root}/build/${build_preset}/GeneralsArsenal/GeneralsArsenal"
dxvk_dir="${repo_root}/build/${build_preset}/_deps/dxvk-src/lib"
source_data_root="${GENERALS_ARSENAL_QA_DATA_ROOT:-${HOME}/.GeneralsArsenal}"

if [[ -n "${WAYLAND_DISPLAY:-}" ]] || [[ -n "${DISPLAY:-}" ]]; then
	echo "INFO: Host display variables will not be forwarded to the lifecycle test"
fi
if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
if ! command -v mutter >/dev/null 2>&1; then
	echo "ERROR: Mutter is required for the isolated Wayland test" >&2
	exit 2
fi
if [[ "${start_profile}" != "generals" && "${start_profile}" != "zerohour" ]]; then
	echo "ERROR: START_PROFILE must be generals or zerohour" >&2
	exit 2
fi
if [[ "${window_mode}" != "windowed" && "${window_mode}" != "fullscreen" ]]; then
	echo "ERROR: WINDOW_MODE must be windowed or fullscreen" >&2
	exit 2
fi
if [[ "${quick_start}" != "yes" && "${quick_start}" != "no" ]] ||
	[[ "${no_shell_map}" != "yes" && "${no_shell_map}" != "no" ]]; then
	echo "ERROR: GENERALS_ARSENAL_QA_QUICK_START and GENERALS_ARSENAL_QA_NO_SHELL_MAP must be yes or no" >&2
	exit 2
fi
if [[ ! -f "${source_data_root}/Generals/INI.big" || ! -f "${source_data_root}/GeneralsZH/INIZH.big" ]]; then
	echo "ERROR: Both retail data sets are required under ${source_data_root}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/generals-arsenal-lifecycle.XXXXXX)"
qa_runtime="${qa_root}/runtime"
qa_home="${qa_root}/home"
log_file="${qa_root}/lifecycle.log"
mkdir -p "${qa_runtime}" "${qa_home}/.GeneralsArsenal/Profiles" \
	"${qa_home}/.GeneralsArsenal/UserData/Generals" "${qa_home}/.GeneralsArsenal/UserData/GeneralsZH" \
	"${qa_home}/.GeneralsArsenal/Launcher" "${qa_home}/.GeneralsArsenal/Mods"
chmod 700 "${qa_runtime}"
ln -s "${source_data_root}/Generals" "${qa_home}/.GeneralsArsenal/Generals"
ln -s "${source_data_root}/GeneralsZH" "${qa_home}/.GeneralsArsenal/GeneralsZH"
cp "${source_data_root}/Profiles/generals.ini" "${qa_home}/.GeneralsArsenal/Profiles/"
cp "${source_data_root}/Profiles/zerohour.ini" "${qa_home}/.GeneralsArsenal/Profiles/"
printf '%s\n' \
	'[Launcher]' \
	'SchemaVersion=1' \
	"WindowMode=${window_mode}" \
	'WindowWidth=1100' \
	'WindowHeight=680' \
	'' \
	'[Generals]' \
	'Windowed=yes' \
	"QuickStart=${quick_start}" \
	"NoShellMap=${no_shell_map}" \
	'' \
	'[ZeroHour]' \
	'Windowed=no' \
	"QuickStart=${quick_start}" \
	"NoShellMap=${no_shell_map}" \
	>"${qa_home}/.GeneralsArsenal/Launcher/Settings.ini"
printf '%s\n' 'Resolution = 800 600' >"${qa_home}/.GeneralsArsenal/UserData/Generals/Options.ini"
printf '%s\n' 'Resolution = 1024 768' >"${qa_home}/.GeneralsArsenal/UserData/GeneralsZH/Options.ini"

# shellcheck disable=SC2016 # Variables are intentionally expanded by the nested shell.
qa_root="${qa_root}" launcher="${launcher}" dxvk_dir="${dxvk_dir}" cycle_count="${cycle_count}" \
	update_count="${update_count}" start_profile="${start_profile}" dbus-run-session -- bash -c '
	set -euo pipefail
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_RUNTIME_DIR="${qa_root}/runtime"
	mutter --headless --wayland --no-x11 --wayland-display=arsenal-lifecycle-wl --virtual-monitor 1280x800 \
		>"${qa_root}/mutter.log" 2>&1 &
	compositor_pid=$!
	trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
	for _ in $(seq 1 100); do
		[[ -S "${XDG_RUNTIME_DIR}/arsenal-lifecycle-wl" ]] && break
		sleep 0.05
	done
	[[ -S "${XDG_RUNTIME_DIR}/arsenal-lifecycle-wl" ]]
	env HOME="${qa_root}/home" WAYLAND_DISPLAY=arsenal-lifecycle-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
		WAYLAND_DEBUG=client \
		DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
		LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" timeout 600s "${launcher}" \
		--internal-ui-worker --profile="${start_profile}" --internal-test-return-after-updates="${update_count}" \
		--internal-test-cycles="${cycle_count}" >"${qa_root}/lifecycle.log" 2>&1
'

expected_quiescent="${cycle_count}"
actual_quiescent="$(rg -c 'engine quiescence flags: 0x0000007f' "${log_file}" || true)"
if [[ "${actual_quiescent}" != "${expected_quiescent}" ]]; then
	echo "ERROR: Expected ${expected_quiescent} quiescent sessions, found ${actual_quiescent}" >&2
	tail -n 160 "${log_file}" >&2
	exit 1
fi
if ! rg -q "lifecycle test completed ${cycle_count} cycles" "${log_file}"; then
	echo "ERROR: Lifecycle completion marker is missing" >&2
	tail -n 160 "${log_file}" >&2
	exit 1
fi
window_handoffs="$(rg '^\[WINDOW-HANDOFF\]' "${log_file}" || true)"
window_handoff_count="$(printf '%s\n' "${window_handoffs}" | rg -c '^\[WINDOW-HANDOFF\]' || true)"
expected_window_handoffs="$((cycle_count * 3))"
if [[ "${window_handoff_count}" != "${expected_window_handoffs}" ]]; then
	echo "ERROR: Expected ${expected_window_handoffs} window-state samples, found ${window_handoff_count}" >&2
	exit 1
fi
if ! printf '%s\n' "${window_handoffs}" | awk '
	function geometry(line) {
		sub(/^.* logical=/, "", line)
		return line
	}
	/phase=launcher-before-engine/ { before = geometry($0); next }
	/phase=launcher-restored/ {
		restored = geometry($0)
		if (before == "" || before != restored) failures++
		before = ""
	}
	END { exit failures != 0 || before != "" }
'; then
	echo "ERROR: Launcher window state was not restored after an engine handoff" >&2
	printf '%s\n' "${window_handoffs}" >&2
	exit 1
fi
mode_requests="$(rg '^\[WINDOW-MODE\] owner=engine requested=' "${log_file}" || true)"
if [[ "${cycle_count}" -ge 2 ]]; then
	if ! printf '%s\n' "${mode_requests}" | rg -q 'requested=windowed render=800x600' ||
		! printf '%s\n' "${mode_requests}" | rg -q 'requested=fullscreen render=1024x768'; then
		echo "ERROR: Alternating engine window/fullscreen requests were not observed" >&2
		printf '%s\n' "${mode_requests}" >&2
		exit 1
	fi
fi
if rg -q 'Wayland display connection closed|error in client communication|protocol error' \
	"${log_file}" "${qa_root}/mutter.log"; then
	echo "ERROR: Wayland protocol failure detected" >&2
	tail -n 160 "${log_file}" >&2
	exit 1
fi

# GeneralsArsenal @bugfix Codex 13/08/2026 Verify the exact protocol lifetime that
# previously killed the real GNOME Wayland client after returning from DXVK.
syncobj_check="$(awk '
	/get_surface\(new id wp_linux_drm_syncobj_surface_v1#[0-9]+, wl_surface#[0-9]+\)/ {
		object = $0
		sub(/^.*new id wp_linux_drm_syncobj_surface_v1#/, "", object)
		sub(/,.*/, "", object)
		surface = $0
		sub(/^.*wl_surface#/, "", surface)
		sub(/\).*/, "", surface)
		if (active[surface] != "") failures++
		active[surface] = object
		owner[object] = surface
		created++
	}
	/wp_linux_drm_syncobj_surface_v1#[0-9]+\.destroy\(\)/ {
		object = $0
		sub(/^.*wp_linux_drm_syncobj_surface_v1#/, "", object)
		sub(/\.destroy.*/, "", object)
		surface = owner[object]
		if (surface == "" || active[surface] != object) failures++
		if (surface != "") delete active[surface]
		delete owner[object]
		destroyed++
	}
	END {
		remaining = 0
		for (surface in active) remaining++
		printf "%d %d %d %d", created, destroyed, remaining, failures
	}
' "${log_file}")"
read -r syncobj_created syncobj_destroyed syncobj_remaining syncobj_failures <<<"${syncobj_check}"
if (( syncobj_created == 0 || syncobj_created != syncobj_destroyed || syncobj_remaining != 0 || syncobj_failures != 0 )); then
	echo "ERROR: Wayland explicit-sync surfaces are not balanced (created=${syncobj_created}, destroyed=${syncobj_destroyed}, remaining=${syncobj_remaining}, failures=${syncobj_failures})" >&2
	tail -n 200 "${log_file}" >&2
	exit 1
fi

resource_rows="$(rg '^\[WORKER-RESOURCES\]' "${log_file}" || true)"
resource_count="$(printf '%s\n' "${resource_rows}" | rg -c '^\[WORKER-RESOURCES\]' || true)"
expected_resource_count="$((cycle_count + 1))"
if [[ "${resource_count}" != "${expected_resource_count}" ]]; then
	echo "ERROR: Expected ${expected_resource_count} worker resource samples, found ${resource_count}" >&2
	tail -n 160 "${log_file}" >&2
	exit 1
fi

resource_check="$(printf '%s\n' "${resource_rows}" | awk '
	function value(name,   position, tail) {
		position = match($0, name "=[0-9]+")
		if (!position) return -1
		tail = substr($0, position + length(name) + 1)
		return tail + 0
	}
	{
		rss = value("rss_kb")
		threads = value("threads")
		fds = value("fds")
		if (NR == 1) {
			min_rss = max_rss = rss
			min_threads = max_threads = threads
			min_fds = max_fds = fds
		}
		if (NR == 3) plateau_min_rss = rss
		if (NR >= 3 && rss < plateau_min_rss) plateau_min_rss = rss
		if (rss > max_rss) max_rss = rss
		if (threads < min_threads) min_threads = threads
		if (threads > max_threads) max_threads = threads
		if (fds < min_fds) min_fds = fds
		if (fds > max_fds) max_fds = fds
		last_rss = rss
		last_threads = threads
		last_fds = fds
	}
	END {
		if (NR < 3) plateau_min_rss = min_rss
		printf "%d %d %d %d %d %d %d", last_rss, plateau_min_rss, max_rss, last_threads, max_threads, last_fds, max_fds
	}
')"
read -r last_rss min_rss max_rss last_threads max_threads last_fds max_fds <<<"${resource_check}"
rss_growth_kb="$((last_rss - min_rss))"
if (( rss_growth_kb > 262144 )); then
	echo "ERROR: Worker RSS grew by ${rss_growth_kb} KiB across quiescent handoffs" >&2
	exit 1
fi
if (( last_threads > 24 || max_threads > 64 )); then
	echo "ERROR: Worker thread count did not return to a bounded quiescent state (last=${last_threads}, max=${max_threads})" >&2
	exit 1
fi
if (( last_fds > 96 || max_fds > 160 )); then
	echo "ERROR: Worker descriptor count did not return to a bounded quiescent state (last=${last_fds}, max=${max_fds})" >&2
	exit 1
fi

echo "PASS: ${cycle_count} alternating Generals/Zero Hour handoffs completed in isolated Wayland"
echo "PASS: Launcher window state was restored across ${cycle_count} engine handoffs"
if [[ "${cycle_count}" -ge 2 ]]; then
	echo "PASS: Engine presentation changed independently between 800x600 windowed and 1024x768 fullscreen"
fi
echo "PASS: Wayland explicit-sync surface lifetime is balanced (${syncobj_created}/${syncobj_destroyed})"
echo "PASS: Quiescent resource bounds rss_growth=${rss_growth_kb}KiB rss_max=${max_rss}KiB threads=${last_threads}/${max_threads} fds=${last_fds}/${max_fds}"
echo "INFO: Logs: ${qa_root}"
