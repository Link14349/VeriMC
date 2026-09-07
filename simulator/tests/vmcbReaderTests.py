#!/usr/bin/env python3
"""Independent VMCB reader: stdlib plus the installed native zstd library.
No production C++ encoding helpers are used for the byte-level checks.
"""
import ctypes
import ctypes.util
from pathlib import Path
import struct
import sys
import zlib

zstd = ctypes.CDLL(ctypes.util.find_library('zstd'))
zstd.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
zstd.ZSTD_decompress.restype = ctypes.c_size_t

def cbor(data):
    cursor = 0
    def read(count):
        nonlocal cursor
        assert cursor + count <= len(data)
        value = data[cursor:cursor+count]; cursor += count
        return value
    def value():
        head = read(1)[0]; major, info = head >> 5, head & 31
        if major == 7:
            if info in (20,21,22): return {20:False,21:True,22:None}[info]
            return struct.unpack({25:'>e',26:'>f',27:'>d'}[info],read({25:2,26:4,27:8}[info]))[0]
        count = info if info < 24 else int.from_bytes(read({24:1,25:2,26:4,27:8}[info]),'big')
        if major == 0: return count
        if major == 1: return -1-count
        if major in (2,3):
            raw = read(count)
            return raw if major == 2 else raw.decode('utf-8')
        if major == 4: return [value() for _ in range(count)]
        if major == 5:
            result = {}
            for _ in range(count):
                key = value(); assert key not in result; result[key] = value()
            return result
        raise AssertionError(major)
    result = value(); assert cursor == len(data)
    return result

def inspect(path):
    data = Path(path).read_bytes()
    magic,major,minor,headerSize,entrySize,flags,count,offset,indexSize,fileSize,blocks,indexCrc,headerCrc = struct.unpack_from('<8sHHHHIIQQQQII',data)
    assert magic == b'VMCB\r\n\x1a\n' and (major,minor,headerSize,entrySize)==(1,0,64,64)
    assert fileSize == len(data) == offset+indexSize and indexSize==count*64
    assert zlib.crc32(data[:60])==headerCrc and zlib.crc32(data[offset:])==indexCrc
    pages=[]; lastKey=None
    for i in range(count):
        tag,major,minor,critical,codec,x,y,z,part,start,stored,raw,checksum,reserved=struct.unpack_from('<4sHHIB3xiiiIQQQII',data,offset+i*64)
        assert (major,minor,critical,reserved)==(1,0,1,0)
        key=(tag,x,y,z,part); assert lastKey is None or lastKey<key;lastKey=key
        content=data[start:start+stored]
        if codec:
            result=ctypes.create_string_buffer(raw)
            assert zstd.ZSTD_decompress(result,raw,content,len(content))==raw
            content=result.raw
        assert len(content)==raw and zlib.crc32(content)==checksum
        pages.append((tag,(x,y,z),content,start,stored))
    physical=sorted(pages,key=lambda row:row[3]);end=64
    for *_,start,stored in physical: assert start==end;end+=stored
    assert end==offset
    metadata=cbor(next(row[2] for row in pages if row[0]==b'META'))
    assert metadata['minecraftVersion']=='26.2' and len(metadata['rulesDigest'])==32
    paletteData=next(row[2] for row in pages if row[0]==b'PALT')
    def varInt(data,cursor):
        result=0;shift=0
        while True:
            byte=data[cursor];cursor+=1;result|=(byte&127)<<shift
            if byte<128:return result,cursor
            shift+=7
    def string(data,cursor):
        size,cursor=varInt(data,cursor);return data[cursor:cursor+size].decode(),cursor+size
    total,cursor=varInt(paletteData,0);palette=[]
    for _ in range(total):
        name,cursor=string(paletteData,cursor);size,cursor=varInt(paletteData,cursor);properties={}
        for _ in range(size):
            key,cursor=string(paletteData,cursor);value,cursor=string(paletteData,cursor);properties[key]=value
        palette.append((name,properties))
    assert cursor==len(paletteData) and palette[0]==('minecraft:air',{})
    world={}
    for tag,(cx,cy,cz),content,_,_ in pages:
        if tag!=b'BLKS':continue
        mode,bits,nonAir,size,reserved=struct.unpack_from('<BBHHH',content);assert reserved==0
        cursor=8;localPalette=[]
        for _ in range(size):
            state,cursor=varInt(content,cursor);localPalette.append(state)
        values=[0]*4096
        if mode==0:values=[localPalette[0]]*4096
        else:
            positions=list(range(4096));lengths=[]
            if mode==2:
                positions=[];previous=-1
                for _ in range(nonAir):gap,cursor=varInt(content,cursor);previous+=1+gap;positions.append(previous)
            if mode==3:
                runs,cursor=varInt(content,cursor);positions=list(range(runs))
                for _ in range(runs):length,cursor=varInt(content,cursor);lengths.append(length+1)
            packed=content[cursor:];cursor+=len(packed);stream=int.from_bytes(packed,'little');runPos=0
            for i,position in enumerate(positions):
                state=localPalette[(stream>>(i*bits))&((1<<bits)-1)]
                if mode==3:values[runPos:runPos+lengths[i]]=[state]*lengths[i];runPos+=lengths[i]
                else:values[position]=state
        assert cursor==len(content) and sum(state!=0 for state in values)==nonAir
        for i,state in enumerate(values):
            if state:
                pos=(cx*16+(i&15),cy*16+(i>>8),cz*16+((i>>4)&15));assert pos not in world;world[pos]=palette[state]
    assert len(world)==blocks
    if metadata['name']=='独立读取器验证':
        assert flags==1 and metadata['snapshot']['tick']==7 and blocks==181
        assert world[-17,1,0][0]=='minecraft:stone_button' and world[-17,1,0][1]['powered']=='true'
        runtime={cbor(row[2])['table']:cbor(row[2])['rows'] for row in pages if row[0]==b'RUNT'}
        assert any(row['blockName']=='minecraft:stone_button' and row['tick']==20 for row in runtime['events'])
    print(f'PASS independent reader: {path}, {blocks} blocks, {count} pages, {len(data)} bytes')
    return world

if __name__=='__main__':
    for path in sys.argv[1:] or [Path(__file__).parent/'fixtures/vmcbCheckpoint1.vmcb']:inspect(path)
