#!/bin/sh

set -eu

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
DIST="${ROOT}/dist"
FREEBSD_PACKAGE_SITE="${FREEBSD_PACKAGE_SITE:-https://pkg.freebsd.org/FreeBSD:15:amd64/latest}"
PKGNAME="qemu-guest-agent"
PACKAGESITE="${DIST}/packagesite.pkg"
PACKAGESITE_YAML="${DIST}/packagesite.yaml"

mkdir -p "${DIST}"

if ! command -v curl >/dev/null 2>&1; then
	echo "curl is required" >&2
	exit 1
fi
if ! command -v tar >/dev/null 2>&1; then
	echo "tar is required" >&2
	exit 1
fi
if ! command -v unzstd >/dev/null 2>&1; then
	echo "unzstd is required" >&2
	exit 1
fi

rm -f "${DIST}/${PKGNAME}-"*.pkg "${DIST}/qemu-ga" "${DIST}/VERSION" "${PACKAGESITE}" "${PACKAGESITE_YAML}"

curl -fsSL "${FREEBSD_PACKAGE_SITE}/packagesite.pkg" -o "${PACKAGESITE}"
tar --use-compress-program=unzstd -xOf "${PACKAGESITE}" packagesite.yaml > "${PACKAGESITE_YAML}"

version=$(python3 - "${PACKAGESITE_YAML}" "${PKGNAME}" <<'PY'
import json
import sys

path, pkgname = sys.argv[1:]
with open(path, encoding='utf-8') as handle:
    for line in handle:
        record = json.loads(line)
        if record.get('name') == pkgname:
            print(record['version'])
            break
    else:
        raise SystemExit(f'{pkgname} was not found in {path}')
PY
)
if [ -z "${version}" ]; then
	echo "Unable to resolve ${PKGNAME} version from ${FREEBSD_PACKAGE_SITE}" >&2
	exit 1
fi

archive="${DIST}/${PKGNAME}-${version}.pkg"
curl -fsSL "${FREEBSD_PACKAGE_SITE}/All/${PKGNAME}-${version}.pkg" -o "${archive}"

if tar --use-compress-program=unzstd -xOf "${archive}" /usr/local/bin/qemu-ga > "${DIST}/qemu-ga" 2>/dev/null; then
	:
elif tar --use-compress-program=unzstd -xOf "${archive}" usr/local/bin/qemu-ga > "${DIST}/qemu-ga" 2>/dev/null; then
	:
else
	echo "Unable to extract qemu-ga from ${archive}" >&2
	tar --use-compress-program=unzstd -tf "${archive}" >&2
	exit 1
fi
chmod 0755 "${DIST}/qemu-ga"
printf '%s\n' "${version}" > "${DIST}/VERSION"

echo "${version}"
