#!/usr/bin/env bash
set -euo pipefail

# Shared clang-format wrapper for both local developers and CI.
# - --check: fail when tracked C/C++ files are not formatted.
# - --fix: rewrite tracked C/C++ files in place.
#
# Keeping this logic in one script avoids drift between local and CI behavior.
MODE="${1:---check}"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format is not installed or not on PATH" >&2
  echo "hint: install clang-format and rerun 'bash tools/clang_format.sh --check'" >&2
  exit 2
fi

# Restrict formatting scope to tracked source files so generated or temporary
# workspace artifacts are never reformatted by accident.
mapfile -d '' FILES < <(
  git ls-files -z \
    '*.c' '*.cc' '*.cpp' '*.cxx' \
    '*.h' '*.hh' '*.hpp' '*.hxx'
)

if [[ "${#FILES[@]}" -eq 0 ]]; then
  echo "No tracked C/C++ files found."
  exit 0
fi

case "${MODE}" in
  --check)
    echo "Running clang-format check on ${#FILES[@]} files..."
    clang-format --dry-run --Werror "${FILES[@]}"
    echo "clang-format check passed."
    ;;
  --fix)
    echo "Applying clang-format to ${#FILES[@]} files..."
    clang-format -i "${FILES[@]}"
    echo "clang-format apply complete."
    ;;
  *)
    echo "usage: bash tools/clang_format.sh [--check|--fix]" >&2
    exit 2
    ;;
esac
