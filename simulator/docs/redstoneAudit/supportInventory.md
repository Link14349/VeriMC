# 固定版本逐方块支持清单

本文件由 `tools/buildSupportInventory.py` 生成，输入是 `exportSupportInventory` 导出的内核支持等级
与 `tests/fixtures/java26_2BlockCapabilities.json`（用反射记录 26.2 每个方块**实际重写**了哪些红石相关回调）。
不要手工编辑；调色板或 `supportLevel` 变化时重新生成并复核。

“红石相关”的判据是：方块状态是信号源、有模拟量输出，或方块类重写了 `neighborChanged`、`tick`、
`triggerEvent`、`affectNeighborsAfterRemoval`、`getSignal`、`getDirectSignal`、`ownSignal` 之一。
纯形状/碰撞重写（栅栏、墙等）不计入，因为它们不带信号行为。

- 注册表方块总数：**1196**
- 其中红石相关：**416**
- 全部方块按支持等级：externalStimulus 45、implemented 375、partial 23、unimplemented 753
- 红石相关方块按支持等级：externalStimulus 45、implemented 105、partial 23、unimplemented 243

支持等级的含义：`implemented` 有器件实现并有原版差分；`partial` 有实现但存在明确缺口；
`externalStimulus` 需要显式环境输入才能驱动，不自动产生世界事件；`unimplemented` 一律拒绝放置。

## 已开放的红石相关方块

