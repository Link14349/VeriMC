#!/usr/bin/env python3
"""HTTP file transfers, native legacy compatibility, undo and cancellation."""
import http.client as httpClient
import json
import sys
import time
from serverClient import ServerClient

port=int(sys.argv[1]) if len(sys.argv)>1 else 28765
client=ServerClient(port)
command=client.command

def http(method,path,body=None,headers=None):
    connection=httpClient.HTTPConnection('127.0.0.1',port,timeout=30)
    connection.request(method,path,body,headers or {})
    response=connection.getresponse();status=response.status;responseHeaders=dict(response.getheaders());content=response.read();connection.close()
    return status,responseHeaders,content

token=json.loads(http('GET','/api/bootstrap')[2])['token']
headers={'X-Simulator-Token':token,'Origin':f'http://127.0.0.1:{port}','Content-Type':'application/octet-stream'}
def wait(job):
    deadline=time.monotonic()+45
    while time.monotonic()<deadline:
        status,_,content=http('GET',f'/api/files/{job}',headers=headers)
        assert status==200,(status,content)
        result=json.loads(content)
        if result['state']!='working':return result
        time.sleep(.025)
    raise AssertionError('file job timed out')
def canonical(value):return {key:value for key,value in value.items() if key not in ('type','cmd','requestId')}
def snapshot():return canonical(command('save',checkpoint=True))
def export(format='vmcb',checkpoint=True):
    job=command('exportFile',format=format,checkpoint=checkpoint);assert job['type']=='reply',job
    result=wait(job['id']);assert result['state']=='done',result
    status,responseHeaders,content=http('GET',f'/api/files/{job["id"]}/download?token={token}')
    assert status==200 and responseHeaders['Content-Type']=='application/octet-stream',(status,content)
    assert 'attachment;' in responseHeaders['Content-Disposition']
    assert responseHeaders['Content-Disposition'].endswith('.json' if format=='json' else '.vmcb'),'download lost its extension'
    return content

def upload(content):
    job=command('importFile');assert job['type']=='reply',job
    status,_,body=http('POST',f'/api/files/{job["id"]}/upload',content,headers)
    assert status==202,(status,body)
    return wait(job['id'])
original=snapshot()
try:
    command('new');command('place',pos=[0,0,0],name='stone');command('place',pos=[0,1,0],name='stone_button',properties={'face':'floor'});command('probe',pos=[0,1,0],name='input');command('interact',pos=[0,1,0]);command('step',count=7)
    expected=snapshot();binary=export();assert binary.startswith(b'VMCB\r\n\x1a\n')
    legacy=export('json');assert json.loads(legacy)==expected
    command('new');empty=snapshot();assert upload(binary)['state']=='done';assert snapshot()==expected
    command('undo');assert snapshot()==empty;command('redo');assert snapshot()==expected
    command('step',count=13);assert command('inspect',pos=[0,1,0])['properties']['powered']=='false'
    assert upload(legacy)['state']=='done';assert snapshot()==expected
    large=json.loads(legacy);large['sequence']=2**53+37
    assert upload(json.dumps(large).encode())['state']=='done'
    assert json.loads(export('json'))['sequence']==2**53+37,'JSON passed through JS Number'
    command('rename',name='x'*200);export();export('json')
    before=snapshot();history=(client.latestStatus.get('canUndo'),client.latestStatus.get('canRedo'))
    bad=bytearray(binary);bad[60]^=1
    result=upload(bad);assert result['state']=='failed',result;assert snapshot()==before
    assert (client.latestStatus.get('canUndo'),client.latestStatus.get('canRedo'))==history,'failed import changed history'
    job=command('importFile');assert command('new')['type']=='error','edits not excluded during import'
    status,_,_=http('POST',f'/api/files/{job["id"]}/upload',binary,{'Origin':f'http://127.0.0.1:{port}'})
    assert status==403,'missing-token upload accepted'
    foreign=dict(headers,Origin='http://untrusted.example')
    assert http('POST',f'/api/files/{job["id"]}/upload',binary,foreign)[0]==403,'foreign-origin upload accepted'
    result=command('cancelFile',id=job['id']);assert result['state']=='cancelled';assert snapshot()==before
    job=command('importFile');connection=httpClient.HTTPConnection('127.0.0.1',port,timeout=5)
    connection.putrequest('POST',f'/api/files/{job["id"]}/upload')
    for key,value in headers.items():connection.putheader(key,value)
    connection.putheader('Content-Length',str(len(binary)*2));connection.endheaders();connection.send(binary[:32]);connection.close()
    result=wait(job['id']);assert result['state']=='failed';assert snapshot()==before,'interrupted upload changed world'
    job=command('importFile');connection=httpClient.HTTPConnection('127.0.0.1',port,timeout=5)
    connection.putrequest('POST',f'/api/files/{job["id"]}/upload')
    for key,value in headers.items():connection.putheader(key,value)
    connection.putheader('Content-Length',str(8*1024**3+1));connection.endheaders()
    response=connection.getresponse();assert response.status==413;response.read();connection.close()
    assert wait(job['id'])['state']=='failed' and snapshot()==before,'oversized upload changed world'
    command('new');design=export(checkpoint=False);assert upload(design)['state']=='done' and not snapshot()['blocks']
    print('PASS file transfers: VMCB + JSON, exact integers, continuation, undo/redo, invalid/cancelled/interrupted imports and session checks')
finally:
    command('load',project=original);client.close()
