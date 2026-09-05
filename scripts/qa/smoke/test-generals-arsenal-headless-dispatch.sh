#!/usr/bin/env bash
# Verify that both engine profiles bypass the graphical launcher in headless mode and tear down cleanly.
#
# Usage:
#   ./scripts/qa/smoke/test-generals-arsenal-headless-dispatch.sh [BUILD_PRESET] [UPDATES]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
update_count="${2:-60}"
launcher="${repo_root}/build/${build_preset}/GeneralsArsenal/GeneralsArsenal"
dxvk_dir="${repo_root}/build/${build_preset}/_deps/dxvk-src/lib"
source_data_root="${GENERALS_ARSENAL_QA_DATA_ROOT:-${HOME}/.GeneralsArsenal}"

if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
if [[ ! -f "${source_data_root}/Generals/INI.big" || ! -f "${source_data_root}/GeneralsZH/INIZH.big" ]]; then
	echo "ERROR: Both retail data sets are required under ${source_data_root}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/generals-arsenal-headless.XXXXXX)"
qa_home="${qa_root}/home"
mkdir -p "${qa_home}/.GeneralsArsenal/Profiles" \
	"${qa_home}/.GeneralsArsenal/UserData/Generals" "${qa_home}/.GeneralsArsenal/UserData/GeneralsZH" \
	"${qa_home}/.GeneralsArsenal/Launcher" "${qa_home}/.GeneralsArsenal/Mods"
ln -s "${source_data_root}/Generals" "${qa_home}/.GeneralsArsenal/Generals"
ln -s "${source_data_root}/GeneralsZH" "${qa_home}/.GeneralsArsenal/GeneralsZH"
cp "${source_data_root}/Profiles/generals.ini" "${source_data_root}/Profiles/zerohour.ini" \
	"${qa_home}/.GeneralsArsenal/Profiles/"

for profile in generals zerohour; do
	log_file="${qa_root}/${profile}.log"
	env -u DISPLAY -u WAYLAND_DISPLAY HOME="${qa_home}" SDL_AUDIODRIVER=dummy \
		DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
		LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
		timeout 180s "${launcher}" --profile="${profile}" -headless \
		--internal-test-return-after-updates="${update_count}" >"${log_file}" 2>&1
	if ! rg -q 'engine quiescence flags: 0x0000007f' "${log_file}"; then
		echo "ERROR: ${profile} did not reach the required headless quiescent state" >&2
		tail -n 120 "${log_file}" >&2
		exit 1
	fi
	if rg -q 'UI worker|launcher renderer' "${log_file}"; then
		echo "ERROR: ${profile} unexpectedly entered the graphical launcher path" >&2
		exit 1
	fi
done

echo "PASS: Generals and Zero Hour headless dispatch completed without entering the graphical launcher"
echo "INFO: Logs: ${qa_root}"
