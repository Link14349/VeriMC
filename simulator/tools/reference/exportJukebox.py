#!/usr/bin/env python3
"""Extract fixed-version song metadata and default playable item associations."""
import json
import math
import struct
from pathlib import Path
from zipfile import ZipFile
root=Path(__file__).resolve().parents[2]
def float32(value):return struct.unpack('f',struct.pack('f',value))[0]
songs=[]
with ZipFile(root/'.cache/reference/game.jar') as jar:
    for name in sorted(jar.namelist()):
        if not name.startswith('data/minecraft/jukebox_song/') or not name.endswith('.json'):continue
        value=json.loads(jar.read(name))
        songs.append({'name':'minecraft:'+Path(name).stem,'sound':value['sound_event'],'lengthTicks':math.ceil(float32(float32(value['length_in_seconds'])*20)), 'comparatorOutput':value['comparator_output']})
items={}
for path in sorted((root/'.cache/reference/reports/reports/minecraft/components/item').glob('*.json')):
    value=json.loads(path.read_text())['components'].get('minecraft:jukebox_playable')
    if value is not None:
        if not isinstance(value,str):raise ValueError('unsupported playable component')
        items['minecraft:'+path.stem]=value
if set(items.values())!={s['name'] for s in songs}:raise ValueError('song/item registry mismatch')
(root/'data/jukeboxRules.json').write_text(json.dumps({'version':'26.2','reference':'Fixed Java 26.2 song definitions and default item component report','songs':songs,'items':items},indent=2)+'\n')
print(f'Exported {len(songs)} songs and {len(items)} playable items')
