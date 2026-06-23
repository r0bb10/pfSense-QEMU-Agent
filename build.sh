#!/bin/sh

set -eu

PORTNAME="pfSense-pkg-qemu-guest-agent"
PORTVERSION="${PORTVERSION:-0.0.0}"
ABI="FreeBSD:15:amd64"
PREFIX="/usr/local"
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
FILES="${ROOT}/files"
BUILD="${ROOT}/build"
STAGE="${BUILD}/stage"
OUTPUT="${BUILD}/pkg"
BINARY="${ROOT}/dist/qemu-ga"

clean() {
	rm -rf "${BUILD}"
}

json_escape() {
	awk '{ gsub(/\\/, "\\\\"); gsub(/\t/, "\\t"); gsub(/"/, "\\\""); printf "%s\\n", $0 }'
}

stage() {
	if [ ! -x "${BINARY}" ]; then
		echo "Missing FreeBSD/amd64 binary: ${BINARY}" >&2
		echo "Run scripts/fetch-freebsd-qemu-guest-agent.sh or place qemu-ga at dist/qemu-ga." >&2
		exit 1
	fi

	rm -rf "${STAGE}"
	mkdir -p \
		"${STAGE}${PREFIX}/bin" \
		"${STAGE}${PREFIX}/etc/rc.d" \
		"${STAGE}${PREFIX}/pkg" \
		"${STAGE}${PREFIX}/www" \
		"${STAGE}${PREFIX}/www/shortcuts" \
		"${STAGE}${PREFIX}/www/widgets/widgets" \
		"${STAGE}${PREFIX}/share/${PORTNAME}" \
		"${STAGE}/etc/inc/priv"

	install -m 0755 "${BINARY}" "${STAGE}${PREFIX}/bin/qemu-ga"
	install -m 0555 "${FILES}${PREFIX}/etc/rc.d/qemu-guest-agent" "${STAGE}${PREFIX}/etc/rc.d/qemu-guest-agent"
	install -m 0644 "${FILES}${PREFIX}/pkg/qemu_guest_agent.xml" "${STAGE}${PREFIX}/pkg/qemu-guest-agent.xml"
	install -m 0644 "${FILES}${PREFIX}/pkg/qemu_guest_agent.inc" "${STAGE}${PREFIX}/pkg/qemu-guest-agent.inc"
	install -m 0644 "${FILES}${PREFIX}/www/shortcuts/pkg_qemu_guest_agent.inc" "${STAGE}${PREFIX}/www/shortcuts/pkg_qemu_guest_agent.inc"
	install -m 0644 "${FILES}${PREFIX}/www/status_qemu_guest_agent.php" "${STAGE}${PREFIX}/www/status_qemu_guest_agent.php"
	install -m 0644 "${FILES}${PREFIX}/www/widgets/widgets/qemu_guest_agent.widget.php" "${STAGE}${PREFIX}/www/widgets/widgets/qemu_guest_agent.widget.php"
	install -m 0644 "${FILES}${PREFIX}/share/${PORTNAME}/info.xml" "${STAGE}${PREFIX}/share/${PORTNAME}/info.xml"
	install -m 0644 "${FILES}/etc/inc/priv/qemu_guest_agent.priv.inc" "${STAGE}/etc/inc/priv/qemu_guest_agent.priv.inc"

	for file in \
		"${STAGE}${PREFIX}/pkg/qemu-guest-agent.xml" \
		"${STAGE}${PREFIX}/share/${PORTNAME}/info.xml"; do
		sed "s/%%PKGVERSION%%/${PORTVERSION}/g" "${file}" > "${file}.tmp"
		mv "${file}.tmp" "${file}"
	done
}

manifest() {
	post_install_script=$(sed "s/%%PORTNAME%%/${PORTNAME}/g" "${FILES}/pkg-install.in" | json_escape)
	pre_deinstall_script=$(sed "s/%%PORTNAME%%/${PORTNAME}/g" "${FILES}/pkg-deinstall.in" | json_escape)

	cat > "${BUILD}/+MANIFEST" <<EOF
name: "${PORTNAME}"
version: "${PORTVERSION}"
origin: "sysutils/${PORTNAME}"
comment: "Proxmox Guest Agent for pfSense"
maintainer: "noreply@github.com"
prefix: "${PREFIX}"
abi: "${ABI}"
desc: "Proxmox QEMU Guest Agent package for pfSense with WebGUI service integration and status visibility."
www: "https://github.com/r0bb10/pfSense-QEMU-Agent"
licenselogic: "single"
licenses: ["GPLv2"]
categories: ["sysutils"]
scripts: {
  post-install: "${post_install_script}",
  pre-deinstall: "${pre_deinstall_script}"
}
EOF

	sed "s|%%DATADIR%%|share/${PORTNAME}|g" "${ROOT}/pkg-plist" > "${BUILD}/plist"
}

package() {
	stage
	manifest
	mkdir -p "${OUTPUT}"
	pkg create -M "${BUILD}/+MANIFEST" -p "${BUILD}/plist" -r "${STAGE}" -o "${OUTPUT}"
	find "${OUTPUT}" -maxdepth 1 -type f -print
}

case "${1:-package}" in
	clean) clean ;;
	stage) stage ;;
	package) package ;;
	*) echo "Usage: $0 [package|stage|clean]" >&2; exit 2 ;;
esac
