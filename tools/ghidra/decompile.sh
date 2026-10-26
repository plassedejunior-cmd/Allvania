#!/bin/sh
# Decompile specific functions from the Ghidra project to stdout.
# Usage: tools/ghidra/decompile.sh 02000c78[,0208c998,...]
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HEADLESS=${GHIDRA_HEADLESS:-$(command -v analyzeHeadless || echo /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless)}
if [ -z "${JAVA_HOME:-}" ]; then
	for jdk in /opt/homebrew/opt/openjdk*/libexec/openjdk.jdk/Contents/Home; do
		[ -d "$jdk" ] && JAVA_HOME=$jdk
	done
	export JAVA_HOME
fi

# work on a throwaway copy so several decompiles can run at once without fighting over the project lock
WORK=$(mktemp -d)
OUT="$WORK/out.c"
cp -R "$ROOT/ghidra_project/." "$WORK/"
"$HEADLESS" "$WORK" ooe -process arm9.bin -noanalysis \
	-scriptPath "$ROOT/tools/ghidra" -postScript DecompileAt.java "$OUT" "$1" >/dev/null 2>&1
cat "$OUT"
rm -rf "$WORK"