| 方块类 | 支持等级 | 数量 | 示例 |
|---|---|---:|---|
| `BarrelBlock` | implemented | 1 | minecraft:barrel |
| `BeehiveBlock` | externalStimulus | 2 | minecraft:bee_nest, minecraft:beehive |
| `BellBlock` | partial | 1 | minecraft:bell |
| `ButtonBlock` | implemented | 14 | minecraft:stone_button, minecraft:oak_button, minecraft:spruce_button … |
| `CalibratedSculkSensorBlock` | partial | 1 | minecraft:calibrated_sculk_sensor |
| `CauldronBlock` | externalStimulus | 1 | minecraft:cauldron |
| `ChestBlock` | implemented | 1 | minecraft:chest |
| `ChiseledBookShelfBlock` | partial | 1 | minecraft:chiseled_bookshelf |
| `ComparatorBlock` | implemented | 1 | minecraft:comparator |
| `ComposterBlock` | partial | 1 | minecraft:composter |
| `CopperBulbBlock` | implemented | 4 | minecraft:waxed_copper_bulb, minecraft:waxed_exposed_copper_bulb, minecraft:waxed_weathered_copper_bulb … |
| `CopperChestBlock` | implemented | 4 | minecraft:waxed_copper_chest, minecraft:waxed_exposed_copper_chest, minecraft:waxed_weathered_copper_chest … |
| `CopperGolemStatueBlock` | externalStimulus | 4 | minecraft:waxed_copper_golem_statue, minecraft:waxed_exposed_copper_golem_statue, minecraft:waxed_weathered_copper_golem_statue … |
| `DaylightDetectorBlock` | externalStimulus | 1 | minecraft:daylight_detector |
| `DecoratedPotBlock` | partial | 1 | minecraft:decorated_pot |
| `DetectorRailBlock` | externalStimulus | 1 | minecraft:detector_rail |
| `DoorBlock` | implemented | 17 | minecraft:oak_door, minecraft:iron_door, minecraft:spruce_door … |
| `DropperBlock` | partial | 1 | minecraft:dropper |
| `EndPortalFrameBlock` | externalStimulus | 1 | minecraft:end_portal_frame |
| `FenceGateBlock` | implemented | 12 | minecraft:oak_fence_gate, minecraft:spruce_fence_gate, minecraft:birch_fence_gate … |
| `HopperBlock` | implemented | 1 | minecraft:hopper |
| `JukeboxBlock` | partial | 1 | minecraft:jukebox |
| `LavaCauldronBlock` | externalStimulus | 1 | minecraft:lava_cauldron |
| `LayeredCauldronBlock` | externalStimulus | 2 | minecraft:water_cauldron, minecraft:powder_snow_cauldron |
| `LecternBlock` | externalStimulus | 1 | minecraft:lectern |
| `LeverBlock` | implemented | 1 | minecraft:lever |
| `LightningRodBlock` | externalStimulus | 4 | minecraft:waxed_lightning_rod, minecraft:waxed_exposed_lightning_rod, minecraft:waxed_weathered_lightning_rod … |
| `MovingPistonBlock` | implemented | 1 | minecraft:moving_piston |
| `NoteBlock` | partial | 1 | minecraft:note_block |
| `ObserverBlock` | implemented | 1 | minecraft:observer |
| `PiglinWallSkullBlock` | partial | 1 | minecraft:piglin_wall_head |
| `PistonBaseBlock` | implemented | 2 | minecraft:sticky_piston, minecraft:piston |
| `PistonHeadBlock` | implemented | 1 | minecraft:piston_head |
| `PlayerHeadBlock` | partial | 1 | minecraft:player_head |
| `PlayerWallHeadBlock` | partial | 1 | minecraft:player_wall_head |
| `PoweredBlock` | implemented | 1 | minecraft:redstone_block |
| `PoweredRailBlock` | implemented | 2 | minecraft:powered_rail, minecraft:activator_rail |
| `PressurePlateBlock` | externalStimulus | 14 | minecraft:stone_pressure_plate, minecraft:oak_pressure_plate, minecraft:spruce_pressure_plate … |
| `RailBlock` | implemented | 1 | minecraft:rail |
| `RedStoneWireBlock` | implemented | 1 | minecraft:redstone_wire |
| `RedstoneLampBlock` | implemented | 1 | minecraft:redstone_lamp |
| `RedstoneTorchBlock` | implemented | 1 | minecraft:redstone_torch |
| `RedstoneWallTorchBlock` | implemented | 1 | minecraft:redstone_wall_torch |
| `RepeaterBlock` | implemented | 1 | minecraft:repeater |
| `RespawnAnchorBlock` | externalStimulus | 1 | minecraft:respawn_anchor |
| `SculkSensorBlock` | partial | 1 | minecraft:sculk_sensor |
| `SkullBlock` | partial | 5 | minecraft:skeleton_skull, minecraft:zombie_head, minecraft:creeper_head … |
| `TargetBlock` | externalStimulus | 1 | minecraft:target |
| `TrapDoorBlock` | implemented | 17 | minecraft:oak_trapdoor, minecraft:spruce_trapdoor, minecraft:birch_trapdoor … |
| `TrappedChestBlock` | implemented | 1 | minecraft:trapped_chest |
| `TripWireBlock` | externalStimulus | 1 | minecraft:tripwire |
| `TripWireHookBlock` | implemented | 1 | minecraft:tripwire_hook |
| `WallSkullBlock` | partial | 4 | minecraft:skeleton_wall_skull, minecraft:zombie_wall_head, minecraft:creeper_wall_head … |
| `WeatheringCopperBulbBlock` | implemented | 4 | minecraft:copper_bulb, minecraft:exposed_copper_bulb, minecraft:weathered_copper_bulb … |
| `WeatheringCopperChestBlock` | implemented | 4 | minecraft:copper_chest, minecraft:exposed_copper_chest, minecraft:weathered_copper_chest … |
| `WeatheringCopperDoorBlock` | implemented | 4 | minecraft:copper_door, minecraft:exposed_copper_door, minecraft:weathered_copper_door … |
| `WeatheringCopperGolemStatueBlock` | externalStimulus | 4 | minecraft:copper_golem_statue, minecraft:exposed_copper_golem_statue, minecraft:weathered_copper_golem_statue … |
| `WeatheringCopperTrapDoorBlock` | implemented | 4 | minecraft:copper_trapdoor, minecraft:exposed_copper_trapdoor, minecraft:weathered_copper_trapdoor … |
| `WeatheringLightningRodBlock` | externalStimulus | 4 | minecraft:lightning_rod, minecraft:exposed_lightning_rod, minecraft:weathered_lightning_rod … |
| `WeightedPressurePlateBlock` | externalStimulus | 2 | minecraft:light_weighted_pressure_plate, minecraft:heavy_weighted_pressure_plate |
| `WitherSkullBlock` | partial | 1 | minecraft:wither_skeleton_skull |
| `WitherWallSkullBlock` | partial | 1 | minecraft:wither_skeleton_wall_skull |

## 尚未实现、一律拒绝放置的红石相关方块

这些方块在固定版注册表里确实带红石回调或模拟量输出，但内核没有实现其行为，
`Simulator::place` 会直接拒绝。补支持时逐器件分单，不得只放开 `supportLevel`。

