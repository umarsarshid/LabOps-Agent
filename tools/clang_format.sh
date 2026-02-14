#!/usr/bin/env bash
set -euo pipefail

# Shared clang-format wrapper for both local developers and CI.
# - --check: fail when tracked C/C++ files are not formatted.
# - --fix: rewrite tracked C/C++ files in place.
#
# Keeping this logic in one script avoids drift between local and CI behavior.
MODE="${1:---check}"
CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-clang-format}"
CLANG_FORMAT_REQUIRED_MAJOR="${CLANG_FORMAT_REQUIRED_MAJOR:-}"

if ! command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1; then
  echo "error: formatter '${CLANG_FORMAT_BIN}' is not installed or not on PATH" >&2
  echo "hint: install clang-format and rerun 'bash tools/clang_format.sh --check'" >&2
  echo "hint: optionally set CLANG_FORMAT_BIN=clang-format-<major>" >&2
  exit 2
fi

# CI and local machines can have different default formatter binaries.
# Exposing the selected executable in logs makes formatter drift visible quickly.
VERSION_TEXT="$("${CLANG_FORMAT_BIN}" --version)"
echo "Using formatter: ${CLANG_FORMAT_BIN} (${VERSION_TEXT})"

if [[ -n "${CLANG_FORMAT_REQUIRED_MAJOR}" ]]; then
  DETECTED_MAJOR="$(echo "${VERSION_TEXT}" | sed -E 's/.*version ([0-9]+).*/\1/' | head -n 1)"
  if [[ -z "${DETECTED_MAJOR}" ]]; then
    echo "error: unable to parse clang-format major version from: ${VERSION_TEXT}" >&2
    exit 2
  fi
  if [[ "${DETECTED_MAJOR}" != "${CLANG_FORMAT_REQUIRED_MAJOR}" ]]; then
    echo "error: expected clang-format major ${CLANG_FORMAT_REQUIRED_MAJOR}, got ${DETECTED_MAJOR}" >&2
    echo "hint: set CLANG_FORMAT_BIN to a matching binary for this repo" >&2
    exit 2
  fi
fi

# Restrict formatting scope to tracked source files so generated or temporary
# workspace artifacts are never reformatted by accident.
# Use a read loop instead of `mapfile` so this works on macOS default Bash 3.x.
FILES=()
while IFS= read -r -d '' file; do
  FILES+=("${file}")
done < <(
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
    "${CLANG_FORMAT_BIN}" --dry-run --Werror "${FILES[@]}"
    echo "clang-format check passed."
    ;;
  --fix)
    echo "Applying clang-format to ${#FILES[@]} files..."
    "${CLANG_FORMAT_BIN}" -i "${FILES[@]}"
    echo "clang-format apply complete."
    ;;
  *)
    echo "usage: bash tools/clang_format.sh [--check|--fix]" >&2
    exit 2
    ;;
esac
