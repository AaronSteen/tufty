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
    echo "tufty.dylib: OK"

    clang++ $CommonCompilerFlags $OptFlag -framework Cocoa -framework AudioToolbox -framework UniformTypeIdentifiers \
        "$SCRIPT_DIR/osx_tufty.mm" -o "$BUILD/tufty" || exit $?
    echo "tufty: OK"
    exit 0
else
    SCRIPT_WIN=$(wslpath -w "$SCRIPT_DIR/build_wsl.bat")

    cmd.exe /c "$SCRIPT_WIN" "$MODE" 2>&1 | sed \
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
