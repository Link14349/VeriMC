set -u
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
for fixture in "$@"; do
  ./simulator/buildQ/checkReference "$fixture"
  echo "exitCode=$? fixture=$fixture"
done
