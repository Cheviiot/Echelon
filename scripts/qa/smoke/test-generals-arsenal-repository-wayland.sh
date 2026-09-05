#!/usr/bin/env bash
# Verify that the installed launcher dynamically consumes a live RepositoryCatalogV1 in private Wayland.
#
# Usage:
#   ./scripts/qa/smoke/test-generals-arsenal-repository-wayland.sh
#
# Environment:
#   ARSENAL_REPOSITORY_CATALOG_URL  Override the public catalog URL.
#   ARSENAL_REPOSITORY_EXPECTED_ITEMS  Require an exact item count (default: 0).
#   ARSENAL_REPOSITORY_ENGINE  Open the Generals or Zero Hour catalog (default: zerohour).
#   ARSENAL_REPOSITORY_INSTALL_ID  Install this catalog item before capturing the screenshot.
#   ARSENAL_REPOSITORY_SEED_MOD  Optional id@version primary-mod fixture for dependency tests.
#   ARSENAL_REPOSITORY_TIMEOUT  Maximum launcher runtime in seconds (default: 120).

set -euo pipefail

app_id="io.github.cheviiot.GeneralsArsenal"
catalog_url="${ARSENAL_REPOSITORY_CATALOG_URL:-https://github.com/Cheviiot/GeneralsArsenalRepository/releases/latest/download/catalog.json}"
expected_items="${ARSENAL_REPOSITORY_EXPECTED_ITEMS:-0}"
repository_engine="${ARSENAL_REPOSITORY_ENGINE:-zerohour}"
install_id="${ARSENAL_REPOSITORY_INSTALL_ID:-}"
seed_mod="${ARSENAL_REPOSITORY_SEED_MOD:-}"
launcher_timeout="${ARSENAL_REPOSITORY_TIMEOUT:-120}"

if ! flatpak --user info "${app_id}" >/dev/null 2>&1; then
	echo "ERROR: ${app_id} is not installed for the current user" >&2
	exit 2
fi
if ! command -v mutter >/dev/null 2>&1; then
	echo "ERROR: Mutter is required for the isolated Wayland test" >&2
	exit 2
