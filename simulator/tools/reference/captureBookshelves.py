#!/usr/bin/env python3
"""Bookshelf last-slot output, five accepted book types, hoppers and droppers."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def inventory(tick,pos,slots):commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':slots}})
def slot(index,item='book',count=1):return {'slot':index,'item':item,'count':count}
def comparator(pos):
    x,y,z=pos;setBlock(0,(x+1,y-1,z),'stone');setBlock(0,(x+1,y,z),'comparator',facing='west');watch.append([x+1,y,z])

pos=(3,2,3);setBlock(0,pos,'chiseled_bookshelf');watch.append(list(pos));comparator(pos)
inventory(0,pos,[slot(5),slot(0,'knowledge_book')])
inventory(2,pos,[slot(i,name) for i,name in enumerate(['book','written_book','enchanted_book','writable_book','knowledge_book'],1)])
inventory(4,pos,[slot(2,count=0)])
inventory(6,pos,[slot(i,count=0) for i in range(5,-1,-1)])
inventory(7,pos,[slot(5,count=0)])

# A non-book in the first source slot must be skipped; six books fill six slots.
setBlock(0,(3,2,10),'hopper',facing='east');setBlock(0,(4,2,10),'chiseled_bookshelf',facing='east')
inventory(0,(3,2,10),[slot(0,'stone',4),slot(1,'book',7)])
watch.extend([[3,2,10],[4,2,10]]);comparator((4,2,10))

# Failed extraction is prechecked by the bookshelf; it must not touch lastSlot.
setBlock(0,(12,3,10),'chiseled_bookshelf');setBlock(0,(12,2,10),'hopper',facing='east')
inventory(0,(12,3,10),[slot(0),slot(2,'knowledge_book'),slot(5,'enchanted_book')])
inventory(0,(12,2,10),[slot(i,'stone',63) for i in range(5)])
inventory(7,(12,2,10),[slot(1,count=0)]);inventory(16,(12,2,10),[slot(1,count=0)])
watch.extend([[12,3,10],[12,2,10]]);comparator((12,3,10))

setBlock(0,(22,2,10),'dropper',facing='east');setBlock(0,(23,2,10),'chiseled_bookshelf')
inventory(0,(22,2,10),[slot(0,'stone',2)])
setBlock(1,(21,2,10),'redstone_block');setBlock(2,(21,2,10),'air')
inventory(6,(22,2,10),[slot(0,'knowledge_book',1)])
setBlock(7,(21,2,10),'redstone_block');setBlock(8,(21,2,10),'air')
watch.extend([[22,2,10],[23,2,10]]);comparator((23,2,10))
runCapture(commands,watch,56,'java26_2Bookshelves')