| 方块类 | 数量 | 方块 |
|---|---:|---|
| `AnvilBlock` | 3 | `anvil`, `chipped_anvil`, `damaged_anvil` |
| `BambooStalkBlock` | 1 | `bamboo` |
| `BannerBlock` | 16 | `black_banner`, `blue_banner`, `brown_banner`, `cyan_banner`, `gray_banner`, `green_banner`, `light_blue_banner`, `light_gray_banner`, `lime_banner`, `magenta_banner`, `orange_banner`, `pink_banner`, `purple_banner`, `red_banner`, `white_banner`, `yellow_banner` |
| `BeaconBlock` | 1 | `beacon` |
| `BigDripleafBlock` | 1 | `big_dripleaf` |
| `BigDripleafStemBlock` | 1 | `big_dripleaf_stem` |
| `BlastFurnaceBlock` | 1 | `blast_furnace` |
| `BrewingStandBlock` | 1 | `brewing_stand` |
| `BrushableBlock` | 2 | `suspicious_gravel`, `suspicious_sand` |
| `BubbleColumnBlock` | 1 | `bubble_column` |
| `CactusBlock` | 1 | `cactus` |
| `CakeBlock` | 1 | `cake` |
| `CampfireBlock` | 2 | `campfire`, `soul_campfire` |
| `CandleCakeBlock` | 17 | `black_candle_cake`, `blue_candle_cake`, `brown_candle_cake`, `candle_cake`, `cyan_candle_cake`, `gray_candle_cake`, `green_candle_cake`, `light_blue_candle_cake`, `light_gray_candle_cake`, `lime_candle_cake`, `magenta_candle_cake`, `orange_candle_cake`, `pink_candle_cake`, `purple_candle_cake`, `red_candle_cake`, `white_candle_cake`, `yellow_candle_cake` |
| `CaveVinesBlock` | 1 | `cave_vines` |
| `CaveVinesPlantBlock` | 1 | `cave_vines_plant` |
| `CeilingHangingSignBlock` | 12 | `acacia_hanging_sign`, `bamboo_hanging_sign`, `birch_hanging_sign`, `cherry_hanging_sign`, `crimson_hanging_sign`, `dark_oak_hanging_sign`, `jungle_hanging_sign`, `mangrove_hanging_sign`, `oak_hanging_sign`, `pale_oak_hanging_sign`, `spruce_hanging_sign`, `warped_hanging_sign` |
| `ChorusFlowerBlock` | 1 | `chorus_flower` |
| `ChorusPlantBlock` | 1 | `chorus_plant` |
| `ColoredFallingBlock` | 1 | `gravel` |
| `CommandBlock` | 3 | `chain_command_block`, `command_block`, `repeating_command_block` |
| `ConcretePowderBlock` | 16 | `black_concrete_powder`, `blue_concrete_powder`, `brown_concrete_powder`, `cyan_concrete_powder`, `gray_concrete_powder`, `green_concrete_powder`, `light_blue_concrete_powder`, `light_gray_concrete_powder`, `lime_concrete_powder`, `magenta_concrete_powder`, `orange_concrete_powder`, `pink_concrete_powder`, `purple_concrete_powder`, `red_concrete_powder`, `white_concrete_powder`, `yellow_concrete_powder` |
| `ConduitBlock` | 1 | `conduit` |
| `CoralBlock` | 5 | `brain_coral_block`, `bubble_coral_block`, `fire_coral_block`, `horn_coral_block`, `tube_coral_block` |
| `CoralFanBlock` | 5 | `brain_coral_fan`, `bubble_coral_fan`, `fire_coral_fan`, `horn_coral_fan`, `tube_coral_fan` |
| `CoralPlantBlock` | 5 | `brain_coral`, `bubble_coral`, `fire_coral`, `horn_coral`, `tube_coral` |
| `CoralWallFanBlock` | 5 | `brain_coral_wall_fan`, `bubble_coral_wall_fan`, `fire_coral_wall_fan`, `horn_coral_wall_fan`, `tube_coral_wall_fan` |
| `CrafterBlock` | 1 | `crafter` |
| `CreakingHeartBlock` | 1 | `creaking_heart` |
| `DirtPathBlock` | 1 | `dirt_path` |
| `DispenserBlock` | 1 | `dispenser` |
| `DragonEggBlock` | 1 | `dragon_egg` |
| `DriedGhastBlock` | 1 | `dried_ghast` |
| `EnchantingTableBlock` | 1 | `enchanting_table` |
| `EndGatewayBlock` | 1 | `end_gateway` |
| `EndPortalBlock` | 1 | `end_portal` |
| `EnderChestBlock` | 1 | `ender_chest` |
| `EyeblossomBlock` | 2 | `closed_eyeblossom`, `open_eyeblossom` |
| `FarmlandBlock` | 1 | `farmland` |
| `FireBlock` | 1 | `fire` |
| `FrogspawnBlock` | 1 | `frogspawn` |
| `FrostedIceBlock` | 1 | `frosted_ice` |
| `FurnaceBlock` | 1 | `furnace` |
| `HangingMossBlock` | 1 | `pale_hanging_moss` |
| `KelpBlock` | 1 | `kelp` |
| `KelpPlantBlock` | 1 | `kelp_plant` |
| `LiquidBlock` | 2 | `lava`, `water` |
| `MangroveLeavesBlock` | 1 | `mangrove_leaves` |
| `PointedDripstoneBlock` | 1 | `pointed_dripstone` |
| `PotentSulfurBlock` | 1 | `potent_sulfur` |
| `SandBlock` | 2 | `red_sand`, `sand` |
| `ScaffoldingBlock` | 1 | `scaffolding` |
| `SculkCatalystBlock` | 1 | `sculk_catalyst` |
| `SculkShriekerBlock` | 1 | `sculk_shrieker` |
| `ShelfBlock` | 12 | `acacia_shelf`, `bamboo_shelf`, `birch_shelf`, `cherry_shelf`, `crimson_shelf`, `dark_oak_shelf`, `jungle_shelf`, `mangrove_shelf`, `oak_shelf`, `pale_oak_shelf`, `spruce_shelf`, `warped_shelf` |
| `ShulkerBoxBlock` | 17 | `black_shulker_box`, `blue_shulker_box`, `brown_shulker_box`, `cyan_shulker_box`, `gray_shulker_box`, `green_shulker_box`, `light_blue_shulker_box`, `light_gray_shulker_box`, `lime_shulker_box`, `magenta_shulker_box`, `orange_shulker_box`, `pink_shulker_box`, `purple_shulker_box`, `red_shulker_box`, `shulker_box`, `white_shulker_box`, `yellow_shulker_box` |
| `SmokerBlock` | 1 | `smoker` |
| `SnifferEggBlock` | 1 | `sniffer_egg` |
| `SpawnerBlock` | 1 | `spawner` |
| `SpongeBlock` | 1 | `sponge` |
| `StandingSignBlock` | 12 | `acacia_sign`, `bamboo_sign`, `birch_sign`, `cherry_sign`, `crimson_sign`, `dark_oak_sign`, `jungle_sign`, `mangrove_sign`, `oak_sign`, `pale_oak_sign`, `spruce_sign`, `warped_sign` |
| `StructureBlock` | 1 | `structure_block` |
| `SugarCaneBlock` | 1 | `sugar_cane` |
| `SulfurSpikeBlock` | 1 | `sulfur_spike` |
| `TestBlock` | 1 | `test_block` |
| `TestInstanceBlock` | 1 | `test_instance_block` |
| `TintedParticleLeavesBlock` | 6 | `acacia_leaves`, `birch_leaves`, `dark_oak_leaves`, `jungle_leaves`, `oak_leaves`, `spruce_leaves` |
| `TntBlock` | 1 | `tnt` |
| `TrialSpawnerBlock` | 1 | `trial_spawner` |
| `TwistingVinesBlock` | 1 | `twisting_vines` |
| `TwistingVinesPlantBlock` | 1 | `twisting_vines_plant` |
| `UntintedParticleLeavesBlock` | 4 | `azalea_leaves`, `cherry_leaves`, `flowering_azalea_leaves`, `pale_oak_leaves` |
| `VaultBlock` | 1 | `vault` |
| `WallBannerBlock` | 16 | `black_wall_banner`, `blue_wall_banner`, `brown_wall_banner`, `cyan_wall_banner`, `gray_wall_banner`, `green_wall_banner`, `light_blue_wall_banner`, `light_gray_wall_banner`, `lime_wall_banner`, `magenta_wall_banner`, `orange_wall_banner`, `pink_wall_banner`, `purple_wall_banner`, `red_wall_banner`, `white_wall_banner`, `yellow_wall_banner` |
| `WallHangingSignBlock` | 12 | `acacia_wall_hanging_sign`, `bamboo_wall_hanging_sign`, `birch_wall_hanging_sign`, `cherry_wall_hanging_sign`, `crimson_wall_hanging_sign`, `dark_oak_wall_hanging_sign`, `jungle_wall_hanging_sign`, `mangrove_wall_hanging_sign`, `oak_wall_hanging_sign`, `pale_oak_wall_hanging_sign`, `spruce_wall_hanging_sign`, `warped_wall_hanging_sign` |
| `WallSignBlock` | 12 | `acacia_wall_sign`, `bamboo_wall_sign`, `birch_wall_sign`, `cherry_wall_sign`, `crimson_wall_sign`, `dark_oak_wall_sign`, `jungle_wall_sign`, `mangrove_wall_sign`, `oak_wall_sign`, `pale_oak_wall_sign`, `spruce_wall_sign`, `warped_wall_sign` |
| `WeepingVinesBlock` | 1 | `weeping_vines` |
| `WeepingVinesPlantBlock` | 1 | `weeping_vines_plant` |

## 占位耦合门禁

`Device::dispenser` / `crafter` / `furnace` 在枚举和容量表里有占位值。
`BlockRegistry` 现在会在这些器件的 `supportLevel` 不是 `unimplemented` 时直接抛出，
核心测试 “26.2 redstone capability coverage gate” 也断言这一点，
避免有人只放开支持等级就让它们退化成普通容器。

## 已确定不在实施范围

TNT 复制机、流体农场、矿车计算机、依赖完整生物 AI 的机器属于用户已确定的排除项，
不计入待实现功能；实验红石与基岩版规则同样不属于兼容目标。

