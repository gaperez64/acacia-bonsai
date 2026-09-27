#!/bin/bash
# Every 30 s: package temperature, load, available memory, cumulative throttle counters, and
# the running leg (arm + rows written). Read-only; cheap enough not to disturb the legs.
out=/home/gperez/GIT-repos/acacia-bonsai/build_scratch/thermal/samples.tsv
label_file=/home/gperez/GIT-repos/acacia-bonsai/build_scratch/thermal/current-leg
legs=/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/campaign/perarm-m1/legs
[ -s "$out" ] || printf 'time\tpkg_c\tload1\tmem_avail_mib\tpkg_throttle\tcore_throttle_sum\tleg\trows\n' > "$out"
while true; do
  t=$(sed -n 's/^Package id 0: *+\([0-9.]*\).*/\1/p' <(sensors 2>/dev/null))
  l=$(cut -d' ' -f1 /proc/loadavg)
  m=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
  p=$(cat /sys/devices/system/cpu/cpu0/thermal_throttle/package_throttle_count)
  c=$(cat /sys/devices/system/cpu/cpu*/thermal_throttle/core_throttle_count | awk '{s+=$1} END {print s}')
  f=$(ls -t "$legs"/*/*.tsv 2>/dev/null | head -1)
  r=$([ -n "$f" ] && echo $(( $(wc -l < "$f") - 1 )))
  leg=$(basename "$(dirname "$f")")
  if [ -s "$label_file" ]; then leg=$(cat "$label_file"); r=; fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date +%FT%T)" "$t" "$l" "$m" "$p" "$c" "$leg" "$r" >> "$out"
  sleep 30
done
