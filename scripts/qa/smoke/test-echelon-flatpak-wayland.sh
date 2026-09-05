#!/usr/bin/env bash
# Exercise the installed Echelon Flatpak in a private Wayland compositor and HOME.
#
# Usage:
#   ./scripts/qa/smoke/test-echelon-flatpak-wayland.sh [CYCLES] [UPDATES] [WINDOW_MODE]

set -euo pipefail

app_id="io.github.cheviiot.Echelon"
cycle_count="${1:-10}"
update_count="${2:-30}"
window_mode="${3:-windowed}"
quick_start="${ECHELON_QA_QUICK_START:-no}"
no_shell_map="${ECHELON_QA_NO_SHELL_MAP:-no}"
source_data_root="${ECHELON_QA_DATA_ROOT:-${HOME}/.Echelon}"

if ! flatpak --user info "${app_id}" >/dev/null 2>&1; then
	echo "ERROR: ${app_id} is not installed for the current user" >&2
	exit 2
fi
if ! command -v mutter >/dev/null 2>&1; then
	echo "ERROR: Mutter is required for the isolated Wayland test" >&2
	exit 2
fi
if [[ "${window_mode}" != "windowed" && "${window_mode}" != "fullscreen" ]]; then
	echo "ERROR: WINDOW_MODE must be windowed or fullscreen" >&2
	exit 2
fi
if [[ "${quick_start}" != "yes" && "${quick_start}" != "no" ]] ||
	[[ "${no_shell_map}" != "yes" && "${no_shell_map}" != "no" ]]; then
	echo "ERROR: ECHELON_QA_QUICK_START and ECHELON_QA_NO_SHELL_MAP must be yes or no" >&2
	exit 2
fi
if [[ ! -f "${source_data_root}/Generals/INI.big" || ! -f "${source_data_root}/GeneralsZH/INIZH.big" ]]; then
	echo "ERROR: Both retail data sets are required under ${source_data_root}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/echelon-flatpak.XXXXXX)"
qa_runtime="${qa_root}/runtime"
qa_home="${qa_root}/home"
log_file="${qa_root}/flatpak.log"
mkdir -p "${qa_runtime}" "${qa_home}/.Echelon/Profiles" \
	"${qa_home}/.Echelon/UserData/Generals" "${qa_home}/.Echelon/UserData/GeneralsZH" \
	"${qa_home}/.Echelon/Launcher" "${qa_home}/.Echelon/Mods"
chmod 700 "${qa_runtime}"
ln -s "${source_data_root}/Generals" "${qa_home}/.Echelon/Generals"
ln -s "${source_data_root}/GeneralsZH" "${qa_home}/.Echelon/GeneralsZH"
cp "${source_data_root}/Profiles/generals.ini" "${source_data_root}/Profiles/zerohour.ini" \
	"${qa_home}/.Echelon/Profiles/"
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
	>"${qa_home}/.Echelon/Launcher/Settings.ini"
printf '%s\n' 'Resolution = 800 600' >"${qa_home}/.Echelon/UserData/Generals/Options.ini"
printf '%s\n' 'Resolution = 1024 768' >"${qa_home}/.Echelon/UserData/GeneralsZH/Options.ini"

# shellcheck disable=SC2016 # Variables are intentionally expanded by the nested shell.
qa_root="${qa_root}" qa_home="${qa_home}" app_id="${app_id}" cycle_count="${cycle_count}" \
	update_count="${update_count}" source_data_root="${source_data_root}" dbus-run-session -- bash -c '
	set -euo pipefail
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_RUNTIME_DIR="${qa_root}/runtime"
	mutter --headless --wayland --no-x11 --wayland-display=echelon-flatpak-wl --virtual-monitor 1280x800 \
		>"${qa_root}/mutter.log" 2>&1 &
	compositor_pid=$!
	trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
	for _ in $(seq 1 100); do
		[[ -S "${XDG_RUNTIME_DIR}/echelon-flatpak-wl" ]] && break
		sleep 0.05
	done
	[[ -S "${XDG_RUNTIME_DIR}/echelon-flatpak-wl" ]]
	env WAYLAND_DISPLAY=echelon-flatpak-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
		WAYLAND_DEBUG=client \
		timeout 600s flatpak run --filesystem="${qa_home}" --filesystem="${source_data_root}:ro" --env=HOME="${qa_home}" "${app_id}" \
		--internal-ui-worker --profile=generals --internal-test-return-after-updates="${update_count}" \
		--internal-test-cycles="${cycle_count}" >"${qa_root}/flatpak.log" 2>&1
'

