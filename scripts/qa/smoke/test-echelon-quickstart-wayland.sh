#!/usr/bin/env bash
# Verify that quick start loads the live shell map without playing startup movies.
#
# Usage:
#   ./scripts/qa/smoke/test-echelon-quickstart-wayland.sh [BUILD_PRESET] [UPDATES]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
update_count="${2:-120}"
launcher="${repo_root}/build/${build_preset}/Echelon/Echelon"
dxvk_dir="${repo_root}/build/${build_preset}/_deps/dxvk-src/lib"
source_data_root="${ECHELON_QA_DATA_ROOT:-${HOME}/.Echelon}"

if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
for command in gdb mutter; do
	if ! command -v "${command}" >/dev/null 2>&1; then
		echo "ERROR: ${command} is required for quick-start QA" >&2
		exit 2
	fi
done
if [[ ! -f "${source_data_root}/Generals/INI.big" || ! -f "${source_data_root}/GeneralsZH/INIZH.big" ]]; then
	echo "ERROR: Both retail data sets are required under ${source_data_root}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/echelon-quickstart.XXXXXX)"
qa_runtime="${qa_root}/runtime"
qa_home="${qa_root}/home"
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
	'WindowMode=windowed' \
	'WindowWidth=1100' \
	'WindowHeight=680' \
	'' \
	'[Generals]' \
	'Windowed=yes' \
	'QuickStart=yes' \
	'NoShellMap=no' \
	'' \
	'[ZeroHour]' \
	'Windowed=yes' \
	'QuickStart=yes' \
	'NoShellMap=no' \
	>"${qa_home}/.Echelon/Launcher/Settings.ini"

for profile in generals zerohour; do
	command_file="${qa_root}/${profile}.gdb"
	log_file="${qa_root}/${profile}.log"
	printf '%s\n' \
		'set pagination off' \
		'set confirm off' \
		'set breakpoint pending on' \
		'break Shell::showShellMap(bool)' \
		'commands' \
		'silent' \
		'printf "[QUICKSTART-QA] shell-map-request\n"' \
		'continue' \
		'end' \
		'break TerrainLogic::loadMap(AsciiString, bool)' \
		'commands' \
		'silent' \
		'printf "[QUICKSTART-QA] terrain-map-load\n"' \
		'kill' \
		'quit' \
		'end' \
		'break Intro::doEALogoMovie()' \
		'commands' \
		'silent' \
		'printf "[QUICKSTART-QA] forbidden-ea-logo-movie\n"' \
		'continue' \
		'end' \
		'break Intro::doSizzleMovie()' \
		'commands' \
		'silent' \
		'printf "[QUICKSTART-QA] forbidden-sizzle-movie\n"' \
		'continue' \
		'end' \
		"run --internal-ui-worker --profile=${profile} --internal-test-return-after-updates=${update_count} --internal-test-cycles=1" \
		>"${command_file}"

	# Echelon @test Codex 14/08/2026 Never forward the user's real display to debugger-driven graphical QA.
	qa_root="${qa_root}" qa_home="${qa_home}" launcher="${launcher}" dxvk_dir="${dxvk_dir}" \
		command_file="${command_file}" log_file="${log_file}" profile="${profile}" dbus-run-session -- bash -c '
		set -euo pipefail
		unset DISPLAY WAYLAND_DISPLAY
		export XDG_RUNTIME_DIR="${qa_root}/runtime"
		wayland_name="echelon-quickstart-${profile}"
		mutter --headless --wayland --no-x11 --wayland-display="${wayland_name}" --virtual-monitor 1280x800 \
			>"${qa_root}/${profile}-mutter.log" 2>&1 &
		compositor_pid=$!
		trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
		for _ in $(seq 1 100); do
			[[ -S "${XDG_RUNTIME_DIR}/${wayland_name}" ]] && break
			sleep 0.05
		done
		[[ -S "${XDG_RUNTIME_DIR}/${wayland_name}" ]]
		env HOME="${qa_home}" WAYLAND_DISPLAY="${wayland_name}" SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
			DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
			LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
			timeout 300s gdb --batch -x "${command_file}" --args "${launcher}" >"${log_file}" 2>&1
	'

	if ! rg -q '^\[QUICKSTART-QA\] shell-map-request$' "${log_file}" ||
		! rg -q '^\[QUICKSTART-QA\] terrain-map-load$' "${log_file}"; then
		echo "ERROR: ${profile} quick start did not load the live shell map" >&2
		tail -n 160 "${log_file}" >&2
		exit 1
	fi
	if rg -q '^\[QUICKSTART-QA\] forbidden-(ea-logo|sizzle)-movie$' "${log_file}"; then
		echo "ERROR: ${profile} quick start played a startup movie" >&2
		exit 1
	fi
	echo "PASS: ${profile} loaded its live shell map without EA/Sizzle movies"
done

echo "INFO: Logs: ${qa_root}"
