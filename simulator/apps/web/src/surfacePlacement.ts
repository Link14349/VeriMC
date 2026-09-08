export type PlacementFace = 'north' | 'south' | 'east' | 'west' | 'up' | 'down';
export type BlockPlacement = { name: string; properties: Record<string, string> };

// 命中面的外法线也是墙上火把的 facing；预览和实际命令共用此转换。
export function surfacePlacement(name: string, properties: Record<string, string>, face: PlacementFace | null): BlockPlacement | null {
  if (name !== 'minecraft:redstone_torch' && name !== 'minecraft:redstone_wall_torch') return { name, properties };
  if (face === 'down') return null;
  const { facing: _facing, ...shared } = properties;
  if (face && face !== 'up') return { name: 'minecraft:redstone_wall_torch', properties: { ...shared, facing: face } };
  return { name: 'minecraft:redstone_torch', properties: shared };
}
