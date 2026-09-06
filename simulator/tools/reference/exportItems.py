#!/usr/bin/env python3
"""Extract stack capacities from the pinned server's generated component reports."""
import json
from pathlib import Path

rootDir = Path(__file__).resolve().parents[2]
reportDir = rootDir / '.cache/reference/reports/reports'
registry = json.loads((reportDir / 'registries.json').read_text())['minecraft:item']['entries']
items = []
for name, entry in sorted(registry.items(), key=lambda row: row[1]['protocol_id']):
    namespace, item = name.split(':')
    report = json.loads((reportDir / namespace / 'components/item' / (item + '.json')).read_text())
    items.append({'name': name, 'maxStack': report['components']['minecraft:max_stack_size']})
output = {'version': '26.2', 'reference': 'Pinned Java 26.2 server data reports, item component defaults', 'items': items}
(rootDir / 'data/itemDefinitions.json').write_text(json.dumps(output, separators=(',', ':')) + '\n')
print(f'Exported {len(items)} item capacities')