fi
if [[ "${catalog_url}" != https://* ]] || [[ ! "${expected_items}" =~ ^[0-9]+$ ]] \
		|| [[ "${repository_engine}" != "generals" && "${repository_engine}" != "zerohour" ]] \
		|| [[ -n "${install_id}" && ! "${install_id}" =~ ^[a-z0-9][a-z0-9.-]{0,95}$ ]] \
		|| [[ -n "${seed_mod}" && ! "${seed_mod}" =~ ^[a-z0-9][a-z0-9.-]{0,95}@[A-Za-z0-9][A-Za-z0-9._+~-]{0,127}$ ]] \
		|| [[ ! "${launcher_timeout}" =~ ^[1-9][0-9]*$ ]]; then
	echo "ERROR: Invalid repository test configuration" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/generals-arsenal-repository.XXXXXX)"
qa_runtime="${qa_root}/runtime"
qa_home="${qa_root}/home"
log_file="${qa_root}/launcher.log"
cache_file="${qa_home}/.GeneralsArsenal/Mods/Cache/Repositories/arsenal/index.yaml"
mkdir -p "${qa_runtime}" "${qa_home}/.GeneralsArsenal/Launcher" "${qa_home}/.GeneralsArsenal/Mods"
chmod 700 "${qa_runtime}"
if [[ -n "${seed_mod}" ]]; then
	seed_id="${seed_mod%%@*}"
	seed_version="${seed_mod#*@}"
	seed_version_path="$(printf '%s' "${seed_version}" | tr -c 'A-Za-z0-9._-' '-')"
	seed_root="${qa_home}/.GeneralsArsenal/Mods/Installed/${repository_engine}/mod/arsenal-${seed_id}/${seed_version_path}"
	mkdir -p "${seed_root}/content/Data/ArsenalQA"
	printf 'repository dependency fixture\n' >"${seed_root}/content/Data/ArsenalQA/Seed.txt"
	seed_digest="$(sha256sum "${seed_root}/content/Data/ArsenalQA/Seed.txt" | cut -d' ' -f1)"
	printf '%s  Data/ArsenalQA/Seed.txt\n' "${seed_digest}" >"${seed_root}/files.sha256"
	seed_fingerprint="$(sha256sum "${seed_root}/files.sha256" | cut -d' ' -f1)"
	printf '%s\n' '[Modification]' 'SchemaVersion=2' "Id=arsenal:${seed_id}" 'SourceId=arsenal' \
		"Name=${seed_id}" "Version=${seed_version}" "Engine=${repository_engine}" 'Type=mod' 'ParentId=' \
		'Source=repository-qa-fixture' 'RootPath=content' 'CoverImage=' 'SHA256=' \
		"ContentFingerprint=${seed_fingerprint}" >"${seed_root}/manifest.ini"
fi
printf '%s\n' '[Launcher]' 'SchemaVersion=1' 'WindowMode=windowed' 'WindowWidth=1280' 'WindowHeight=800' \
	>"${qa_home}/.GeneralsArsenal/Launcher/Settings.ini"

# GeneralsArsenal @test Codex 15/08/2026 Never expose live repository QA on the user's desktop or HOME.
qa_root="${qa_root}" qa_home="${qa_home}" app_id="${app_id}" catalog_url="${catalog_url}" \
	repository_engine="${repository_engine}" install_id="${install_id}" launcher_timeout="${launcher_timeout}" \
	dbus-run-session -- bash -c '
	set -euo pipefail
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_RUNTIME_DIR="${qa_root}/runtime"
	mutter --headless --wayland --no-x11 --wayland-display=arsenal-repository-wl --virtual-monitor 1280x800 \
		>"${qa_root}/mutter.log" 2>&1 &
	compositor_pid=$!
	trap "kill ${compositor_pid} 2>/dev/null || true; wait ${compositor_pid} 2>/dev/null || true" EXIT
	for _ in $(seq 1 100); do
		[[ -S "${XDG_RUNTIME_DIR}/arsenal-repository-wl" ]] && break
		sleep 0.05
	done
	[[ -S "${XDG_RUNTIME_DIR}/arsenal-repository-wl" ]]
	install_arguments=()
	if [[ -n "${install_id}" ]]; then
		install_arguments+=("--internal-test-install-modification=arsenal:${install_id}")
	fi
	env WAYLAND_DISPLAY=arsenal-repository-wl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
		timeout "${launcher_timeout}s" flatpak run --filesystem="${qa_home}" --env=HOME="${qa_home}" \
		--env=GENERALS_ARSENAL_REPOSITORY_URL="${catalog_url}" "${app_id}" \
		--internal-ui-worker --mods --internal-test-wait-for-repository \
		--internal-test-mods-engine="${repository_engine}" \
		--internal-test-window-size=1280x800 --internal-test-screenshot="${qa_home}/repository.png" \
		"${install_arguments[@]}" \
		>"${qa_root}/launcher.log" 2>&1
'

if [[ ! -s "${qa_home}/repository.png" ]] || [[ ! -s "${cache_file}" ]]; then
	echo "ERROR: Installed launcher did not render and cache the live catalog" >&2
	tail -n 120 "${log_file}" >&2
	exit 1
fi
actual_items="$(python3 - "${cache_file}" <<'PY'
import json
import sys
from pathlib import Path

catalog = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if catalog.get("SchemaVersion") != 1 or catalog.get("SourceId") != "arsenal":
    raise SystemExit("invalid cached Arsenal catalog")
items = catalog.get("Items")
if not isinstance(items, list):
    raise SystemExit("invalid cached Arsenal item list")
print(len(items))
PY
)"
if [[ "${actual_items}" != "${expected_items}" ]]; then
	echo "ERROR: Expected ${expected_items} live catalog items, found ${actual_items}" >&2
	exit 1
fi
if rg -q 'Repository refresh failed|Cannot refresh arsenal' "${log_file}"; then
	echo "ERROR: Installed launcher did not apply the live catalog cleanly" >&2
	tail -n 120 "${log_file}" >&2
	exit 1
fi
if [[ -n "${install_id}" ]]; then
	installed_manifest="$(find "${qa_home}/.GeneralsArsenal/Mods/Installed/${repository_engine}" \
		-type f -name manifest.ini -print0 2>/dev/null | xargs -0 -r rg -l "^Id=arsenal:${install_id}$" | head -n 1)"
	if [[ -z "${installed_manifest}" ]]; then
		echo "ERROR: Installed launcher did not publish ${install_id} atomically" >&2
		tail -n 120 "${log_file}" >&2
		exit 1
	fi
fi

echo "PASS: Installed launcher dynamically loaded ${actual_items} catalog items from GitHub Releases"
echo "PASS: Repository QA used a private Wayland compositor and temporary HOME"
if [[ -n "${install_id}" ]]; then
	echo "PASS: Installed ${install_id} through the live launcher download pipeline"
fi
echo "INFO: Screenshot and logs: ${qa_root}"
