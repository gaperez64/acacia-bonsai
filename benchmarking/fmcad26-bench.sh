#!/bin/zsh -f
#
# fmcad26-bench.sh — orchestrate self-benchmark.sh over two configuration
# sets and their suites, convert logs to mkplot format, clone mkplot into a
# temporary location to produce cactus plots, and print PAR-2 rankings for
# each run.
#
# Quick-check mode (-q CONF) runs a single configuration on the default
# suite only, producing one plot + ranking — a fast sanity check of the
# data-generation and plotting pipeline.

emulate -L zsh

SCRIPT_NAME=${0:t}
SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}

SUITE_DEFAULT=ab/syntcomp21/crit
SUITE_SYNTCOMP24=ab/syntcomp24/0s-20s
MKPLOT_REPO=https://github.com/alexeyignatiev/mkplot.git

SET1_CONFS=(best_mona best_decomp_mona base_iosprecom_mona
            best_decomp_kdtree_mona best_decomp_sharingtrie_mona ltlsynt)
SET2_CONFS=(ltlsynt best_decomp_mona best_decomp_kdtree_mona
            best_decomp_sharingtrie_mona base_iosprecom_mona)

TIMEOUT_FACTOR=1.7
quick_conf=
native_file=

usage() {
  cat <<EOF
usage: $SCRIPT_NAME [-h] [-q CONF] [-t FACTOR] [-n NATIVE_FILE]
  -h              Print this message.
  -q CONF         Quick-check: run only CONF on $SUITE_DEFAULT and plot that alone.
  -t FACTOR       Timeout factor passed to meson test (default: $TIMEOUT_FACTOR).
  -n NATIVE_FILE  Path to a meson native file passed to \`meson setup\` via self-benchmark.sh.
EOF
}

while getopts "hq:t:n:" opt; do
  case $opt in
    h) usage; exit 0 ;;
    q) quick_conf=$OPTARG ;;
    t) TIMEOUT_FACTOR=$OPTARG ;;
    n) native_file=$OPTARG ;;
    *) usage >&2; exit 2 ;;
  esac
done

typeset -a runs
if [[ -n $quick_conf ]]; then
  runs=("quick-${quick_conf}|${SUITE_DEFAULT}|${quick_conf}")
else
  runs=(
    "set1|${SUITE_DEFAULT}|${(j:,:)SET1_CONFS}"
    "set2|${SUITE_SYNTCOMP24}|${(j:,:)SET2_CONFS}"
  )
fi

cd $REPO_ROOT

# -- benchmark phase ----------------------------------------------------------
# Each run: wipe _bm-logs/ so stale JSONs don't leak, clear per-config
# 'benchmarked' sentinels so configs appearing in multiple sets (e.g.
# ltlsynt, best_decomp_mona) re-run under the current suite, invoke
# self-benchmark.sh with -f so one bad config doesn't halt the whole set,
# then rename _bm-logs/ to a per-run directory to preserve it.
for run in $runs; do
  parts=("${(@s:|:)run}")
  label=$parts[1]; suite=$parts[2]; conflist=$parts[3]

  print "============================================================"
  print "== Run: $label  (suite=$suite)"
  print "== Configs: $conflist"
  print "============================================================"

  for conf in "${(@s:,:)conflist}"; do
    rm -f build_$conf/benchmarked
  done

  rm -rf _bm-logs

  native_flag=()
  [[ -n $native_file ]] && native_flag=(-n "$native_file")
  if ! ./self-benchmark.sh -b $suite -c $conflist -t $TIMEOUT_FACTOR -f $native_flag; then
    print -u2 "WARN: self-benchmark.sh returned non-zero for run '$label'; continuing."
  fi

  logs_dir=_bm-logs-fmcad26-${label}
  rm -rf $logs_dir
  if [[ -d _bm-logs ]]; then
    mv _bm-logs $logs_dir
    print "Logs for $label: $REPO_ROOT/$logs_dir"
  else
    print -u2 "ERROR: _bm-logs/ missing after run '$label'. Skipping."
  fi
done

# -- mkplot checkout ----------------------------------------------------------
mkplot_dir=$(mktemp -d -t mkplot.XXXXXX)
trap 'rm -rf "$mkplot_dir"' EXIT

print
print "Cloning mkplot into $mkplot_dir..."
if ! git clone --depth 1 $MKPLOT_REPO "$mkplot_dir"; then
  print -u2
  print -u2 "============================================================"
  print -u2 "ERROR: failed to clone $MKPLOT_REPO"
  print -u2 "============================================================"
  print -u2
  print -u2 "Check network connectivity and whether the upstream repo is reachable:"
  print -u2 "    git clone --depth 1 $MKPLOT_REPO /tmp/mkplot-test"
  print -u2
  print -u2 "Benchmark logs have been preserved and are untouched:"
  for run in $runs; do
    parts=("${(@s:|:)run}")
    print -u2 "  $REPO_ROOT/_bm-logs-fmcad26-${parts[1]}"
  done
  print -u2
  print -u2 "Once mkplot is available, regenerate plots manually:"
  print -u2 "    mkdir -p <logs_dir>/mkplottable"
  print -u2 "    for f in <logs_dir>/*.json; do"
  print -u2 "      $SCRIPT_DIR/meson-to-mkplot.sh \"\${f:t:r}\" \"\$f\" \\"
  print -u2 "        > <logs_dir>/mkplottable/\${f:t}"
  print -u2 "    done"
  print -u2 "    python3 <path-to-mkplot>/mkplot.py --lloc='upper left' \\"
  print -u2 "        --ymin=1e-2 --ylog -b pdf --save-to plot.pdf \\"
  print -u2 "        <logs_dir>/mkplottable/*.json"
  print -u2
  print -u2 "PAR-2 rankings can still be generated right now:"
  for run in $runs; do
    parts=("${(@s:|:)run}")
    print -u2 "    $SCRIPT_DIR/rank_bm_logs.py _bm-logs-fmcad26-${parts[1]}"
  done
  exit 1
fi

# -- mkplottable + cactus plot + PAR-2 ----------------------------------------
for run in $runs; do
  parts=("${(@s:|:)run}")
  label=$parts[1]
  logs_dir=_bm-logs-fmcad26-${label}
  mkplottable_dir=${logs_dir}/mkplottable
  plot_pdf=fmcad26-${label}.pdf

  print
  print "------------------------------------------------------------"
  print ">> Processing $label"
  print "------------------------------------------------------------"

  if [[ ! -d $logs_dir ]]; then
    print -u2 "Skipping $label: $logs_dir missing."
    continue
  fi

  mkdir -p $mkplottable_dir
  for f in ${logs_dir}/*.json(N); do
    $SCRIPT_DIR/meson-to-mkplot.sh "${f:t:r}" "$f" > "$mkplottable_dir/${f:t}"
  done

  if ! python3 "$mkplot_dir/mkplot.py" --lloc='upper left' --ymin=1e-2 --ylog \
         -b pdf --save-to "$plot_pdf" "$mkplottable_dir"/*.json; then
    print -u2 "WARN: mkplot.py failed for $label."
  else
    print "Plot: $REPO_ROOT/$plot_pdf"
  fi

  print
  print "== PAR-2 ranking for $label =="
  $SCRIPT_DIR/rank_bm_logs.py $logs_dir
  print
done

print "All done."
