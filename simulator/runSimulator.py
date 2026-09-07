#!/usr/bin/env python3
"""Build and launch the native simulator with the system browser."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time
import urllib.request
import webbrowser

rootDir = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description='VeriMC simulator · C++ / Three.js')
parser.add_argument('--port', type=int, default=28765)
parser.add_argument('--no-build', action='store_true', dest='noBuild')
parser.add_argument('--no-open', action='store_true', dest='noOpen')
args = parser.parse_args()
if not 0 < args.port < 65536: parser.error('端口范围为 1–65535')
url = f'http://127.0.0.1:{args.port}'
def isReady():
    try:
        with urllib.request.urlopen(url + '/health', timeout=.4) as response: return response.read() == b'ok'
    except OSError: return False
if isReady():
    with urllib.request.urlopen(url + '/api/bootstrap', timeout=2) as response: runningService = json.load(response)
    if runningService.get('projectFileVersion') != 1:
        raise SystemExit(f'检测到旧版模拟器后台：{url}\n当前后台不支持 .vmcb 文件接口。请先保留当前工程，再在原终端停止服务，重新运行本命令。仅刷新浏览器不会更新后台。')
    print(f'模拟器已经运行：{url}\n如需重新构建，请先在原终端停止服务，或选择其他 --port。')
    if not args.noOpen: webbrowser.open(url)
    raise SystemExit(0)
if not args.noBuild:
    for dependency in ['cmake', 'npm']:
        if not shutil.which(dependency): raise SystemExit(f'缺少 {dependency}，请先阅读 simulator/README.md 安装构建依赖。')
    webDir = rootDir / 'apps/web'
    if not (webDir / 'node_modules').exists(): subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=webDir, check=True)
    subprocess.run(['npm', 'run', 'build'], cwd=webDir, check=True)
    subprocess.run(['cmake', '--preset', 'release', '-DsimulatorBuildServer=ON'], cwd=rootDir, check=True)
    subprocess.run(['cmake', '--build', '--preset', 'release', '-j', '6'], cwd=rootDir, check=True)
executable = rootDir / 'build/simulatorServer'
if not executable.exists(): raise SystemExit('尚未构建模拟器；请去掉 --no-build 后重试。')
process = subprocess.Popen([str(executable), str(args.port)], cwd=rootDir)
def openWhenReady():
    for _ in range(100):
        if process.poll() is not None: return
        if isReady():
            if not args.noOpen: webbrowser.open(url)
            return
        time.sleep(.1)
threading.Thread(target=openWhenReady, daemon=True).start()
try:
    process.wait()
except KeyboardInterrupt:
    process.terminate(); process.wait(timeout=5)
