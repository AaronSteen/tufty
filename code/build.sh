#!/bin/bash
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD="$(cd "$SCRIPT_DIR/.." && pwd)/build"

# Build mode: "debug" (default, no optimization) or "release" (-O2).
# Neovim's :make passes its arguments through, so ":make release" runs "./build.sh release".
MODE="${1:-debug}"
if [ "$MODE" != "debug" ] && [ "$MODE" != "release" ]; then
    echo "build.sh: unknown mode '$MODE' (expected debug or release)"
    exit 1
fi

# Optional second word: "analyze" also runs the static analyzer after a successful
# compile. Left out, it's a plain compile. In Neovim: <leader>b = compile only,
# <leader>ab = compile + analyze (runs "./build.sh debug analyze").
ANALYZE="$2"
if [ -n "$ANALYZE" ] && [ "$ANALYZE" != "analyze" ]; then
    echo "build.sh: unknown option '$ANALYZE' (expected analyze, or nothing)"
    exit 1
fi

mkdir -p "$BUILD"

# compile_commands.json tells clangd exactly how each file is compiled, so it can
# index the whole project (this is what makes <leader>ss and gd reach files you
# haven't opened). The code that writes it is shared by all projects and lives in
# the dotfiles repo. If it's missing (e.g. a fresh clone on another machine), the
# build still works; clangd just won't see the whole project.
CDB_HELPER="$HOME/.dotfiles/scripts/compile_commands.sh"
if [ -f "$CDB_HELPER" ]; then
    source "$CDB_HELPER"
else
    echo "note: $CDB_HELPER not found, skipping compile_commands.json"
    write_compile_commands()      { :; }   # do-nothing stand-ins
    write_compile_commands_msvc() { :; }
fi

if [ "$(uname -s)" = "Darwin" ]; then
    CODE="$SCRIPT_DIR"

    if [ "$MODE" = "release" ]; then OptFlag="-O2"; else OptFlag="-O0"; fi

    # Optimization flag deliberately NOT in here; it's added per build as $OptFlag
    CommonCompilerFlags="-g -std=c++17 -fno-exceptions -fno-rtti \
        -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter \
        -Wno-writable-strings -Wno-missing-braces -Wno-sign-compare \
        -DTUFTY_INTERNAL=1 -DTUFTY_SLOW=1 -DTUFTY_OSX=1"

    # clangd gets the same flags (optimization doesn't affect how code is read).
    # Written before compiling so it's up to date even when the build fails.
    # $CommonCompilerFlags is unquoted on purpose so it splits into separate flags.
    write_compile_commands "$BUILD" clang++ "$CODE/tufty.cpp" "$CODE/osx_tufty.mm" -- $CommonCompilerFlags

    echo "build mode: $MODE"

    clang++ $CommonCompilerFlags $OptFlag -c "$CODE/tufty.cpp" -o "$BUILD/tufty.o" || exit $?
    clang++ $CommonCompilerFlags $OptFlag -dynamiclib "$BUILD/tufty.o" -o "$BUILD/tufty.dylib" || exit $?

    dsymutil "$BUILD/tufty.dylib" || exit $?
    echo "tufty.dylib: compiled OK"

    clang++ $CommonCompilerFlags $OptFlag -framework Cocoa -framework AudioToolbox -framework UniformTypeIdentifiers \
        "$SCRIPT_DIR/osx_tufty.mm" -o "$BUILD/tufty" || exit $?
    echo "tufty: compiled OK"

    # STATIC ANALYSIS (set up Nov 2026)
    # Skipped unless "analyze" was passed (see the top of this file), because it
    # adds a lot to build time.
    if [ "$ANALYZE" != "analyze" ]; then exit 0; fi
    # Runs only when every compile above succeeded (each one exits on failure), so
    # compiler errors always come first in the quickfix list and are never repeated
    # by the analyzer. --analyze runs Clang's static analyzer instead of compiling.
    # Findings print as "file:line:col: warning: ...", which Neovim's errorformat
    # already reads. They don't fail the build. -o puts each run's .plist report
    # file in build/ (otherwise it would land next to the source files).
    #
    # The analyzer's exit code is always 0, so analyze() captures its output,
    # prints it (so it still reaches the quickfix list), and then searches it with
    # grep for a real finding. -q makes grep print nothing and only report, through
    # its exit code, whether it matched. ": warning:" only matches findings that
    # point at a file location, like "tufty.h:125:5: warning: ...", and not the
    # analyzer's location-less "warning: Path diagnostic report ..." notes.
    analyze() {   # usage: analyze <label> <source file> <plist file>
        local out
        out=$(clang++ $CommonCompilerFlags --analyze "$2" -o "$3" 2>&1)
        if [ -n "$out" ]; then printf '%s\n' "$out"; fi
        if printf '%s\n' "$out" | grep -q ': warning:'; then
            echo "$1: static analysis found issues"
        else
            echo "$1: static analysis OK"
        fi
    }
    analyze "tufty.dylib" "$CODE/tufty.cpp"    "$BUILD/tufty.plist"
    analyze "tufty"       "$CODE/osx_tufty.mm" "$BUILD/osx_tufty.plist"
    exit 0
else
    SCRIPT_WIN=$(wslpath -w "$SCRIPT_DIR/build_wsl.bat")

    # build_wsl.bat compiles, then (only if asked to and the compile succeeded)
    # runs the static analyzer. Both outputs come through this same sed, so analyzer findings get
    # the same path and (line,col) -> :line:col conversion as compiler errors.
    # "$ANALYZE" is either "analyze" or empty; build_wsl.bat reads it as %2.
    cmd.exe /c "$SCRIPT_WIN" "$MODE" "$ANALYZE" 2>&1 | sed \
        -e 's|\r||g' \
        -e 's|C:\\Users\\ams56\\work\\|/mnt/c/Users/ams56/work/|g' \
        -e 's|\\|/|g' \
        -e 's|(\([0-9][0-9]*\),\([0-9][0-9]*\))|:\1:\2|g'

    STATUS=${PIPESTATUS[0]}   # must be read immediately after the pipeline above

    # build_wsl.bat leaves its compiler flags and header-folder list in build/;
    # the shared helper turns them into compile_commands.json with WSL-style paths.
    write_compile_commands_msvc "$BUILD" "$SCRIPT_DIR/tufty.cpp" "$SCRIPT_DIR/win32_tufty_handmade.cpp"

    exit $STATUS
fi
