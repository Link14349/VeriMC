set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
export PATH="$PWD/simulator/.cache/jdk25/bin:$PATH"
out="${1:?output dir}"
shift
cd simulator/tools/reference
python3 probeSnapshotWitness.py "$out" "$@"
