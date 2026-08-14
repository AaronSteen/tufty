#!/bin/bash
SCRIPT_DIR=$(dirname "$(realpath "$0")")
SCRIPT_WIN=$(wslpath -w "$SCRIPT_DIR/build_wsl.bat")

cmd.exe /c "$SCRIPT_WIN" 2>&1 | sed \
    -e 's|\r||g' \
    -e 's|C:\\Users\\ams56\\work\\|/mnt/c/Users/ams56/work/|g' \
    -e 's|\\|/|g' \
    -e 's|(\([0-9][0-9]*\),\([0-9][0-9]*\))|:\1:\2|g'

exit ${PIPESTATUS[0]}
