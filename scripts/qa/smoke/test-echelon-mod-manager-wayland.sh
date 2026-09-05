#!/usr/bin/env bash
# Render the native mod manager with a full saved stack in an isolated Wayland compositor.
#
# Usage:
#   ./scripts/qa/smoke/test-echelon-mod-manager-wayland.sh [BUILD_PRESET]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
launcher="${repo_root}/build/${build_preset}/Echelon/Echelon"

if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
if ! command -v mutter >/dev/null 2>&1; then
	echo "ERROR: Mutter is required for the isolated Wayland test" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/echelon-mod-manager.XXXXXX)"
qa_home="${qa_root}/home"
mods_root="${qa_home}/.Echelon/Mods"
mkdir -p "${qa_root}/runtime" "${qa_home}/.Echelon/Launcher" \
	"${mods_root}/Profiles" "${mods_root}/Cache/Repositories/qa"
chmod 700 "${qa_root}/runtime"
printf '%s\n' '[Launcher]' 'SchemaVersion=1' 'WindowMode=windowed' 'WindowWidth=1280' 'WindowHeight=800' \
	>"${qa_home}/.Echelon/Launcher/Settings.ini"

make_fixture() {
	local type="$1" id="$2" name="$3" parent="$4" version="$5"
	local slug="${id//:/-}"
	local root="${mods_root}/Installed/generals/${type}/${slug}/${version}"
	local content_file="${root}/content/Data/INI/${slug}.ini"
	mkdir -p "$(dirname "${content_file}")"
	printf 'fixture=%s\n' "${id}" >"${content_file}"
	local file_digest index_line fingerprint
	file_digest="$(sha256sum "${content_file}" | awk '{print $1}')"
	index_line="${file_digest}  Data/INI/${slug}.ini"
	fingerprint="$(printf '%s\n' "${index_line}" | sha256sum | awk '{print $1}')"
	printf '%s\n' "${index_line}" >"${root}/files.sha256"
	cp "${repo_root}/assets/launcher/echelon-logo.png" "${root}/cover.png"
	printf '%s\n' \
		'[Modification]' 'SchemaVersion=2' "Id=${id}" 'SourceId=qa' "Name=${name}" "Version=${version}" \
		'Engine=generals' "Type=${type}" "ParentId=${parent}" 'Source=https://example.invalid/echelon-qa' \
		'RootPath=content' 'CoverImage=cover.png' 'SHA256=' "ContentFingerprint=${fingerprint}" \
		>"${root}/manifest.ini"
}

make_fixture mod qa:desert 'Desert Echelon' '' 1.0
make_fixture mod qa:desert 'Desert Echelon' '' 0.9
make_fixture patch qa:balance 'Balance Patch' qa:desert 1.0
make_fixture addon qa:voices 'Voice Pack' qa:desert 1.0
make_fixture addon qa:effects 'Effects Pack' qa:balance 1.0

printf '%s\n' \
	'[ModificationStack]' 'SchemaVersion=1' 'Engine=generals' 'Mod=qa:desert@1.0' \
	'Patch=qa:balance@1.0' 'Addon=qa:voices@1.0' 'Addon=qa:effects@1.0' \
	>"${mods_root}/Profiles/generals.ini"
printf '%s\n' \
	'[Source qa]' 'Id=qa' 'Engine=generals' 'URL=https://example.invalid/catalog.yaml' 'Format=echelon-v1' \
	>"${mods_root}/Repositories.ini"
printf '%s\n' \
	'SchemaVersion: 1' 'SourceId: qa' 'Items:' \
	'  - Id: desert' '    Engine: generals' '    Type: mod' '    Name: Desert Echelon' '    Version: "2.0"' \
	'    DownloadUrl: https://example.invalid/desert-2.0.zip' \
	'    SHA256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' '    Size: 1' \
	>"${mods_root}/Cache/Repositories/qa/index.yaml"

QA_ROOT="${qa_root}" LAUNCHER="${launcher}" dbus-run-session -- bash -c '
	set -euo pipefail
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_RUNTIME_DIR="${QA_ROOT}/runtime"
	mutter --headless --wayland --no-x11 --wayland-display=echelon-mods-wl --virtual-monitor 1920x1080 \
		>"${QA_ROOT}/mutter.log" 2>&1 &
	compositor_pid=$!
	trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
	for _ in $(seq 1 100); do
		[[ -S "${XDG_RUNTIME_DIR}/echelon-mods-wl" ]] && break
		sleep 0.05
	done
	[[ -S "${XDG_RUNTIME_DIR}/echelon-mods-wl" ]]
	for page in mods patches addons; do
		env HOME="${QA_ROOT}/home" WAYLAND_DISPLAY=echelon-mods-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
			DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null timeout 45s strace -f -e trace=connect,openat -o "${QA_ROOT}/network-${page:-context}.trace" "${LAUNCHER}" --internal-ui-worker --mods \
			--internal-test-mods-page="${page}" --internal-test-window-size=1280x800 \
			--internal-test-screenshot="${QA_ROOT}/${page}.png" >"${QA_ROOT}/${page}.log" 2>&1
	done
	for context in folders links; do
		page="${context}"
		env HOME="${QA_ROOT}/home" WAYLAND_DISPLAY=echelon-mods-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
			DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null timeout 45s strace -f -e trace=connect,openat -o "${QA_ROOT}/network-${page:-context}.trace" "${LAUNCHER}" --internal-ui-worker --mods \
			--internal-test-mods-page=patches --internal-test-mods-context="${context}" \
			--internal-test-window-size=1280x800 --internal-test-screenshot="${QA_ROOT}/${context}.png" \
			>"${QA_ROOT}/${context}.log" 2>&1
	done
'

for page in mods patches addons folders links; do
	if [[ ! -s "${qa_root}/${page}.png" ]] || ! rg -q 'Saved launcher test screenshot' "${qa_root}/${page}.log"; then
		echo "ERROR: ${page} mod-manager screenshot was not produced" >&2
		exit 1
	fi
done
if ! rg -q '^Mod=qa:desert@1.0$' "${mods_root}/Profiles/generals.ini" || \
	! rg -q '^Patch=qa:balance@1.0$' "${mods_root}/Profiles/generals.ini" || \
	[[ "$(rg -c '^Addon=' "${mods_root}/Profiles/generals.ini")" != "2" ]]; then
	echo "ERROR: Saved modification stack was not preserved" >&2
	exit 1
fi

if rg -q 'sa_family=AF_INET|/Cache/Repositories/.*O_RDONLY' "${qa_root}"/network-*.trace; then
	echo "ERROR: Local mod UI accessed a network address or retired catalog cache" >&2
	exit 1
fi

echo "PASS: Mod manager rendered mod, patch, add-on, folder-action, and link-action pages in isolated Wayland"
echo "INFO: Screenshots and logs: ${qa_root}"
