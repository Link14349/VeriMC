set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
cmakeBin='/mnt/f/学习和研究/VeriMC-spatial-work/simulator/.cache/cmake-3.30.5-linux-x86_64/bin/cmake'
prefix='/mnt/f/学习和研究/VeriMC-spatial-work/simulator/.cache/nativePrefix'
"$cmakeBin" -S simulator -B simulator/buildL -DCMAKE_BUILD_TYPE=Release -DsimulatorBuildServer=OFF -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$prefix"
"$cmakeBin" --build simulator/buildL --target checkReference -j 6
