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

# Writes $BUILD/compile_commands.json, which tells clangd exactly how each file is
# compiled so it can index the whole project (this is what makes <leader>ss see
# files you haven't opened). clangd finds it because it checks build/ folders.
#   Usage: write_compile_commands <compiler> <source files...>
#   Compiler flags come from the CDB_ARGS array.
# Assumes no flag or path contains a " or \ (true here), so values go into the JSON as-is.
# The file is only replaced when its contents change, so clangd doesn't re-index for nothing.
write_compile_commands() {
    local compiler="$1"; shift
    local out="$BUILD/compile_commands.json"
    local tmp="$out.tmp"
    local sep=""
    local file arg
    {
        echo "["
        for file in "$@"; do
            printf '%s  {\n    "directory": "%s",\n    "file": "%s",\n    "arguments": ["%s"' \
                "$sep" "$BUILD" "$file" "$compiler"
            for arg in "${CDB_ARGS[@]}"; do
                printf ', "%s"' "$arg"
            done
            printf ', "%s"]\n  }' "$file"
            sep=$',\n'
        done
        printf '\n]\n'
    } > "$tmp"

    if cmp -s "$tmp" "$out"; then
        rm -f "$tmp"
    else
        mv -f "$tmp" "$out"
    fi
}

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
    CDB_ARGS=($CommonCompilerFlags)
    write_compile_commands clang++ "$CODE/tufty.cpp" "$CODE/osx_tufty.mm"

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

    # build_wsl.bat leaves two small files behind: its compiler flags, and the list of
    # header folders (Windows SDK + MSVC) that vcvarsall.bat set up. clangd runs inside
    # WSL and can't see either on its own, so turn them into compile_commands.json
    # with WSL-style paths.
    if [ -f "$BUILD/clangd_flags.txt" ] && [ -f "$BUILD/clangd_include.txt" ]; then
        read -r FLAGS_LINE < "$BUILD/clangd_flags.txt"
        read -r INCLUDE_LINE < "$BUILD/clangd_include.txt"
        FLAGS_LINE=${FLAGS_LINE%$'\r'}       # strip Windows line ending
        INCLUDE_LINE=${INCLUDE_LINE%$'\r'}

        # --driver-mode=cl : read the flags the way clang-cl does (-Od, -W4, -MTd, ...)
        # --target=...     : treat the code as a Windows program, though clangd runs on Linux
        # -D_ALLOW_...     : stops MSVC's headers refusing a clang older than they expect
        # $FLAGS_LINE is unquoted on purpose so it splits into separate flags.
        CDB_ARGS=(--driver-mode=cl --target=x86_64-pc-windows-msvc \
                  -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH $FLAGS_LINE)

        # INCLUDE is a ;-separated list of Windows paths. -imsvc marks each as a system
        # header folder (the way clang-cl treats the real INCLUDE variable).
        IFS=';' read -r -a INCLUDE_DIRS <<< "$INCLUDE_LINE"
        for dir in "${INCLUDE_DIRS[@]}"; do
            if [ -n "$dir" ]; then
                CDB_ARGS+=(-imsvc "$(wslpath -u "$dir")")
            fi
        done

        write_compile_commands clang-cl "$SCRIPT_DIR/tufty.cpp" "$SCRIPT_DIR/win32_tufty_handmade.cpp"
    fi

    exit $STATUS
fi
