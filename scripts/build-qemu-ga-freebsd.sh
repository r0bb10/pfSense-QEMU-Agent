#!/bin/sh

set -eu

TAG="${1:?usage: $0 <qemu-version-or-tag>}"
VERSION="${TAG#v}"
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
WORK="${ROOT}/work"

if ! printf '%s\n' "${VERSION}" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
	echo "Expected a stable QEMU version, for example v11.1.2." >&2
	exit 1
fi
mkdir -p "${WORK}" "${ROOT}/dist"
BUILD=$(mktemp -d "${WORK}/qemu-build.XXXXXX")
ARCHIVE="${BUILD}/qemu-${VERSION}.tar.xz"
SRC="${BUILD}/qemu-${VERSION}"

# Stage the target userspace separately; host compiler/tools remain untouched.
QEMU_TARGET_SYSROOT="${PFSENSE_SYSROOT:-$(python3 "${ROOT}/scripts/prepare-pfsense-sysroot.py")}"
export QEMU_TARGET_SYSROOT
python3 - "${QEMU_TARGET_SYSROOT}" <<'PY'
import json
from pathlib import Path
import sys
root = Path(sys.argv[1])
sdk = json.loads((root / 'pfsense-sdk.json').read_text())
if sdk['abi'] != 'FreeBSD:15:amd64' or sdk['osversion'] != 1500029:
    raise SystemExit('Expected the pfSense 2.8.1 / FreeBSD 15 (1500029) target sysroot.')
PY
QEMU_TARGET_CC=$(command -v "${CC:-clang}")
QEMU_TARGET_CXX=$(command -v "${CXX:-clang++}")
export QEMU_TARGET_CC QEMU_TARGET_CXX
cat > "${BUILD}/pfsense-cc" <<'EOF'
#!/bin/sh
exec "$QEMU_TARGET_CC" --target=x86_64-unknown-freebsd15.0 --sysroot="$QEMU_TARGET_SYSROOT" "$@"
EOF
cat > "${BUILD}/pfsense-cxx" <<'EOF'
#!/bin/sh
exec "$QEMU_TARGET_CXX" --target=x86_64-unknown-freebsd15.0 --sysroot="$QEMU_TARGET_SYSROOT" "$@"
EOF
chmod 0755 "${BUILD}/pfsense-cc" "${BUILD}/pfsense-cxx"

# Only the target pkg-config files are visible, never the host's newer GLib.
PKG_CONFIG_SYSROOT_DIR="${QEMU_TARGET_SYSROOT}"
PKG_CONFIG_LIBDIR="${QEMU_TARGET_SYSROOT}/usr/local/libdata/pkgconfig:${QEMU_TARGET_SYSROOT}/usr/local/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR
unset PKG_CONFIG_PATH

curl --fail --location --proto '=https' --proto-redir '=https' \
	--connect-timeout 15 --max-time 600 --retry 3 \
	"https://download.qemu.org/qemu-${VERSION}.tar.xz" -o "${ARCHIVE}"
tar -xf "${ARCHIVE}" -C "${BUILD}"
test "$(cat "${SRC}/VERSION")" = "${VERSION}"

mkdir "${BUILD}/out"
cd "${BUILD}/out"
"${SRC}/configure" \
	--cross-prefix= \
	--cc="${BUILD}/pfsense-cc" \
	--cxx="${BUILD}/pfsense-cxx" \
	--host-cc="${HOST_CC:-clang}" \
	--prefix=/usr/local \
	--localstatedir=/var \
	--python="$(command -v python3)" \
	--without-default-features \
	--disable-system \
	--disable-user \
	--disable-tools \
	--disable-docs \
	--enable-guest-agent \
	--extra-ldflags="-fuse-ld=lld -Wl,-rpath-link,${QEMU_TARGET_SYSROOT}/lib:${QEMU_TARGET_SYSROOT}/usr/lib:${QEMU_TARGET_SYSROOT}/usr/local/lib"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
"${MAKE:-gmake}" -j "${JOBS}" qemu-ga

# Meson's install step removes build-tree RUNPATHs without rebuilding anything.
./pyvenv/bin/meson install --no-rebuild --tags runtime --destdir "${BUILD}/installed"
RESULT="${BUILD}/installed/usr/local/bin/qemu-ga"
"${STRIP:-strip}" "${RESULT}"
# Reject any accidental host-library paths embedded in the shipped executable.
readelf -d "${RESULT}" > "${BUILD}/dynamic-section.txt"
if grep -Eq 'RPATH|RUNPATH' "${BUILD}/dynamic-section.txt"; then
	echo "Refusing a qemu-ga with embedded build-library paths." >&2
	exit 1
fi
# The Actions host is FreeBSD 15; use the target libraries for this smoke test.
if [ "$(uname -s)" = FreeBSD ]; then
	test "$(LD_LIBRARY_PATH="${QEMU_TARGET_SYSROOT}/lib:${QEMU_TARGET_SYSROOT}/usr/lib:${QEMU_TARGET_SYSROOT}/usr/local/lib" "${RESULT}" --version)" = "QEMU Guest Agent ${VERSION}"
fi
install -m 0755 "${RESULT}" "${ROOT}/dist/qemu-ga"
printf '%s\n' "${VERSION}" > "${ROOT}/dist/VERSION"
echo "Built QEMU Guest Agent ${VERSION} for pfSense 2.8.1 / FreeBSD 15."
