import type { CatalogItem } from './api';
import { shortName } from './blockLabels';
import type { PlacementFace } from './surfacePlacement';

export const initialHotbar = ['redstone_wire','repeater','comparator','redstone_torch','lever','redstone_lamp','stone','redstone_block','sticky_piston'].map(name => `minecraft:${name}`);
export const hotbarIndex = (index: number, delta: number) => (index + Math.sign(delta) + 9) % 9;
export function itemForm(name: string, properties: Record<string,string> = {}): string | null {
  if(name==='minecraft:moving_piston'||name==='minecraft:air')return null;
  if(name==='minecraft:piston_head')return properties.type==='sticky'?'minecraft:sticky_piston':'minecraft:piston';
  return name === 'minecraft:redstone_wall_torch' ? 'minecraft:redstone_torch' : name;
}

// 仅把内核的直接点击操作分派为 interact；容器由现有库存面板处理。
export function creativeUse(item: CatalogItem): 'interact' | 'inspect' | null {
  const name = shortName(item.name);
  if ([16,17,18,28].includes(item.device) || ['chiseled_bookshelf','decorated_pot','jukebox','lectern','composter'].includes(name)) return 'inspect';
  if (['lever','repeater','comparator','redstone_wire','note_block','bell','daylight_detector'].includes(name) || name.endsWith('_button') || name.endsWith('copper_golem_statue')) return 'interact';
  if ((name.endsWith('_door') || name.endsWith('_trapdoor')) && !name.startsWith('iron_')) return 'interact';
  if (name.endsWith('_fence_gate')) return 'interact';
  return null;
}

// 玩家视线 -> 常用红石器件的放置朝向。仅写入器件实际公开的属性。
export function creativePlacement(item: CatalogItem | undefined, direction: {x:number;y:number;z:number}, face: PlacementFace | null, hitHeight = .5): Record<string,string> {
  if (!item) return {};
  const name = shortName(item.name), properties: Record<string,string> = {};
  const horizontal = Math.abs(direction.x) > Math.abs(direction.z) ? (direction.x > 0 ? 'east' : 'west') : (direction.z > 0 ? 'south' : 'north');
  const opposite: Record<string,string> = {north:'south',south:'north',east:'west',west:'east',up:'down',down:'up'};
  let facing = horizontal;
  if (['piston','sticky_piston','observer','dispenser','dropper','barrel'].includes(name)) {
    const nearest = Math.abs(direction.y) > Math.max(Math.abs(direction.x),Math.abs(direction.z)) ? (direction.y > 0 ? 'up' : 'down') : horizontal;
    facing = name === 'observer' ? nearest : opposite[nearest];
  } else if (['repeater','comparator'].includes(name)) facing = opposite[horizontal];
  else if (name === 'hopper') facing = face && face !== 'down' && face !== 'up' ? opposite[face] : 'down';
  else if (name === 'lever' || name.endsWith('_button')) {
    properties.face = face === 'down' ? 'ceiling' : face === 'up' || !face ? 'floor' : 'wall';
    facing = properties.face === 'wall' ? face! : horizontal;
  } else if (name.endsWith('shulker_box') || name.endsWith('lightning_rod')) facing=face??'up';
  else if (name.endsWith('_trapdoor')) {
    facing=face&&face!=='up'&&face!=='down'?face:opposite[horizontal];
    properties.half=face==='down'||(face!=='up'&&hitHeight>.5)?'top':'bottom';
  } else if (name.endsWith('_door') || name.endsWith('_fence_gate') || ['calibrated_sculk_sensor','decorated_pot'].includes(name)) facing=horizontal;
  else if (item.properties.facing) facing = opposite[horizontal];
  properties.facing = facing;
  if (name.endsWith('_slab')) properties.type = face === 'down' || (face!=='up'&&hitHeight>.5) ? 'top' : 'bottom';
  if (item.properties.axis) properties.axis = face === 'east' || face === 'west' ? 'x' : face === 'north' || face === 'south' ? 'z' : 'y';
  return Object.fromEntries(Object.entries(properties).filter(([key,value]) => item.properties[key]?.includes(value)));
}
