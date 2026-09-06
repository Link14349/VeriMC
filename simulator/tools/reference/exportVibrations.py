#!/usr/bin/env python3
"""Extract vibration registry observations and recursively resolved vanilla tags."""
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile

root=Path(__file__).resolve().parents[2]
observations=root/'.cache/reference/vibrationRegistry.json'
subprocess.run(['python3',str(Path(__file__).with_name('runReferenceTool.py')),'ExportVibrations',str(observations)],check=True)
data=json.loads(observations.read_text())
with ZipFile(root/'.cache/reference/game.jar') as jar:
    def tag(kind,name,parents=()):
        if name in parents:raise ValueError('cyclic reference tag '+name)
        namespace,path=name.split(':',1);result=set()
        for entry in json.loads(jar.read(f'data/{namespace}/tags/{kind}/{path}.json'))['values']:
            if not isinstance(entry,str):raise ValueError('unsupported optional tag entry')
            if entry.startswith('#'):result.update(tag(kind,entry[1:],parents+(name,)))
            else:result.add(entry)
        return result
    vibrations=tag('game_event','minecraft:vibrations');sneaking=tag('game_event','minecraft:ignore_vibrations_sneaking')
    for row in data['events']:
        row['listenable']=row['name'] in vibrations;row['ignoreSneaking']=row['name'] in sneaking
    for name in ['occludes_vibration_signals','dampens_vibrations','vibration_resonators']:
        data[name]=sorted(tag('block','minecraft:'+name))
data['reference']='Fixed Java 26.2 registry, VibrationSystem frequencies and built-in tags'
(root/'data/vibrationRules.json').write_text(json.dumps(data,indent=2)+'\n')
