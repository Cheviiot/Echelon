#!/usr/bin/env bash
# Exercise a verified mod -> patch -> addon stack in both engines without touching retail data.
#
# Usage:
#   ./scripts/qa/smoke/test-echelon-content-stack.sh [BUILD_PRESET] [UPDATES]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
update_count="${2:-30}"
launcher="${repo_root}/build/${build_preset}/Echelon/Echelon"
dxvk_dir="${repo_root}/build/${build_preset}/_deps/dxvk-src/lib"
source_data_root="${ECHELON_QA_DATA_ROOT:-${HOME}/.Echelon}"

if [[ ! -x "${launcher}" ]]; then
	echo "ERROR: Launcher is not built: ${launcher}" >&2
	exit 2
fi
if [[ ! -f "${source_data_root}/Generals/INI.big" || ! -f "${source_data_root}/GeneralsZH/INIZH.big" ]]; then
	echo "ERROR: Both retail data sets are required under ${source_data_root}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/echelon-content-stack.XXXXXX)"
qa_home="${qa_root}/home"
echelon_root="${qa_home}/.Echelon"
mkdir -p "${echelon_root}/Profiles" "${echelon_root}/UserData/Generals" \
	"${echelon_root}/UserData/GeneralsZH" "${echelon_root}/Launcher" "${echelon_root}/Mods/Installed"
ln -s "${source_data_root}/Generals" "${echelon_root}/Generals"
ln -s "${source_data_root}/GeneralsZH" "${echelon_root}/GeneralsZH"
cp "${source_data_root}/Profiles/generals.ini" "${source_data_root}/Profiles/zerohour.ini" \
	"${echelon_root}/Profiles/"

create_layer() {
	local engine="$1"
	local type="$2"
	local id="$3"
	local parent_id="$4"
	local ordinal="$5"
	local layer_root="${echelon_root}/Mods/Installed/${engine}/${type}/${id#*:}/1.0"
	mkdir -p "${layer_root}/content/Data/EchelonQA"
	printf 'loose-layer=%s\n' "${ordinal}" >"${layer_root}/content/Data/EchelonQA/Precedence.txt"
	python3 - "${layer_root}/content/!!${ordinal}-preferred.gib" "${ordinal}-preferred" <<'PY'
import struct
import sys

target, ordinal = sys.argv[1], sys.argv[2]
name = b"Data/EchelonQA/BigPrecedence.txt\0"
payload = ("big-layer=" + ordinal + "\n").encode("ascii")
offset = 16 + 8 + len(name)
size = offset + len(payload)
archive = b"BIGF" + struct.pack("<I", size) + struct.pack(">I", 1) + struct.pack(">I", offset)
archive += struct.pack(">II", offset, len(payload)) + name + payload
with open(target, "wb") as output:
    output.write(archive)
PY
	python3 - "${layer_root}/content/!${ordinal}-fallback.big" "${ordinal}-fallback" <<'PY'
import struct
import sys

target, ordinal = sys.argv[1], sys.argv[2]
name = b"Data/EchelonQA/BigPrecedence.txt\0"
payload = ("big-layer=" + ordinal + "\n").encode("ascii")
offset = 16 + 8 + len(name)
size = offset + len(payload)
archive = b"BIGF" + struct.pack("<I", size) + struct.pack(">I", 1) + struct.pack(">I", offset)
archive += struct.pack(">II", offset, len(payload)) + name + payload
with open(target, "wb") as output:
    output.write(archive)
PY
	python3 - "${layer_root}" "${engine}" "${type}" "${id}" "${parent_id}" <<'PY'
import hashlib
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
engine, kind, identity, parent = sys.argv[2:]
content = root / "content"
entries = []
for path in sorted((p for p in content.rglob("*") if p.is_file()), key=lambda p: p.relative_to(content).as_posix()):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    entries.append(f"{digest}  {path.relative_to(content).as_posix()}\n")
fingerprint = hashlib.sha256("".join(entries).encode("utf-8")).hexdigest()
manifest = (
    "[Modification]\nSchemaVersion=2\n"
    f"Id={identity}\nSourceId=qa\nName={identity}\nVersion=1.0\nEngine={engine}\n"
    f"Type={kind}\nParentId={parent}\nSource=qa-fixture\nRootPath=content\nCoverImage=\n"
    f"SHA256=\nContentFingerprint={fingerprint}\n"
)
(root / "manifest.ini").write_text(manifest, encoding="utf-8")
(root / "files.sha256").write_text("".join(entries), encoding="utf-8")
PY
}

