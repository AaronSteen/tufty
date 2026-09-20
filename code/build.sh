#!/bin/bash
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ "$(uname -s)" = "Darwin" ]; then
    BUILD="$SCRIPT_DIR/../build"
    CODE="$SCRIPT_DIR"

    CommonCompilerFlags="-g -O0 -std=c++17 -fno-exceptions -fno-rtti \
        -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter \
        -Wno-writable-strings -Wno-missing-braces -Wno-sign-compare \
        -DTUFTY_INTERNAL=1 -DTUFTY_SLOW=1 -DTUFTY_OSX=1"

    mkdir -p "$BUILD"

    clang++ $CommonCompilerFlags -c "$CODE/tufty.cpp" -o "$BUILD/tufty.o" || exit $?
    clang++ $CommonCompilerFlags -dynamiclib "$BUILD/tufty.o" -o "$BUILD/tufty.dylib" || exit $? 

    dsymutil "$BUILD/tufty.dylib" || exit $?
    echo "tufty.dylib: OK"


    clang++ $CommonCompilerFlags -framework Cocoa -framework AudioToolbox -framework UniformTypeIdentifiers \
        "$SCRIPT_DIR/osx_tufty.mm" -o "$BUILD/tufty" || exit $?
    echo "tufty: OK"
    exit 0
else
    SCRIPT_WIN=$(wslpath -w "$SCRIPT_DIR/build_wsl.bat")

    cmd.exe /c "$SCRIPT_WIN" 2>&1 | sed \
        -e 's|\r||g' \
        -e 's|C:\\Users\\ams56\\work\\|/mnt/c/Users/ams56/work/|g' \
        -e 's|\\|/|g' \
        -e 's|(\([0-9][0-9]*\),\([0-9][0-9]*\))|:\1:\2|g'

    exit ${PIPESTATUS[0]}
fi
