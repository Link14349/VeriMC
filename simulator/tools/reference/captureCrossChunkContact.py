# Capture the vanilla cross-chunk collision case that does NOT pick up the item.
# Usage: python3 simulator/tools/reference/captureCrossChunkContact.py OUTPUT_DIR
# Requires JDK 25 on PATH and the local reference cache. Expected checker exit: 0.
import json,sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'simulator/tools/reference'))
from vanillaReplay import runOnce
out=Path(sys.argv[1]).resolve()
out.mkdir(exist_ok=True)
base=json.loads((root/'simulator/tests/fixtures/java26_2BlockTicking.json').read_text())
blocks=json.loads((root/'simulator/data/blockStates.json').read_text())['blocks']
def state(name,props={}):
    b=next(b for b in blocks if b['name']=='minecraft:'+name)
    return next(s['id'] for s in b['states'] if all(s.get('properties',{}).get(k)==v for k,v in props.items()))
p=[16,1,4]
fixture={'origin':base['origin'],'referenceEnvironment':base['referenceEnvironment'],'endTick':16,
         'requiresAlignedOrigin':True,'watchChunkState':True,'watchGroundItems':True,'watch':[p,[15,1,4]],
         'commands':[
             {'tick':0,'pos':[16,0,4],'stateId':state('stone')},
             {'tick':0,'pos':p,'stateId':state('hopper',{'facing':'down','enabled':'true'})},
             {'tick':0,'pos':[16,2,4],'stateId':state('stone')},
             {'tick':2,'pos':p,'chunkForced':False},
             {'tick':2,'pos':p,'chunkState':'blockTicking'},
             {'tick':5,'pos':p,'stimulus':{'groundItems':[{'item':'minecraft:stone','count':1,'x':-0.05,'y':0.75,'z':0.5}]}},
         ]}
if '--probe' in sys.argv[2:]:
    fixture['watchItemEntities']=True
path,captured=runOnce('java26_2CrossChunkContact',fixture,out,'',False)
run=subprocess.run([str(root/'simulator/buildAudit/checkReference'),str(path)],capture_output=True,text=True)
(out/'checker.json').write_text(run.stdout)
print(run.stdout)
print('frame6',captured['frames'][6])
sys.exit(run.returncode)
