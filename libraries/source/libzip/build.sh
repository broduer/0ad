#!/bin/sh
set -e

: "${OS:=$(uname -s)}"
: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=1.11.4
LIB_VERSION=${PV}+wfg0

fetch()
{
	curl -fLo "libzip-${PV}.tar.xz" \
		"https://libzip.org/download/libzip-${PV}.tar.xz"
}

echo "Building libzip..."
while [ "$#" -gt 0 ]; do
	case "$1" in
		--fetch-only)
			fetch
			exit
			;;
		--force-rebuild) rm -f .already-built ;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
	shift
done

if [ -e .already-built ] && [ "$(cat .already-built || true)" = "${LIB_VERSION}" ]; then
	echo "Skipping - already built (use --force-rebuild to override)"
	exit
fi

# fetch
if [ ! -e "libzip-${PV}.tar.xz" ]; then
	fetch
fi

# unpack
rm -Rf "libzip-${PV}"
"${TAR}" xf "libzip-${PV}.tar.xz"

# configure
rm -rf build
shared_libs=ON
if [ "${OS}" = "Darwin" ]; then
	shared_libs=OFF
fi
cmake -B build -S "libzip-${PV}" \
	-DCMAKE_INSTALL_PREFIX="$(realpath . || true)" \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DBUILD_SHARED_LIBS=${shared_libs} \
	-DENABLE_COMMONCRYPTO=OFF \
	-DENABLE_GNUTLS=OFF \
	-DENABLE_MBEDTLS=OFF \
	-DENABLE_OPENSSL=OFF \
	-DENABLE_WINDOWS_CRYPTO=OFF \
	-DENABLE_BZIP2=OFF \
	-DENABLE_LZMA=OFF \
	-DENABLE_ZSTD=OFF \
	-DENABLE_FDOPEN=OFF \
	-DBUILD_TOOLS=OFF \
	-DBUILD_REGRESS=OFF \
	-DBUILD_OSSFUZZ=OFF \
	-DBUILD_EXAMPLES=OFF \
	-DBUILD_DOC=OFF

# build
cmake --build build

# install
rm -Rf bin include lib
cmake --install build

echo "${LIB_VERSION}" >.already-built