for profile in generals zerohour; do
	create_layer "${profile}" mod qa:mod "" 100
	create_layer "${profile}" patch qa:patch qa:mod 200
	create_layer "${profile}" addon qa:addon-one qa:patch 300
	create_layer "${profile}" addon qa:addon-two qa:mod 400
done

run_stack() {
	local profile="$1"
	local log_file="${qa_root}/${profile}.log"
	env -u DISPLAY -u WAYLAND_DISPLAY HOME="${qa_home}" SDL_AUDIODRIVER=dummy \
		DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
		LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
		timeout 180s "${launcher}" --profile="${profile}" \
		--mod=qa:mod@1.0 --patch=qa:patch@1.0 \
		--addon=qa:addon-one@1.0 --addon=qa:addon-two@1.0 \
		-headless --internal-test-return-after-updates="${update_count}" >"${log_file}" 2>&1
	if ! rg -q '\[CONTENT-STACK\].*layers=4 fingerprint=[0-9a-f]{64}' "${log_file}"; then
		echo "ERROR: ${profile} did not receive the complete stable stack" >&2
		tail -n 160 "${log_file}" >&2
		exit 1
	fi
	local observed
	observed="$(rg -o '\[CONTENT-LAYER\].*id=[^ ]+' "${log_file}" | sed 's/.*id=//' | paste -sd, -)"
	if [[ "${observed}" != "qa:mod,qa:patch,qa:addon-one,qa:addon-two" ]]; then
		echo "ERROR: ${profile} mounted layers in the wrong order: ${observed}" >&2
		exit 1
	fi
	if [[ "$(rg -c '\[CONTENT-LAYER\].*big=loaded archives=2 files=2 precedence=verified' "${log_file}")" -ne 4 ]]; then
		echo "ERROR: ${profile} did not preserve duplicate BIG priority inside every managed layer" >&2
		exit 1
	fi
	if ! rg -q 'engine quiescence flags: 0x0000007f' "${log_file}"; then
		echo "ERROR: ${profile} did not release its content stack at quiescence" >&2
		exit 1
	fi
}

run_stack generals
run_stack zerohour

printf 'tampered\n' >>"${echelon_root}/Mods/Installed/generals/mod/mod/1.0/content/Data/EchelonQA/Precedence.txt"
tamper_log="${qa_root}/tamper.log"
if env -u DISPLAY -u WAYLAND_DISPLAY HOME="${qa_home}" SDL_AUDIODRIVER=dummy \
	DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
	LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
	timeout 60s "${launcher}" --profile=generals --mod=qa:mod@1.0 -headless \
	--internal-test-return-after-updates=1 >"${tamper_log}" 2>&1; then
	echo "ERROR: Tampered content unexpectedly launched" >&2
	exit 1
fi
if ! rg -qi 'fingerprint does not match' "${tamper_log}"; then
	echo "ERROR: Tampered content was rejected without a useful integrity error" >&2
	cat "${tamper_log}" >&2
	exit 1
fi

conflict_log="${qa_root}/legacy-conflict.log"
if env -u DISPLAY -u WAYLAND_DISPLAY HOME="${qa_home}" SDL_AUDIODRIVER=dummy \
	DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
	LD_LIBRARY_PATH="${dxvk_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
	timeout 60s "${launcher}" --profile=zerohour --mod=qa:mod@1.0 -mod arbitrary.big -headless \
	--internal-test-return-after-updates=1 >"${conflict_log}" 2>&1; then
	echo "ERROR: Managed content and legacy -mod were accepted together" >&2
	exit 1
fi
if ! rg -qi 'cannot be combined' "${conflict_log}"; then
	echo "ERROR: Managed/legacy conflict was rejected without a useful error" >&2
	exit 1
fi

echo "PASS: Verified mod, patch, and ordered addon layers mounted and released in both engines"
echo "INFO: Logs and fixtures: ${qa_root}"
