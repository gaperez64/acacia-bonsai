#!/bin/zsh -f
#
# Run the two-axis comparison requested for the preprocessing work:
#   1. Acacia-Bonsai on the original LTL benchmark files.
#   2. Acacia-Bonsai on TLSF specs decomposed through tlsfcompose first.
# Each axis is evaluated with Spot NBA fast paths disabled, deterministic-only,
# and deterministic+GFG-decision enabled.

emulate -L zsh

SCRIPT_NAME=${0:t}
SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}

TIMEOUT_FACTOR=1.7
NATIVE_FILE=
CONFS=(best_decomp_mona_spotfast_off best_decomp_mona_spotfast_det best_decomp_mona_spotfast_all)
BASE_SUITES=(ab/realizable/0-1s ab/unrealizable/0-1s)
PREP_SUITES=(ab_tlsfprep/realizable/0-1s ab_tlsfprep/unrealizable/0-1s)

usage() {
  cat <<EOF
usage: $SCRIPT_NAME [-h] [-t FACTOR] [-n NATIVE_FILE] [-b SUITE[,SUITE]] [-p SUITE[,SUITE]]
  -h              Print this message.
  -t FACTOR       Timeout factor passed to meson test (default: $TIMEOUT_FACTOR).
  -n NATIVE_FILE  Path to a meson native file passed through self-benchmark.sh.
  -b SUITES       Comma-separated baseline suites (default: ${(j:,:)BASE_SUITES}).
  -p SUITES       Comma-separated TLSF-preprocessed suites (default: ${(j:,:)PREP_SUITES}).
EOF
}

while getopts "ht:n:b:p:" opt; do
  case $opt in
    h) usage; exit 0 ;;
    t) TIMEOUT_FACTOR=$OPTARG ;;
    n) NATIVE_FILE=$OPTARG ;;
    b) BASE_SUITES=("${(@s:,:)OPTARG}") ;;
    p) PREP_SUITES=("${(@s:,:)OPTARG}") ;;
    *) usage >&2; exit 2 ;;
  esac
done

build_tlsf_oxidd() {
  local dir=$REPO_ROOT/subprojects/tlsf-tools
  if [[ ! -d $dir ]]; then
    print -u2 "ERROR: tlsf-tools submodule is missing at $dir"
    return 2
  fi

  if [[ -x $dir/build-oxidd/tlsfcompose ]]; then
    return 0
  fi

  print "Building OxiDD-enabled tlsf-tools in $dir/build-oxidd..."
  (cd $dir && git submodule update --init --recursive external/oxidd) || return $?
  (cd $dir && scripts/build_oxidd.sh) || return $?
  if [[ -d $dir/build-oxidd/meson-private ]]; then
    (cd $dir && meson setup build-oxidd -Doxidd=enabled --buildtype=release --reconfigure) || return $?
  else
    (cd $dir && meson setup build-oxidd -Doxidd=enabled --buildtype=release) || return $?
  fi
  (cd $dir && meson compile -C build-oxidd) || return $?
}

run_eval() {
  local label=$1
  shift
  local suites=("$@")
  local suite_csv=${(j:,:)suites}
  local conf_csv=${(j:,:)CONFS}
  local native_flag=()

  print "============================================================"
  print "== Run: $label"
  print "== Suites: $suite_csv"
  print "== Configs: $conf_csv"
  print "============================================================"

  for conf in $CONFS; do
    rm -f $REPO_ROOT/build_$conf/benchmarked
  done

  rm -rf $REPO_ROOT/_bm-logs
  [[ -n $NATIVE_FILE ]] && native_flag=(-n "$NATIVE_FILE")

  if ! (cd $REPO_ROOT && ./self-benchmark.sh -b $suite_csv -c $conf_csv -t $TIMEOUT_FACTOR -f $native_flag); then
    print -u2 "WARN: self-benchmark.sh returned non-zero for $label; preserving whatever logs were produced."
  fi

  local logs_dir=$REPO_ROOT/_bm-logs-preprocessing-${label}
  rm -rf $logs_dir
  if [[ -d $REPO_ROOT/_bm-logs ]]; then
    mv $REPO_ROOT/_bm-logs $logs_dir
    print "Logs for $label: $logs_dir"
    $SCRIPT_DIR/rank_bm_logs.py $logs_dir || true
  else
    print -u2 "ERROR: _bm-logs/ missing after $label."
    return 1
  fi
}

cd $REPO_ROOT
build_tlsf_oxidd || exit $?
run_eval baseline $BASE_SUITES || exit $?
run_eval tlsfprep $PREP_SUITES || exit $?
