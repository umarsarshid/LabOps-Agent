#!/usr/bin/env bash
set -euo pipefail

# Fails the build when changed files look like proprietary SDK payloads.
#
# Default mode: checks files changed in git diff (PR/push aware when CI env is
# available, fallback to HEAD~1..HEAD locally).
#
# Optional mode: pass paths as args to check explicit file names.

choose_diff_base() {
  if [[ "${GITHUB_EVENT_NAME:-}" == "pull_request" && -n "${GITHUB_BASE_REF:-}" ]]; then
    echo "origin/${GITHUB_BASE_REF}"
    return 0
  fi

  if [[ -n "${GITHUB_EVENT_BEFORE:-}" && "${GITHUB_EVENT_BEFORE}" != "0000000000000000000000000000000000000000" ]]; then
    echo "${GITHUB_EVENT_BEFORE}"
    return 0
  fi

  if git rev-parse --verify HEAD~1 >/dev/null 2>&1; then
    echo "HEAD~1"
    return 0
  fi

  echo ""
}

is_forbidden_sdk_path() {
  local path="$1"

  local lower
  lower="$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')"

  case "$lower" in
    vendorsdk/*|*/vendorsdk/*|vendor_sdk/*|*/vendor_sdk/*)
      return 0
      ;;
    *.dll|*.so|*.so.*|*.dylib|*.lib|*.a|*.obj|*.o|*.pdb|*.exp|*.framework|*.xcframework)
      return 0
      ;;
  esac

  return 1
}

collect_paths_to_check() {
  if [[ "$#" -gt 0 ]]; then
    printf '%s\n' "$@"
    return 0
  fi

  local base_ref
  base_ref="$(choose_diff_base)"

  if [[ -z "$base_ref" ]]; then
    # No baseline commit (for example first commit in a new repo). No diff to
    # evaluate; treat as pass.
    return 0
  fi

  if [[ "$base_ref" == origin/* ]]; then
    local remote_branch="${base_ref#origin/}"
    git fetch --no-tags --depth=1 origin "$remote_branch" >/dev/null 2>&1 || true
  fi

  git diff --name-only --diff-filter=ACMR "${base_ref}...HEAD"
}

main() {
  local paths=()
  local line
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    paths+=("$line")
  done < <(collect_paths_to_check "$@")

  if [[ "${#paths[@]}" -eq 0 ]]; then
    echo "anti-leak: no changed files to scan"
    exit 0
  fi

  local offenders=()
  local path
  for path in "${paths[@]}"; do
    [[ -z "$path" ]] && continue
    if is_forbidden_sdk_path "$path"; then
      offenders+=("$path")
    fi
  done

  if [[ "${#offenders[@]}" -eq 0 ]]; then
    echo "anti-leak: passed (no forbidden SDK patterns in changed files)"
    exit 0
  fi

  echo "anti-leak: FAILED"
  echo "The following changed paths match forbidden SDK patterns:"
  for path in "${offenders[@]}"; do
    echo "  - ${path}"
  done
  echo
  echo "Never commit vendor SDK headers/binaries/libraries."
  exit 1
}

main "$@"