actual_quiescent="$(rg -c 'engine quiescence flags: 0x0000007f' "${log_file}" || true)"
if [[ "${actual_quiescent}" != "${cycle_count}" ]]; then
	echo "ERROR: Expected ${cycle_count} quiescent Flatpak sessions, found ${actual_quiescent}" >&2
	tail -n 160 "${log_file}" >&2
	exit 1
fi
if ! rg -q "lifecycle test completed ${cycle_count} cycles" "${log_file}"; then
	echo "ERROR: Flatpak lifecycle completion marker is missing" >&2
	exit 1
fi
window_handoffs="$(rg '^\[WINDOW-HANDOFF\]' "${log_file}" || true)"
window_handoff_count="$(printf '%s\n' "${window_handoffs}" | rg -c '^\[WINDOW-HANDOFF\]' || true)"
expected_window_handoffs="$((cycle_count * 3))"
if [[ "${window_handoff_count}" != "${expected_window_handoffs}" ]]; then
	echo "ERROR: Expected ${expected_window_handoffs} installed window-state samples, found ${window_handoff_count}" >&2
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
		pairs++
	}
	END { exit failures != 0 || before != "" || pairs == 0 }
'; then
	echo "ERROR: Installed Flatpak did not restore launcher window state after handoff" >&2
	printf '%s\n' "${window_handoffs}" >&2
	exit 1
fi
mode_requests="$(rg '^\[WINDOW-MODE\] owner=engine requested=' "${log_file}" || true)"
if [[ "${cycle_count}" -ge 2 ]]; then
	if ! printf '%s\n' "${mode_requests}" | rg -q 'requested=windowed render=800x600' ||
		! printf '%s\n' "${mode_requests}" | rg -q 'requested=fullscreen render=1024x768'; then
		echo "ERROR: Installed alternating engine window/fullscreen requests were not observed" >&2
		printf '%s\n' "${mode_requests}" >&2
		exit 1
	fi
fi
if ! rg -q 'launcher font: .*/share/echelon/DejaVuSans.ttf' "${log_file}"; then
	echo "ERROR: Installed launcher did not load its packaged font" >&2
	exit 1
fi
if rg -q 'Wayland display connection closed|error in client communication|protocol error' \
	"${log_file}" "${qa_root}/mutter.log"; then
	echo "ERROR: Wayland protocol failure detected in the installed Flatpak" >&2
	exit 1
fi

# Echelon @bugfix Codex 13/08/2026 Ensure the packaged DXVK instance
# destroys every explicit-sync surface before the launcher renderer reuses the window.
syncobj_check="$(awk '
	/get_surface\(new id wp_linux_drm_syncobj_surface_v1[@#][0-9]+, wl_surface[@#][0-9]+\)/ {
		object = $0
		sub(/^.*new id wp_linux_drm_syncobj_surface_v1[@#]/, "", object)
		sub(/,.*/, "", object)
		surface = $0
		sub(/^.*wl_surface[@#]/, "", surface)
		sub(/\).*/, "", surface)
		if (active[surface] != "") failures++
		active[surface] = object
		owner[object] = surface
		created++
	}
	/wp_linux_drm_syncobj_surface_v1[@#][0-9]+\.destroy\(\)/ {
		object = $0
		sub(/^.*wp_linux_drm_syncobj_surface_v1[@#]/, "", object)
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
if (( syncobj_created != syncobj_destroyed || syncobj_remaining != 0 || syncobj_failures != 0 )); then
	echo "ERROR: Installed Flatpak leaked Wayland explicit-sync surfaces (created=${syncobj_created}, destroyed=${syncobj_destroyed}, remaining=${syncobj_remaining}, failures=${syncobj_failures})" >&2
	exit 1
fi

echo "PASS: Installed Flatpak completed ${cycle_count} alternating handoffs with its packaged font"
echo "PASS: Installed Flatpak restored launcher window state after every handoff"
if [[ "${cycle_count}" -ge 2 ]]; then
	echo "PASS: Installed engine presentation changed independently between 800x600 windowed and 1024x768 fullscreen"
fi
if (( syncobj_created > 0 )); then
	echo "PASS: Installed Flatpak balanced Wayland explicit-sync surfaces (${syncobj_created}/${syncobj_destroyed})"
elif rg -q 'wp_linux_drm_syncobj_manager_v1' "${log_file}"; then
	echo "ERROR: Explicit-sync was advertised but not exercised" >&2
	exit 1
else
	echo "SKIP: Compositor does not advertise the Wayland explicit-sync protocol"
fi
echo "INFO: Logs: ${qa_root}"
