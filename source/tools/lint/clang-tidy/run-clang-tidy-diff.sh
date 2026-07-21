#!/bin/sh

if command -v clang-tidy-diff-20.py >/dev/null; then
	ctd=clang-tidy-diff-20.py
else
	# Get path assuming install prefix is good enough.
	ctd="$(realpath "$(dirname "$(which clang)")/../share/clang/clang-tidy-diff.py")"
fi

./libraries/source/premake-core/bin/premake5 --file=build/premake/premake5.lua --minimal-flags --cc-config=Release compilecommands >/dev/null

git diff "$1" -- ':(exclude)*test*' | "${ctd}" -p1 -quiet -config-file ./source/tools/lint/clang-tidy/config.yml
