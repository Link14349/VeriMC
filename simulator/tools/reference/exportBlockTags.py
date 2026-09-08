#!/usr/bin/env python3
"""Resolve the built-in block tags the kernel needs straight from the pinned JAR.

`Bootstrap.bootStrap()` alone does not load datapack tags, so `BlockTags.WALLS` is empty in
the registry exporter. The tag files themselves are inside the fixed JAR, so they are read
and resolved here, exactly like `exportVibrations.py` does for game-event tags.
"""
import json
from pathlib import Path
from zipfile import ZipFile

root = Path(__file__).resolve().parents[2]
WANTED = ['walls']

with ZipFile(root / '.cache/reference/game.jar') as jar:
    def tag(name, parents=()):
        if name in parents:
            raise ValueError('cyclic reference tag ' + name)
        namespace, path = name.split(':', 1)
        result = set()
        for entry in json.loads(jar.read(f'data/{namespace}/tags/block/{path}.json'))['values']:
            if not isinstance(entry, str):
                raise ValueError('unsupported optional tag entry')
            if entry.startswith('#'):
                result.update(tag(entry[1:], parents + (name,)))
            else:
                result.add(entry)
        return result

    data = {'version': '26.2', 'reference': 'Fixed Java 26.2 built-in block tags, resolved recursively'}
    for name in WANTED:
        data[name] = sorted(tag('minecraft:' + name))

(root / 'data/blockTags.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
print('Exported ' + ', '.join(f'{name}={len(data[name])}' for name in WANTED))
