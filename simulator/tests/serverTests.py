#!/usr/bin/env python3
"""End-to-end local HTTP/WebSocket tests, using only the Python standard library."""
import sys
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)
command = client.command
original = command('save', checkpoint=True)
try:
    command('new')
    blocks = [{'pos': [x,0,0], 'name': 'stone'} for x in range(5)]
    blocks += [{'pos': [0,1,0], 'name': 'lever', 'properties': {'face': 'floor'}}, {'pos': [1,1,0], 'name': 'redstone_wire'}, {'pos': [2,1,0], 'name': 'repeater', 'properties': {'facing': 'west', 'delay': '2'}}, {'pos': [3,1,0], 'name': 'redstone_wire'}, {'pos': [4,1,0], 'name': 'redstone_lamp'}]
    assert command('edit', blocks=blocks)['type'] == 'reply'
    command('probe', pos=[3,1,0], name='output')
    command('interact', pos=[0,1,0]); command('step', count=3)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'false'
    command('step', count=1)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    checkpoint = command('save', checkpoint=True)
    command('undo')
    assert command('inspect', pos=[0,1,0])['properties']['powered'] == 'false'
    command('redo')
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    assert command('place', pos=[0,2,0], name='tnt')['type'] == 'error'
    assert command('inspect', pos=[0,2,0])['name'] == 'minecraft:air'
    command('interact', pos=[0,1,0]); command('step', count=12)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'false'
    assert '#200' in command('vcd')['text']
    command('load', project=checkpoint)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    command('demo', kind='pistons')
    command('interact', pos=[0,1,4]); command('step', count=1)
    assert command('inspect', pos=[5,1,4])['name'] == 'minecraft:moving_piston'
    moving = command('save', checkpoint=True)
    assert moving['motions'], 'motion data missing from checkpoint'
    command('step', count=3)
    assert command('inspect', pos=[5,1,4])['name'] == 'minecraft:slime_block'
    command('load', project=moving); command('step', count=3)
    command('interact', pos=[0,1,4]); command('step', count=3)
    assert command('inspect', pos=[4,1,4])['name'] == 'minecraft:slime_block'
    assert command('inspect', pos=[5,1,4])['name'] == 'minecraft:air'
    command('demo', kind='environment')
    command('stimulate', pos=[0,1,0], stimulus={'entities': 1, 'livingEntities': 1})
    assert command('inspect', pos=[3,2,0])['properties']['open'] == 'true'
    command('stimulate', pos=[0,1,0], stimulus={'entities': 0, 'livingEntities': 0})
    command('step', count=19)
    assert command('inspect', pos=[3,2,0])['properties']['open'] == 'true'
    command('step', count=1)
    assert command('inspect', pos=[3,2,0])['properties']['open'] == 'false'
    assert command('inspect', pos=[7,1,4])['value'] == 15
    command('stimulate', pos=[0,1,4], stimulus={'page': 14})
    command('step', count=2)
    assert command('inspect', pos=[2,1,4])['value'] == 15
    assert command('stimulate', pos=[0,1,4], stimulus={'page': 200})['type'] == 'error'
    assert command('inspect', pos=[0,1,4])['runtime']['page'] == 14
    command('stimulate', pos=[7,1,0], stimulus={})
    assert command('inspect', pos=[7,1,0])['value'] == 15
    command('step', count=8)
    assert command('inspect', pos=[7,1,0])['value'] == 0
    command('new')
    command('place', pos=[0,0,0], name='chest')
    command('place', pos=[1,0,0], name='chest')
    assert command('inspect', pos=[0,0,0])['inventorySize'] == 54
    command('stimulate', pos=[0,0,0], stimulus={'inventory': [{'slot': 0, 'item': 'stone', 'count': 64}, {'slot': 27, 'item': 'wooden_sword', 'count': 1}]})
    inventory = command('save', checkpoint=True)
    command('interact', pos=[0,0,0])
    assert command('inspect', pos=[1,0,0])['runtime']['viewers'] == 1
    command('load', project=inventory)
    assert command('inspect', pos=[0,0,0])['inventory'][1]['item'] == 'minecraft:wooden_sword'
    assert command('stimulate', pos=[0,0,0], stimulus={'inventory': [{'slot': 0, 'item': 'wooden_sword', 'count': 64}]})['type'] == 'error'
    assert command('inspect', pos=[0,0,0])['inventory'][0]['count'] == 64
    command('new')
    command('place', pos=[0,0,0], name='hopper', properties={'facing': 'east'})
    command('place', pos=[1,0,0], name='barrel')
    command('stimulate', pos=[0,0,0], stimulus={'inventory': [{'slot': 0, 'item': 'stone', 'count': 2}]})
    command('step', count=1)
    assert command('inspect', pos=[1,0,0])['inventory'][0]['count'] == 1
    hopper = command('save', checkpoint=True)
    command('step', count=8)
    assert command('inspect', pos=[1,0,0])['inventory'][0]['count'] == 2
    command('demo', kind='rails')
    command('stimulate', pos=[0,1,0], stimulus={'carts':[{'type':'chest_minecart','inventory':[]}]})
    assert command('inspect', pos=[0,1,0])['inventorySize'] == 27
    assert command('inspect', pos=[9,1,0])['properties']['powered'] == 'true'
    assert command('inspect', pos=[10,1,0])['properties']['powered'] == 'false'
    command('stimulate', pos=[0,1,0], stimulus={'cartInventory':[{'slot':0,'item':'stone','count':64}]})
    command('step', count=22)
    assert command('inspect', pos=[0,1,1])['value'] == 1
    command('stimulate', pos=[0,1,0], stimulus={'carts':[]})
    assert command('inspect', pos=[0,1,0])['properties']['powered'] == 'true'
    command('step', count=18)
    assert command('inspect', pos=[0,1,0])['properties']['powered'] == 'false'
    command('demo', kind='tripwire')
    command('step', count=20)
    assert command('inspect', pos=[6,1,1])['properties']['attached'] == 'true'
    command('stimulate', pos=[3,1,1], stimulus={'entities':1})
    assert command('inspect', pos=[8,1,1])['properties']['lit'] == 'true'
    command('stimulate', pos=[3,1,1], stimulus={'entities':0})
    command('step', count=10)
    command('stimulate', pos=[3,1,1], stimulus={'entities':1})
    assert command('inspect', pos=[6,1,1])['properties']['powered'] == 'false'
    contact = command('save', checkpoint=True)
    command('step', count=1)
    assert command('inspect', pos=[6,1,1])['properties']['powered'] == 'true'
    command('load', project=contact); command('step', count=1)
    assert command('inspect', pos=[6,1,1])['properties']['powered'] == 'true'
    command('stimulate', pos=[3,1,5], stimulus={'shear':True})
    assert command('inspect', pos=[3,1,5])['name'] == 'minecraft:air'
    assert command('inspect', pos=[6,1,5])['properties']['powered'] == 'false'
    command('load', project=hopper)
    command('step', count=8)
    assert command('inspect', pos=[1,0,0])['inventory'][0]['count'] == 2
    command('new'); command('place',pos=[0,0,0],name='stone')
    command('place',pos=[0,1,0],name='oak_button',properties={'face':'floor'})
    command('probe',pos=[0,1,0],name='arrow')
    command('stimulate',pos=[0,1,0],stimulus={'arrows':1,'pressedArrows':0})
    assert command('inspect',pos=[0,1,0])['properties']['powered']=='true'
    command('stepEvent')
    assert command('inspect',pos=[0,1,0])['properties']['powered']=='false'
    arrow=command('save',checkpoint=True); command('stepEvent')
    assert command('inspect',pos=[0,1,0])['properties']['powered']=='true'
    command('load',project=arrow); command('stepEvent')
    command('stimulate',pos=[0,1,0],stimulus={'arrows':0}); command('step',count=30)
    assert command('inspect',pos=[0,1,0])['properties']['powered']=='false'
    assert command('stimulate',pos=[0,1,0],stimulus={'arrows':0,'pressedArrows':1})['type']=='error'
    command('new');command('place',pos=[0,0,0],name='oxidized_copper_chest')
    command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':0,'item':'stone','count':8}]})
    command('place',pos=[1,0,0],name='waxed_exposed_copper_chest')
    copper=command('inspect',pos=[0,0,0])
    assert copper['name']=='minecraft:exposed_copper_chest' and copper['inventorySize']==54
    assert copper['inventory'][0]['count']==8
    command('stimulate',pos=[0,0,0],stimulus={'viewers':1})
    opened=command('save',checkpoint=True)
    command('place',pos=[0,0,0],name='waxed_weathered_copper_chest',properties={'facing':'north','type':'left'})
    assert command('inspect',pos=[1,0,0])['name']=='minecraft:waxed_weathered_copper_chest'
    command('undo');assert command('inspect',pos=[0,0,0])['name']=='minecraft:exposed_copper_chest'
    command('load',project=opened)
    restored=command('inspect',pos=[0,0,0]);assert restored['inventory']==copper['inventory'] and restored['runtime']['viewers']==1
    command('new');command('place',pos=[0,0,0],name='chiseled_bookshelf')
    command('probe',pos=[0,0,0],name='lastSlot')
    command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':5,'item':'book','count':1},{'slot':0,'item':'knowledge_book','count':1}]})
    shelf=command('inspect',pos=[0,0,0]);assert shelf['analog']==1 and shelf['inventorySize']==6
    assert command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':2,'item':'stone','count':1}]})['type']=='error'
    command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':0,'count':0},{'slot':5,'count':0}]})
    empty=command('inspect',pos=[0,0,0]);assert empty['analog']==6 and empty['inventory']==[]
    shelfSnapshot=command('save',checkpoint=True);command('new');command('load',project=shelfSnapshot)
    assert command('inspect',pos=[0,0,0])['analog']==6
    command('new');command('place',pos=[0,0,0],name='decorated_pot')
    command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':0,'item':'snowball','count':16}]})
    pot=command('inspect',pos=[0,0,0]);assert pot['analog']==15 and pot['inventorySize']==1
    assert command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':0,'item':'snowball','count':17}]})['type']=='error'
    assert command('stimulate',pos=[0,0,0],stimulus={'viewers':1})['type']=='error'
    potSnapshot=command('save',checkpoint=True)
    command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':0,'count':0}]})
    assert command('inspect',pos=[0,0,0])['analog']==0
    command('undo');assert command('inspect',pos=[0,0,0])['inventory']==pot['inventory']
    command('new');command('load',project=potSnapshot)
    assert command('inspect',pos=[0,0,0])['analog']==15
    assert client.frames > 0
    print(f'PASS: HTTP host validation, binary frames ({client.frames}), circuit editing, delay, probes, VCD, atomic errors, native undo/redo, checkpoint import')
finally:
    command('load', project=original)
    client.close()
