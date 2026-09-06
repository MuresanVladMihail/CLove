#!/bin/sh
# Point git at the versioned hooks in this directory.
#
#   .githooks/install.sh
#
# To undo: git config --unset core.hooksPath
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
echo "Installed git hooks from .githooks (core.hooksPath)."
echo "  pre-commit: builds build/ and runs ./tests/run_tests.sh"
echo "Uninstall with: git config --unset core.hooksPath"
