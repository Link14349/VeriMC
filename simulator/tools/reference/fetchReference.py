#!/usr/bin/env python3
"""Download the pinned official reference into an ignored cache; verify every artifact."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

simulatorRoot = Path(__file__).resolve().parents[2]
referenceInfo = json.loads((simulatorRoot / 'data/referenceVersion.json').read_text())
cacheDir = simulatorRoot / '.cache/reference'
cacheDir.mkdir(parents=True, exist_ok=True)

def fetchChecked(url, destination, expectedHash):
    if destination.exists() and hashlib.sha1(destination.read_bytes()).hexdigest() == expectedHash:
        return
    tempPath = destination.with_suffix('.partial')
    urllib.request.urlretrieve(url, tempPath)
    if hashlib.sha1(tempPath.read_bytes()).hexdigest() != expectedHash:
        tempPath.unlink()
        raise RuntimeError('Official artifact checksum mismatch')
    tempPath.replace(destination)

fetchChecked(referenceInfo['versionManifestUrl'], cacheDir / 'version.json', referenceInfo['versionManifestSha1'])
fetchChecked(referenceInfo['serverUrl'], cacheDir / 'server.jar', referenceInfo['serverSha1'])
with zipfile.ZipFile(cacheDir / 'server.jar') as bundle:
    versionJar = [name for name in bundle.namelist() if name.startswith('META-INF/versions/') and name.endswith('.jar')]
    if len(versionJar) != 1:
        raise RuntimeError('Unexpected server bundle layout')
    (cacheDir / 'game.jar').write_bytes(bundle.read(versionJar[0]))
print('Reference verified:', referenceInfo['edition'], referenceInfo['version'])
