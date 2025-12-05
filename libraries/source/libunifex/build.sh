#!/bin/sh
set -e

: "${OS:=$(uname -s)}"
: "${TAR:=tar}"

cd "$(dirname "$0")"
PV=0.4.0
LIB_VERSION=${PV}+wfg0

fetch()
{
	curl -fLo "libunifex-${PV}.tar.gz" \
		"https://github.com/facebookexperimental/libunifex/archive/refs/tags/v${PV}.tar.gz"
}

echo "Building libunifex..."
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
if [ ! -e "libunifex-${PV}.tar.gz" ]; then
	fetch
fi

# unpack
rm -Rf "libunifex-${PV}"
"${TAR}" xf "libunifex-${PV}.tar.gz"

# configure
rm -rf build
shared_libs=ON
if [ "${OS}" = "Darwin" ]; then
	shared_libs=OFF
fi
cmake -S "libunifex-${PV}" -B build \
	-DCMAKE_INSTALL_PREFIX="$(realpath . || true)" \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DBUILD_SHARED_LIBS=${shared_libs} \
	-DBUILD_TESTING=OFF \
	-DUNIFEX_BUILD_EXAMPLES=OFF

# build
cmake --build build

# install
rm -Rf bin include lib
cmake --install build

echo "${LIB_VERSION}" >.already-built
