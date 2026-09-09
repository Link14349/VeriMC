set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
export PATH="$PWD/simulator/.cache/jdk25/bin:$PATH"
cd simulator/tools/reference
python3 "$@"
