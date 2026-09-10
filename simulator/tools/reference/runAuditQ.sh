set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
export PATH="$PWD/simulator/.cache/jdk25/bin:$PATH"
out="$1"; shift
python3 simulator/tools/reference/auditReference.py --output "$out" \
  --checker simulator/buildQ/checkReference "$@"
