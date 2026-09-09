set -u
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
dir="$1"
for fixture in "$dir"/*.json; do
  printf '%-26s ' "$(basename "$fixture")"
  out=$(./simulator/buildQ/checkReference "$fixture" 2>&1)
  code=$?
  printf 'exit=%s %s\n' "$code" "$(printf '%s' "$out" | head -c 300)"
done
