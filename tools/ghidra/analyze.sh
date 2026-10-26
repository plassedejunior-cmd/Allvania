#!/bin/sh
# Import the extracted ARM9 into a headless Ghidra project, run auto-analysis and export the function list.
# Usage: tools/ghidra/analyze.sh   (run from the repo root after tools/extract_rom.py)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HEADLESS=${GHIDRA_HEADLESS:-$(command -v analyzeHeadless || echo /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless)}

# headless Ghidra cannot prompt for a JDK, so default to Homebrew's when JAVA_HOME is unset
if [ -z "${JAVA_HOME:-}" ]; then
	for jdk in /opt/homebrew/opt/openjdk*/libexec/openjdk.jdk/Contents/Home; do
		[ -d "$jdk" ] && JAVA_HOME=$jdk
	done
	export JAVA_HOME
fi

mkdir -p "$ROOT/ghidra_project" "$ROOT/build"
"$HEADLESS" "$ROOT/ghidra_project" ooe \
	-import "$ROOT/extracted/arm9/arm9.bin" -overwrite \
	-loader BinaryLoader -loader-baseAddr 0x02000000 \
	-processor ARM:LE:32:v5t \
	-scriptPath "$ROOT/tools/ghidra" \
	-preScript SetupNdsMemory.java "$ROOT/extracted" \
	-postScript ApplySymbols.java "$ROOT/config/symbols_arm9.csv" \
	-postScript ExportFunctions.java "$ROOT/build/functions.csv" \
	-analysisTimeoutPerFile 3600
