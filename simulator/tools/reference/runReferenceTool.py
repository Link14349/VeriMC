#!/usr/bin/env python3
"""Compile and run a source-controlled Java reference extractor against the pinned JAR."""
import os
from pathlib import Path
import subprocess
import sys

rootDir = Path(__file__).resolve().parents[2]
cacheDir = rootDir / '.cache/reference'
classDir = cacheDir / 'toolClasses'
classDir.mkdir(exist_ok=True)
classPath = os.pathsep.join([str(cacheDir / 'game.jar')] + [str(p) for p in sorted((cacheDir / 'libraries').rglob('*.jar'))])
toolName = sys.argv[1] if len(sys.argv) > 1 else 'ExportReference'
sourcePath = Path(__file__).parent / (toolName + '.java')
subprocess.run(['javac', '-cp', classPath, '-d', str(classDir), str(sourcePath)], check=True)
args = sys.argv[2:] or [str(rootDir / 'data/blockStates.json')]
subprocess.run(['java', '-cp', str(classDir) + os.pathsep + classPath, toolName] + args, check=True, cwd=cacheDir)
