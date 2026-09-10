#!/usr/bin/env bash
#
# Remove build directories and benchmark logs that nothing refers to.
#
# A working tree accumulates one build_<preset>/ per configuration ever
# measured and one _bm-logs.<campaign>/ per campaign ever run. They are all
# gitignored, so they never show up in git status, and they are individually
# large. On the tree this was written for they were 38 GB of a 42 GB checkout.
#
# What makes this worth being careful about: some of those directories are not
# scratch. G1 needs a baseline binary built from the previous revision at the
# candidate's configuration, and G3 needs paired binaries; a release build
# cannot be reproduced bit-for-bit afterwards, because the release profile
# compiles with -march=native and the binary embeds a git-derived version. A
# deleted baseline is gone.
#
# So a directory is kept when its name appears in any tracked file, or in a
# Markdown file at the top of the tree (which catches a handaround note that
# has not been committed yet), or when it is build_<member> for a member of the
# docker_default group. Everything else is a candidate.
#
#   scripts/prune-artifacts.sh              # report only, deletes nothing
#   scripts/prune-artifacts.sh --manifest F # also write the candidate list to F
#   scripts/prune-artifacts.sh --delete     # actually remove them
#
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

delete=0
manifest=""
while (( $# )); do
  case $1 in
    --delete) delete=1 ;;
    --manifest) manifest=${2:?--manifest needs a path}; shift ;;
    -h|--help) sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

# Everything that could name a directory: tracked files, plus loose Markdown at
# the top of the tree. git ls-files also lists paths that are not on disk --
# tests/syntcomp-benchmarks is a submodule with update = none -- so only the
# files that actually exist are searched.
referenced=$(mktemp); trap 'rm -f "$referenced"' EXIT
# Manifests from earlier runs list every directory by name, so searching them
# would make every directory look referenced and the script would never remove
# anything again.
haystack () {
  { git ls-files; ls -1 ./*.md 2>/dev/null || true; } \
    | grep -v '^benchmarking/artifact-prune-.*\.txt$' \
    | while read -r f; do [[ -f $f ]] && printf '%s\0' "$f"; done
}

haystack \
  | xargs -0 grep -ohE '(build|_bm-logs)[A-Za-z0-9._-]*' 2>/dev/null \
  | sort -u > "$referenced" || true

# The shipped configurations' builds are what the Docker wrappers run.
while read -r preset; do
  [[ -n $preset ]] && echo "build_$preset"
done < <(python3 scripts/acacia-config.py list-group docker_default) >> "$referenced"
sort -u -o "$referenced" "$referenced"

# A directory is also referenced when a committed file records the SHA-256 of
# the binary inside it. Campaign results carry a binary_sha256 column, and
# benchmarking/README.md pins the frozen G1 baseline by hash; either way, that
# is the binary a published measurement came from, so it is not scratch.
hashes=$(mktemp); trap 'rm -f "$referenced" "$hashes"' EXIT
haystack \
  | xargs -0 grep -ohE '\b[0-9a-f]{64}\b' 2>/dev/null \
  | sort -u > "$hashes" || true

keep=(); prune=()
for d in build* _bm-logs*; do
  [[ -d $d ]] || continue
  if grep -qxF "$d" "$referenced"; then
    keep+=("$d")
    continue
  fi
  binary="$d/src/acacia-bonsai"
  if [[ -f $binary ]] \
     && grep -qxF "$(sha256sum "$binary" | cut -d' ' -f1)" "$hashes"; then
    keep+=("$d")
    continue
  fi
  # A write-protected directory is frozen evidence: someone took the write bit
  # off on purpose so that a campaign's logs could not be edited or lost.
  # Honour that rather than discovering it as a pile of rm errors.
  if [[ ! -w $d ]]; then
    keep+=("$d")
    continue
  fi
  prune+=("$d")
done

human () { numfmt --to=iec --suffix=B "$1" 2>/dev/null || echo "$1"; }
size_of () { du -sb --exclude=.git -- "$1" 2>/dev/null | cut -f1 || echo 0; }

total=0
for d in "${prune[@]:-}"; do
  [[ -n $d ]] || continue
  total=$(( total + $(size_of "$d") ))
done

echo "kept:  ${#keep[@]} directories (named by a tracked file, a top-level note, or docker_default)"
echo "prune: ${#prune[@]} directories, $(human "$total")"

if [[ -n $manifest ]]; then
  {
    echo "# Artifact prune manifest"
    echo "# generated $(date -u +%Y-%m-%dT%H:%M:%SZ) at $(git rev-parse --short HEAD)"
    echo "# These directories were removed. They were gitignored build output and"
    echo "# campaign logs that no tracked file, top-level note, or shipped"
    echo "# configuration referred to."
    echo
    echo "## kept"
    printf '%s\n' "${keep[@]:-}" | sed '/^$/d' | sort
    echo
    echo "## removed"
    for d in "${prune[@]:-}"; do
      [[ -n $d ]] || continue
      printf '%s\t%s\n' "$(human "$(size_of "$d")")" "$d"
    done | sort -k2
  } > "$manifest"
  echo "manifest: $manifest"
fi

if (( delete )); then
  removed=0; failed=0
  for d in "${prune[@]:-}"; do
    [[ -n $d ]] || continue
    # One unremovable directory must not abandon the rest of the list.
    if rm -rf -- "$d" 2>/dev/null && [[ ! -e $d ]]; then
      removed=$(( removed + 1 ))
    else
      failed=$(( failed + 1 ))
      echo "could not remove $d" >&2
    fi
  done
  echo "removed $removed directories, reclaimed $(human "$total")"
  (( failed == 0 )) || echo "left $failed in place; see the messages above" >&2
else
  echo "(nothing deleted; pass --delete)"
fi
