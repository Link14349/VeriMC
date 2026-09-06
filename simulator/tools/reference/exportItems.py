#!/usr/bin/env python3
"""Extract stack capacities from the pinned server's generated component reports."""
import json
import zipfile
from pathlib import Path

rootDir = Path(__file__).resolve().parents[2]
reportDir = rootDir / '.cache/reference/reports/reports'
registry = json.loads((reportDir / 'registries.json').read_text())['minecraft:item']['entries']
items = []
with zipfile.ZipFile(rootDir / '.cache/reference/game.jar') as jar:
    bookshelfBooks = set(json.loads(jar.read('data/minecraft/tags/item/bookshelf_books.json'))['values'])
    if any(name.startswith('#') for name in bookshelfBooks): raise ValueError('Resolve nested bookshelf item tags before exporting')
for name, entry in sorted(registry.items(), key=lambda row: row[1]['protocol_id']):
    namespace, item = name.split(':')
    report = json.loads((reportDir / namespace / 'components/item' / (item + '.json')).read_text())
    row = {'name': name, 'maxStack': report['components']['minecraft:max_stack_size']}
    if name in bookshelfBooks: row['bookshelfBook'] = True
    items.append(row)
output = {'version': '26.2', 'reference': 'Pinned Java 26.2 server data reports, item component defaults and bookshelf_books tag', 'items': items}
(rootDir / 'data/itemDefinitions.json').write_text(json.dumps(output, separators=(',', ':')) + '\n')
print(f'Exported {len(items)} item capacities')
