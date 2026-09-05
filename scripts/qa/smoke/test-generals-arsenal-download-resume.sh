#!/usr/bin/env bash
# Verify Range resume, ETag reset, and ignored-Range fallback against a private HTTPS server.
#
# Usage:
#   ./scripts/qa/smoke/test-generals-arsenal-download-resume.sh [BUILD_PRESET]

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_preset="${1:-linux64-deploy}"
test_binary="${repo_root}/build/${build_preset}/Core/GameEngineDevice/Source/GeneralsArsenalLauncher/generals_arsenal_settings_tests"
server_script="${repo_root}/scripts/qa/fixtures/arsenal_https_range_server.py"

if [[ ! -x "${test_binary}" ]]; then
	echo "ERROR: Settings test is not built: ${test_binary}" >&2
	exit 2
fi

qa_root="$(mktemp -d /tmp/generals-arsenal-download-resume.XXXXXX)"
package="${qa_root}/package.zip"
certificate="${qa_root}/certificate.pem"
private_key="${qa_root}/private-key.pem"
port_file="${qa_root}/port"
request_log="${qa_root}/requests.log"

QA_PACKAGE="${package}" python3 - <<'PY'
import os
import zipfile

with zipfile.ZipFile(os.environ["QA_PACKAGE"], "w", compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("Data/INI/NetworkResume.ini", "fixture=network-resume\n" * 4096)
PY
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj '/CN=127.0.0.1' \
	-addext 'subjectAltName=IP:127.0.0.1' -keyout "${private_key}" -out "${certificate}" >/dev/null 2>&1
python3 "${server_script}" --package "${package}" --certificate "${certificate}" --key "${private_key}" \
	--port-file "${port_file}" --log "${request_log}" >"${qa_root}/server.log" 2>&1 &
server_pid=$!
trap 'kill "${server_pid}" 2>/dev/null || true; wait "${server_pid}" 2>/dev/null || true' EXIT
for _ in $(seq 1 100); do
	[[ -s "${port_file}" ]] && break
	sleep 0.05
done
if [[ ! -s "${port_file}" ]]; then
	echo "ERROR: HTTPS fixture server did not start" >&2
	exit 1
fi

package_sha256="$(sha256sum "${package}" | awk '{print $1}')"
package_size="$(stat -c '%s' "${package}")"
port="$(<"${port_file}")"
set +e
distrobox enter dev-ubuntu -- env \
	CURL_CA_BUNDLE="${certificate}" \
	SSL_CERT_FILE="${certificate}" \
	GENERALS_ARSENAL_QA_CA_BUNDLE="${certificate}" \
	GENERALS_ARSENAL_QA_HTTPS_URL="https://127.0.0.1:${port}" \
	GENERALS_ARSENAL_QA_PACKAGE_PATH="${package}" \
	GENERALS_ARSENAL_QA_PACKAGE_SHA256="${package_sha256}" \
	GENERALS_ARSENAL_QA_PACKAGE_SIZE="${package_size}" \
	"${test_binary}" >"${qa_root}/test.log" 2>&1
test_exit=$?
set -e

if ! rg -q '^GET /package.zip range=bytes=[1-9][0-9]*-$' "${request_log}"; then
	echo "ERROR: Resumable request did not use the preserved byte offset" >&2
	exit 1
fi
if ! rg -q '^GET /changed.zip range=none$' "${request_log}"; then
	echo "ERROR: Changed ETag did not force a clean download" >&2
	exit 1
fi
if ! rg -q '^GET /ignore-range.zip range=bytes=[1-9][0-9]*-$' "${request_log}"; then
	echo "ERROR: Ignored-Range fixture was not exercised" >&2
	exit 1
fi
if ! rg -q '^GET /ignore-range.zip range=none$' "${request_log}"; then
	echo "ERROR: Ignored-Range transfer did not restart cleanly from byte zero" >&2
	exit 1
fi
multi_file_gets="$(rg -c '^GET /package.zip range=none$' "${request_log}" || true)"
if (( multi_file_gets < 2 )); then
	echo "ERROR: Multi-file fixture did not download both files" >&2
	exit 1
fi
if ! rg -q '^PASS: Generals: Arsenal settings persistence$' "${qa_root}/test.log"; then
	echo "ERROR: Resume integration checks failed (exit ${test_exit})" >&2
	tail -n 120 "${qa_root}/test.log" >&2
	exit 1
fi

echo "PASS: HTTP resume, ETag replacement, ignored-Range fallback, and parallel file sets are verified"
echo "INFO: Logs: ${qa_root}"
