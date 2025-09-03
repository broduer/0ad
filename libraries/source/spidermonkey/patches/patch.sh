#!/bin/sh
set -e

OS="${OS:=$(uname -s)}"

# This script gets called from inside the extracted SM tarball.
PATCHES="../patches"

# The rust code is only linked if the JS Shell is enabled,
# which fails now that rust is required in all cases.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1588340
patch -p1 <"${PATCHES}"/FixRustLinkage.diff

# Differentiate debug/release library names.
patch -p1 <"${PATCHES}"/FixLibNames.diff

# Add needed debug define in pkg-config file.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1935346
patch -p1 <"${PATCHES}"/FixPkgConfigDebug.diff

# There is an issue on 32-bit linux builds sometimes.
# NB: the patch here is Comment 21 modified by Comment 25
# but that seems to imperfectly fix the issue with GCC.
# It also won't compile on windows - in doubt, apply only where relevant.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1729459
if [ "$(uname -m)" = "i686" ] && [ "${OS}" != "Windows_NT" ]; then
	patch -p1 <"${PATCHES}"/FixFpNormIssue.diff
fi

# Fix build on macOS
# Workarounds adapted from Homebrew's formula at https://github.com/Homebrew/homebrew-core/blob/main/Formula/s/spidermonkey.rb
# - Allow use of pkg-config to use the same zlib for SM and pyrogenesis (https://bugzilla.mozilla.org/show_bug.cgi?id=1783570)
# - Force allowing build with older macOS SDK
# - Fix invocation of recent compiler (https://bugzilla.mozilla.org/show_bug.cgi?id=1844694)
# - Fix linker selection on macOS 15 (https://bugzilla.mozilla.org/show_bug.cgi?id=1964280)
if [ "${OS}" = "Darwin" ]; then
	patch -p1 <"${PATCHES}"/FixMacOSBuild.diff
fi

# Fix building with python 3.14
patch -p1 <"${PATCHES}"/FixPython3_14.diff

# Fix conflicts between vendored fmtlib in SM and our own use of fmtlib.
# This is not upstreamable and can be dropped when we use std::format (can be done in R30 when
# we move to ESR153 which requires C++20 anyway).
# Note: after building SM, it is still needed to rename the fmt include directory so that our
# compiler doesn't mix up the vendored fmtlib headers and the actual ones. The -FixIncludes patch
# is applied to dist/include after building.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1987410
patch -p1 <"${PATCHES}"/StashVendoredLibfmt.patch

# Cherry-pick an upstream commit to allow passing custom flags to ar
patch -p1 <"${PATCHES}"/UpstreamCustomARFlags.patch
