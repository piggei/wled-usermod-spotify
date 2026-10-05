#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  test_runner.sh --manifest FILE --phase prebuild|postbuild --repo DIR [--firmware FILE]

The runner is intentionally generic. Project-specific checks live only in the
TSV manifest, so adding/removing checks does not require changing this script.

Manifest columns (TAB-separated):
  ID  PHASE  LEVEL  TYPE  TARGET  EXPECTED  DESCRIPTION

LEVEL:
  required  failure stops the release workflow
  optional  failure is reported but does not fail the runner

Supported TYPE values:
  file_exists
  file_contains
  file_not_contains
  tree_contains
  tree_not_contains
  firmware_contains
  firmware_not_contains
  python_script
USAGE
}

MANIFEST=""
PHASE=""
REPO=""
FIRMWARE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest) MANIFEST="${2:-}"; shift 2 ;;
    --phase) PHASE="${2:-}"; shift 2 ;;
    --repo) REPO="${2:-}"; shift 2 ;;
    --firmware) FIRMWARE="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "$MANIFEST" ]] || { echo "ERROR: --manifest is required" >&2; exit 2; }
[[ -n "$PHASE" ]] || { echo "ERROR: --phase is required" >&2; exit 2; }
[[ -n "$REPO" ]] || { echo "ERROR: --repo is required" >&2; exit 2; }
[[ -f "$MANIFEST" ]] || { echo "ERROR: manifest not found: $MANIFEST" >&2; exit 2; }
[[ -d "$REPO" ]] || { echo "ERROR: repo/worktree not found: $REPO" >&2; exit 2; }
[[ "$PHASE" == "prebuild" || "$PHASE" == "postbuild" ]] || { echo "ERROR: invalid phase: $PHASE" >&2; exit 2; }

if [[ "$PHASE" == "postbuild" ]]; then
  [[ -n "$FIRMWARE" && -f "$FIRMWARE" ]] || { echo "ERROR: --firmware FILE is required for postbuild" >&2; exit 2; }
  command -v grep >/dev/null 2>&1 || { echo "ERROR: grep not found" >&2; exit 2; }
fi

pass=0
fail_required=0
fail_optional=0
skip=0

run_check() {
  local type="$1" target="$2" expected="$3"
  local path

  case "$type" in
    file_exists)
      path="$REPO/$target"
      [[ -e "$path" ]]
      ;;
    file_contains)
      path="$REPO/$target"
      [[ -f "$path" ]] && grep -Fq -- "$expected" "$path"
      ;;
    file_not_contains)
      path="$REPO/$target"
      [[ -f "$path" ]] && ! grep -Fq -- "$expected" "$path"
      ;;
    tree_contains)
      grep -R -I -Fq --exclude-dir=.git --exclude-dir=build --exclude="$(basename "$MANIFEST")" -- "$expected" "$REPO"
      ;;
    tree_not_contains)
      ! grep -R -I -Fq --exclude-dir=.git --exclude-dir=build --exclude="$(basename "$MANIFEST")" -- "$expected" "$REPO"
      ;;
    firmware_contains)
      # Search raw bytes directly. The previous `strings | grep -q` form produced
      # false negatives under `set -o pipefail`: once grep found a match and quit,
      # strings received SIGPIPE and made the pipeline look failed. Prefer ELF,
      # because it retains diagnostic literals more reliably, then fall back to BIN.
      local elf="${FIRMWARE%/*}/firmware.elf"
      if [[ -f "$elf" ]] && LC_ALL=C grep -aFq -- "$expected" "$elf"; then
        return 0
      fi
      LC_ALL=C grep -aFq -- "$expected" "$FIRMWARE"
      ;;
    firmware_not_contains)
      local elf="${FIRMWARE%/*}/firmware.elf"
      if [[ -f "$elf" ]] && LC_ALL=C grep -aFq -- "$expected" "$elf"; then
        return 1
      fi
      ! LC_ALL=C grep -aFq -- "$expected" "$FIRMWARE"
      ;;
    python_script)
      path="$REPO/$target"
      [[ -f "$path" ]] && python3 "$path"
      ;;
    *)
      echo "UNKNOWN_TYPE:$type"
      return 2
      ;;
  esac
}

printf '\nRelease checks: %s\n' "$PHASE"
printf '%-24s %-9s %-20s %s\n' "ID" "LEVEL" "TYPE" "RESULT"
printf '%-24s %-9s %-20s %s\n' "------------------------" "---------" "--------------------" "------"

# shellcheck disable=SC2162
while IFS=$'\t' read -r id phase level type target expected description || [[ -n "${id:-}" ]]; do
  [[ -n "${id:-}" ]] || continue
  [[ "$id" == \#* ]] && continue
  [[ "$id" == "ID" ]] && continue

  if [[ "$phase" != "$PHASE" ]]; then
    ((skip+=1))
    continue
  fi

  if output="$(run_check "$type" "$target" "$expected" 2>&1)"; then
    printf '%-24s %-9s %-20s PASS  %s\n' "$id" "$level" "$type" "$description"
    ((pass+=1))
  else
    rc=$?
    if [[ $rc -eq 2 && "$output" == UNKNOWN_TYPE:* ]]; then
      printf '%-24s %-9s %-20s FAIL  unsupported test type: %s\n' "$id" "$level" "$type" "$type"
    else
      printf '%-24s %-9s %-20s FAIL  %s\n' "$id" "$level" "$type" "$description"
      [[ -n "$output" ]] && printf '    %s\n' "$output"
    fi
    if [[ "$level" == "required" ]]; then
      ((fail_required+=1))
    else
      ((fail_optional+=1))
    fi
  fi
done < "$MANIFEST"

printf '\nSummary: PASS=%d required-fail=%d optional-fail=%d skipped-other-phase=%d\n' \
  "$pass" "$fail_required" "$fail_optional" "$skip"

[[ $fail_required -eq 0 ]]
