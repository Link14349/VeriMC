import com.google.gson.*;
import java.nio.file.*;
import java.util.function.*;
import java.util.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.ComparatorBlockEntity;
import net.minecraft.core.Direction;
import net.minecraft.core.component.DataComponents;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.entity.LecternBlockEntity;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntitySpawnReason;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.component.WritableBookContent;
import net.minecraft.server.network.Filterable;
import net.minecraft.network.chat.Component;
import net.minecraft.world.Container;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.GameType;
import net.minecraft.core.registries.BuiltInRegistries;

/** Executes our input timeline inside the unmodified Java 26.2 GameTest world. */
public class CaptureRedstone extends TestFunctionLoader {
    static JsonObject scenario;
    static Path output;
    // Intra-tick neighbour/shape update trace. Vanilla exposes exactly this hook through
    // CollectingNeighborUpdater.setDebugListener, so no game code is modified.
    static int traceLimit = 0;
    static boolean traceTruncated = false;
    static JsonArray traceEntries = new JsonArray();
    static BlockPos traceOrigin = BlockPos.ZERO;
    static void appendTrace(JsonElement entry) {
        if (traceEntries.size() >= traceLimit) { traceTruncated = true; return; }
        traceEntries.add(entry);
    }
    static void addRelative(JsonArray target, BlockPos pos) {
        target.add(pos.getX() - traceOrigin.getX());
        target.add(pos.getY() - traceOrigin.getY());
        target.add(pos.getZ() - traceOrigin.getZ());
    }
    static Object field(Object owner, String name) {
        try {
            var declared = owner.getClass().getDeclaredField(name);
            declared.setAccessible(true);
            return declared.get(owner);
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }
    static String blockName(Object block) {
        return BuiltInRegistries.BLOCK.getKey((net.minecraft.world.level.block.Block)block).toString();
    }
    /**
     * One entry per stack peek, describing what kind of update vanilla is about to run and where it
     * came from. `forEachUpdatedPos` only reports coordinates, so the enclosing update object is read
     * off `CollectingNeighborUpdater.stack` instead. This is an observation, not a change: nothing is
     * written back, and the tracing on/off control run proves the timeline is unaffected.
     */
    static JsonArray describeUpdate(Object update) {
        JsonArray entry = new JsonArray();
        String kind = update.getClass().getSimpleName();
        switch (kind) {
        case "MultiNeighborUpdate" -> {
            var skip = (Direction)field(update, "skipDirection");
            entry.add("m"); addRelative(entry, (BlockPos)field(update, "sourcePos"));
            if (skip == null) entry.add(JsonNull.INSTANCE); else entry.add(skip.getName());
            entry.add((Integer)field(update, "idx"));
            entry.add(blockName(field(update, "sourceBlock")));
        }
        case "ShapeUpdate" -> {
            entry.add("s"); addRelative(entry, (BlockPos)field(update, "pos"));
            addRelative(entry, (BlockPos)field(update, "neighborPos"));
            entry.add(((Direction)field(update, "direction")).getName());
            entry.add((Integer)field(update, "updateFlags"));
        }
        case "SimpleNeighborUpdate" -> {
            entry.add("n"); addRelative(entry, (BlockPos)field(update, "pos"));
            entry.add(blockName(field(update, "block")));
        }
        // The full form carries a block state snapshot taken when the update was queued, so a later
        // replacement of the target does not change what handleNeighborChanged sees.
        case "FullNeighborUpdate" -> {
            entry.add("f"); addRelative(entry, (BlockPos)field(update, "pos"));
            entry.add(blockName(field(update, "block")));
            entry.add(Block.getId((BlockState)field(update, "state")));
            entry.add((Boolean)field(update, "movedByPiston"));
        }
        default -> throw new IllegalStateException("Unknown neighbour update kind " + kind);
        }
        return entry;
    }
    static int remainingInBurst = 0;
    static void recordTraceEntry(BlockPos pos) {
        // forEachUpdatedPos fires once per peek for every kind except the multi update, which
        // reports each of its non-skipped neighbours. Only the first call of a burst is an event.
        if (remainingInBurst > 0) { --remainingInBurst; return; }
        Object update = traceStack.peek();
        appendTrace(describeUpdate(update));
        remainingInBurst = update.getClass().getSimpleName().equals("MultiNeighborUpdate")
            ? (field(update, "skipDirection") == null ? 6 : 5) - 1 : 0;
    }
    @SuppressWarnings("unchecked")
    static java.util.ArrayDeque<Object> stackOf(net.minecraft.world.level.redstone.CollectingNeighborUpdater updater) {
        return (java.util.ArrayDeque<Object>)field(updater, "stack");
    }
    static java.util.ArrayDeque<Object> traceStack;
    static net.minecraft.world.level.redstone.CollectingNeighborUpdater neighborUpdaterOf(Level level) {
        try {
            var field = Level.class.getDeclaredField("neighborUpdater");
            field.setAccessible(true);
            return (net.minecraft.world.level.redstone.CollectingNeighborUpdater)field.get(level);
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }
    static Map<BlockPos, List<Entity>> occupants = new HashMap<>();
    static Map<BlockPos, List<Player>> viewers = new HashMap<>();
    static BlockPos pos(JsonArray p) { return new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt()); }
    static JsonArray coordinates(BlockPos p) { JsonArray a = new JsonArray(); a.add(p.getX()); a.add(p.getY()); a.add(p.getZ()); return a; }
    /** Same anonymous player GameTestHelper.makeMockPlayer builds, so both harnesses interact identically. */
    static Player mockPlayer(Level level, GameType gameType) {
        return new Player(level, new com.mojang.authlib.GameProfile(UUID.randomUUID(), "test-mock-player")) {
            @Override public GameType gameMode() { return gameType; }
            @Override public boolean isClientAuthoritative() { return false; }
        };
    }
    static void applyCommand(net.minecraft.server.level.ServerLevel level, BlockPos pos, JsonObject command) {
        if (command.has("stateId")) {
            var placed = Block.stateById(command.get("stateId").getAsInt());
            if (command.has("playerPlace")) {
                var player=mockPlayer(level, GameType.CREATIVE);
                // Stairs read the player's horizontal direction directly and take HALF from the clicked face;
                // chests use the opposite direction and always keep the original click below the target.
                boolean stairs=placed.getBlock() instanceof StairBlock;
                player.setYRot(stairs?placed.getValue(StairBlock.FACING).toYRot():placed.getValue(ChestBlock.FACING).getOpposite().toYRot());
                var hit=stairs
                    ?new net.minecraft.world.phys.BlockHitResult(net.minecraft.world.phys.Vec3.atCenterOf(pos),placed.getValue(StairBlock.HALF)==net.minecraft.world.level.block.state.properties.Half.TOP?Direction.DOWN:Direction.UP,pos,false)
                    :new net.minecraft.world.phys.BlockHitResult(net.minecraft.world.phys.Vec3.atCenterOf(pos.below()),Direction.UP,pos.below(),false);
                var context=new net.minecraft.world.item.context.BlockPlaceContext(player,net.minecraft.world.InteractionHand.MAIN_HAND,new ItemStack(placed.getBlock().asItem()),hit);
                placed=placed.getBlock().getStateForPlacement(context);
                if(placed==null) throw new IllegalStateException("Reference placement failed");
            }
            level.setBlock(pos, placed, 3);
            if (command.has("placedBy")) placed.getBlock().setPlacedBy(level, pos, placed, null, ItemStack.EMPTY);
            return;
        }
        var state = level.getBlockState(pos);
        try {
            if (command.has("interact")) {
                if (state.getBlock() instanceof DoorBlock door) door.setOpen(null, level, state, pos, !state.getValue(DoorBlock.OPEN));
                else if (state.getBlock() instanceof LeverBlock lever) lever.pull(state, level, pos, null);
                else if(state.getBlock() instanceof BellBlock bell) {
                    var direction=state.getValue(BellBlock.FACING);var attachment=state.getValue(BellBlock.ATTACHMENT);
                    if(attachment==net.minecraft.world.level.block.state.properties.BellAttachType.SINGLE_WALL || attachment==net.minecraft.world.level.block.state.properties.BellAttachType.DOUBLE_WALL)direction=direction.getAxis()==Direction.Axis.Z?Direction.EAST:Direction.NORTH;
                    bell.attemptToRing(level,pos,direction);
                }
                else if (state.getBlock() instanceof ButtonBlock button) { if (!state.getValue(ButtonBlock.POWERED)) button.press(state, level, pos, null); }
                else if (state.getBlock() instanceof NoteBlock || state.getBlock() instanceof DaylightDetectorBlock
                    || state.getBlock() instanceof RedStoneWireBlock || state.getBlock() instanceof DiodeBlock
                    || state.getBlock() instanceof TrapDoorBlock || state.getBlock() instanceof FenceGateBlock) {
                    var player=mockPlayer(level, GameType.CREATIVE);
                    // Fence gates flip their facing toward the player when opened from behind, so the
                    // look direction has to be explicit. Without one, look along the gate's own facing,
                    // which is exactly the case where vanilla does not flip.
                    if (state.getBlock() instanceof FenceGateBlock) {
                        var look = command.has("playerFacing") ? Direction.byName(command.get("playerFacing").getAsString())
                            : state.getValue(FenceGateBlock.FACING);
                        player.setYRot(look.toYRot());
                    }
                    var hit=new net.minecraft.world.phys.BlockHitResult(net.minecraft.world.phys.Vec3.atCenterOf(pos),Direction.UP,pos,false);
                    var method=state.getBlock().getClass().getDeclaredMethod("useWithoutItem",BlockState.class,Level.class,BlockPos.class,Player.class,net.minecraft.world.phys.BlockHitResult.class);
                    method.setAccessible(true);method.invoke(state.getBlock(),state,level,pos,player,hit);
                }
                // Random generators cannot guarantee the target still exists; lenient mode makes
                // that a no-op on both sides instead of aborting the capture. Any real behaviour
                // difference still shows up as a state difference.
                else if (!scenario.has("lenientInteract") || !scenario.get("lenientInteract").getAsBoolean())
                    throw new IllegalArgumentException("Unsupported reference interaction");
                return;
            }
            var input = command.getAsJsonObject("stimulus");
            if (input.has("itemFrames")) {
                // Rebuild the frame set hanging on this block. Notifications are issued once per
                // affected cell afterwards, which is the same call ItemFrame.setItem/setRotation make.
                var touched = new LinkedHashSet<Direction>();
                for (var direction : Direction.values()) {
                    var cell = pos.relative(direction);
                    var box = new net.minecraft.world.phys.AABB(cell.getX(), cell.getY(), cell.getZ(), cell.getX() + 1, cell.getY() + 1, cell.getZ() + 1);
                    for (var existing : level.getEntitiesOfClass(net.minecraft.world.entity.decoration.ItemFrame.class, box, f -> f.getDirection() == direction)) {
                        existing.discard(); touched.add(direction);
                    }
                }
                var rotationSetter = net.minecraft.world.entity.decoration.ItemFrame.class.getDeclaredMethod("setRotation", int.class, boolean.class);
                rotationSetter.setAccessible(true);
                for (var value : input.getAsJsonArray("itemFrames")) {
                    var frame = value.getAsJsonObject();
                    var direction = Direction.byName(frame.get("facing").getAsString());
                    var entity = new net.minecraft.world.entity.decoration.ItemFrame(level, pos.relative(direction), direction);
                    entity.setItem(frame.has("hasItem") && frame.get("hasItem").getAsBoolean() ? new ItemStack(Items.STONE) : ItemStack.EMPTY, false);
                    rotationSetter.invoke(entity, frame.get("rotation").getAsInt(), false);
                    level.addFreshEntity(entity);
                    touched.add(direction);
                }
                for (var direction : touched) level.updateNeighbourForOutputSignal(pos.relative(direction), Blocks.AIR);
                return;
            }
            // 器件层实体物品输入：在漏斗吸取范围里放置真实的掉落物实体。
            // 速度清零、关闭重力，这与压力板/绊线的接触输入是同一个约定：不模拟运动轨迹。
            if (input.has("groundItems")) {
                for (var existing : occupants.getOrDefault(pos, List.of())) existing.discard();
                var spawned = new ArrayList<Entity>(); occupants.put(pos, spawned);
                for (var value : input.getAsJsonArray("groundItems")) {
                    var row = value.getAsJsonObject();
                    var item = BuiltInRegistries.ITEM.getValue(Identifier.parse(row.get("item").getAsString()));
                    var stack = new ItemStack(item, row.get("count").getAsInt());
                    double x = row.has("x") ? row.get("x").getAsDouble() : 0.5;
                    double y = row.has("y") ? row.get("y").getAsDouble() : 1.0;
                    double z = row.has("z") ? row.get("z").getAsDouble() : 0.5;
                    var drop = new ItemEntity(level, pos.getX() + x, pos.getY() + y, pos.getZ() + z, stack);
                    drop.setDeltaMovement(net.minecraft.world.phys.Vec3.ZERO);
                    drop.setNoGravity(true);
                    level.addFreshEntity(drop); spawned.add(drop);
                }
                return;
            }
            if(input.has("compostItem")) {
                var item=BuiltInRegistries.ITEM.get(Identifier.parse(input.get("compostItem").getAsString())).orElseThrow().value();
                ComposterBlock.insertItem(null,state,level,new ItemStack(item),pos);return;
            }
            if (input.has("gameEvent")) {
                var event=BuiltInRegistries.GAME_EVENT.get(Identifier.parse(input.get("gameEvent").getAsString())).orElseThrow();
                var offset=input.has("offset")?input.getAsJsonArray("offset"):JsonParser.parseString("[0.5,0.5,0.5]").getAsJsonArray();
                var location=new net.minecraft.world.phys.Vec3(pos.getX()+offset.get(0).getAsDouble(),pos.getY()+offset.get(1).getAsDouble(),pos.getZ()+offset.get(2).getAsDouble());
                Entity source=null;
                if(input.has("source")) {
                    var flags=input.getAsJsonObject("source");
                    source=new ArmorStand(level,pos.getX(),pos.getY(),pos.getZ()) {
                        public boolean isSpectator(){return flags.has("spectator") && flags.get("spectator").getAsBoolean();}
                        public boolean isSteppingCarefully(){return flags.has("sneaking") && flags.get("sneaking").getAsBoolean();}
                        public boolean dampensVibrations(){return flags.has("dampensVibrations") && flags.get("dampensVibrations").getAsBoolean();}
                    };
                }
                BlockState affected=null;
                if(input.has("affectedBlock")) {
                    var block=input.getAsJsonObject("affectedBlock");
                    if(block.has("properties") && block.getAsJsonObject("properties").size()!=0)throw new IllegalArgumentException("Reference event affected properties not supported");
                    affected=BuiltInRegistries.BLOCK.getValue(Identifier.parse(block.get("name").getAsString())).defaultBlockState();
                }
                level.gameEvent(event,location,new net.minecraft.world.level.gameevent.GameEvent.Context(source,affected));
            } else if(state.getBlock() instanceof BellBlock bell) {
                if(input.has("ring"))bell.attemptToRing(level,pos,null);
                else {
                    var direction=Direction.byName(input.get("face").getAsString());
                    var hit=new net.minecraft.world.phys.BlockHitResult(new net.minecraft.world.phys.Vec3(pos.getX()+.5,pos.getY()+input.get("height").getAsDouble(),pos.getZ()+.5),direction,pos,false);
                    bell.onHit(level,state,hit,null,true);
                }
            } else if (state.getBlock() instanceof NoteBlock note) {
                var player=mockPlayer(level, GameType.CREATIVE);
                var method=NoteBlock.class.getDeclaredMethod("attack",BlockState.class,Level.class,BlockPos.class,Player.class);
                method.setAccessible(true);method.invoke(note,state,level,pos,player);
            } else if (state.getBlock() instanceof TargetBlock) {
                var values=input.getAsJsonArray("hit");
                var location=new net.minecraft.world.phys.Vec3(pos.getX()+values.get(0).getAsDouble(),pos.getY()+values.get(1).getAsDouble(),pos.getZ()+values.get(2).getAsDouble());
                var direction=Direction.byName(input.get("face").getAsString());
                var hit=new net.minecraft.world.phys.BlockHitResult(location,direction,pos,false);
                var type=BuiltInRegistries.ENTITY_TYPE.getValue(Identifier.withDefaultNamespace(input.get("arrow").getAsBoolean()?"arrow":"snowball"));
                var entity=type.create(level,EntitySpawnReason.COMMAND);
                var method=TargetBlock.class.getDeclaredMethod("updateRedstoneOutput",net.minecraft.world.level.LevelAccessor.class,BlockState.class,net.minecraft.world.phys.BlockHitResult.class,Entity.class);
                method.setAccessible(true);method.invoke(null,level,state,hit,entity);
            } else if (state.getBlock() instanceof ButtonBlock button) {
                for (var entity : occupants.getOrDefault(pos, List.of())) entity.discard();
                var list = new ArrayList<Entity>(); occupants.put(pos, list);
                int arrows = input.get("arrows").getAsInt(), pressedArrows = input.has("pressedArrows") ? input.get("pressedArrows").getAsInt() : arrows;
                var center = state.setValue(ButtonBlock.POWERED, true).getShape(level, pos).bounds().getCenter();
                var direction = switch (state.getValue(ButtonBlock.FACE)) {
                    case FLOOR -> Direction.UP;
                    case CEILING -> Direction.DOWN;
                    default -> state.getValue(ButtonBlock.FACING);
                };
                for (int i = 0; i < arrows; ++i) {
                    var type = BuiltInRegistries.ENTITY_TYPE.getValue(Identifier.withDefaultNamespace("arrow"));
                    var arrow = (net.minecraft.world.entity.projectile.arrow.AbstractArrow)type.create(level, EntitySpawnReason.COMMAND);
                    double extent = direction.getAxis() == Direction.Axis.Y ? arrow.getBbHeight() : arrow.getBbWidth();
                    double offset = i < pressedArrows ? 0 : extent / 2 + 1.0 / 16;
                    arrow.setPos(pos.getX() + center.x + direction.getStepX() * offset,
                        pos.getY() + center.y - arrow.getBbHeight() / 2 + direction.getStepY() * offset,
                        pos.getZ() + center.z + direction.getStepZ() * offset);
                    // Keep ordinary block contact processing enabled. Setting
                    // noPhysics also disables entity-inside effects in 26.2.
                    arrow.setNoGravity(true);
                    level.addFreshEntity(arrow); list.add(arrow);
                }
                if (!state.getValue(ButtonBlock.POWERED)) {
                    var method = ButtonBlock.class.getDeclaredMethod("checkPressed", BlockState.class, Level.class, BlockPos.class);
                    method.setAccessible(true); method.invoke(button, state, level, pos);
                }
            } else if (state.getBlock() instanceof TripWireBlock wire) {
                if (input.has("shear")) {
                    var player = mockPlayer(level, GameType.CREATIVE);
                    player.setItemInHand(net.minecraft.world.InteractionHand.MAIN_HAND, new ItemStack(Items.SHEARS));
                    wire.playerWillDestroy(level, pos, state, player);
                    level.setBlock(pos, Block.stateById(0), 3);
                } else {
                    for (var entity : occupants.getOrDefault(pos, List.of())) entity.discard();
                    var list = new ArrayList<Entity>(); occupants.put(pos, list);
                    for (int i = 0; i < input.get("entities").getAsInt(); ++i) {
                        var entity = new ArmorStand(level, pos.getX() + .5, pos.getY() + .08, pos.getZ() + .5);
                        entity.setNoGravity(true); level.addFreshEntity(entity); list.add(entity);
                    }
                    if (!state.getValue(TripWireBlock.POWERED) && !level.getBlockTicks().hasScheduledTick(pos, wire)) {
                        var method = TripWireBlock.class.getDeclaredMethod("checkPressed", Level.class, BlockPos.class, List.class);
                        method.setAccessible(true); method.invoke(wire, level, pos, list);
                    }
                }
            } else if (state.getBlock() instanceof DetectorRailBlock detector) {
                if (input.has("carts")) {
                    for (var entity : occupants.getOrDefault(pos, List.of())) entity.discard();
                    var list = new ArrayList<Entity>(); occupants.put(pos, list);
                    for (var value : input.getAsJsonArray("carts")) {
                        var cart = value.getAsJsonObject();
                        var entityType = BuiltInRegistries.ENTITY_TYPE.getValue(Identifier.withDefaultNamespace(cart.get("type").getAsString()));
                        var entity = entityType.create(level, EntitySpawnReason.COMMAND);
                        entity.setPos(pos.getX() + .5, pos.getY() + .0625, pos.getZ() + .5); entity.setNoGravity(true);
                        if (entity instanceof Container container && cart.has("inventory")) for (var item : cart.getAsJsonArray("inventory")) {
                            var row = item.getAsJsonObject(); int count = row.get("count").getAsInt();
                            container.setItem(row.get("slot").getAsInt(), count == 0 ? ItemStack.EMPTY : new ItemStack(BuiltInRegistries.ITEM.getValue(Identifier.parse(row.get("item").getAsString())), count));
                        }
                        level.addFreshEntity(entity); list.add(entity);
                    }
                } else if (input.has("cartInventory")) {
                    for (var entity : occupants.getOrDefault(pos, List.of())) if (entity instanceof Container container) {
                        for (var item : input.getAsJsonArray("cartInventory")) {
                            var row = item.getAsJsonObject(); int count = row.get("count").getAsInt();
                            container.setItem(row.get("slot").getAsInt(), count == 0 ? ItemStack.EMPTY : new ItemStack(BuiltInRegistries.ITEM.getValue(Identifier.parse(row.get("item").getAsString())), count));
                        }
                        break;
                    }
                }
                if (!state.getValue(DetectorRailBlock.POWERED)) {
                    var method = DetectorRailBlock.class.getDeclaredMethod("checkPressed", Level.class, BlockPos.class, BlockState.class);
                    method.setAccessible(true); method.invoke(detector, level, pos, state);
                }
            } else if (level.getBlockEntity(pos) instanceof Container localContainer) {
                Container container = state.getBlock() instanceof ChestBlock chest ? ChestBlock.getContainer(chest, state, level, pos, true) : localContainer;
                if (input.has("inventory")) {
                    for (var value : input.getAsJsonArray("inventory")) {
                        var row = value.getAsJsonObject(); int count = row.get("count").getAsInt();
                        var stack = count == 0 ? ItemStack.EMPTY : new ItemStack(BuiltInRegistries.ITEM.getValue(Identifier.parse(row.get("item").getAsString())), count);
                        container.setItem(row.get("slot").getAsInt(), stack);
                    }
                    container.setChanged();
                } else if (input.has("viewers")) {
                    var players = viewers.computeIfAbsent(pos, key -> new ArrayList<>());
                    int target = input.get("viewers").getAsInt();
                    while (players.size() < target) { var player = mockPlayer(level, GameType.CREATIVE); players.add(player); container.startOpen(player); }
                    while (players.size() > target) container.stopOpen(players.removeLast());
                } else throw new IllegalArgumentException("Unknown inventory input");
            } else if (state.getBlock() instanceof BasePressurePlateBlock plate) {
                for (var entity : occupants.getOrDefault(pos, List.of())) entity.discard();
                var list = new ArrayList<Entity>(); occupants.put(pos, list);
                int count = input.get("entities").getAsInt(), living = input.has("livingEntities") ? input.get("livingEntities").getAsInt() : count;
                for (int i = 0; i < count; ++i) {
                    Entity entity;
                    if (i < living) entity = new ArmorStand(level, pos.getX() + .5, pos.getY() + .065, pos.getZ() + .5);
                    else {
                        var item = new ItemStack(Items.STONE); item.set(DataComponents.CUSTOM_NAME, Component.literal("fixture item " + i));
                        entity = new ItemEntity(level, pos.getX() + .5, pos.getY() + .065, pos.getZ() + .5, item, 0, 0, 0);
                    }
                    entity.setNoGravity(true); level.addFreshEntity(entity); list.add(entity);
                }
                int old = state.getSignal(level, pos, Direction.UP);
                if (old == 0) {
                    // Invoke the same original routine used by an entity entering the plate.
                    var method = BasePressurePlateBlock.class.getDeclaredMethod("checkPressed", Entity.class, Level.class, BlockPos.class, BlockState.class, int.class);
                    method.setAccessible(true); method.invoke(plate, null, level, pos, state, old);
                }
            } else if (state.getBlock() instanceof LightningRodBlock rod) rod.onLightningStrike(state, level, pos);
            else if (level.getBlockEntity(pos) instanceof LecternBlockEntity lectern) {
                if (input.has("pages")) {
                    int pages = input.get("pages").getAsInt(); var book = ItemStack.EMPTY;
                    if (pages > 0) { book = new ItemStack(Items.WRITABLE_BOOK); book.set(DataComponents.WRITABLE_BOOK_CONTENT, new WritableBookContent(Collections.nCopies(pages, Filterable.passThrough("fixture page")))); }
                    lectern.setBook(book); LecternBlock.resetBookState(null, level, pos, state, pages > 0);
                } else {
                    var method = LecternBlockEntity.class.getDeclaredMethod("setPage", int.class);
                    method.setAccessible(true); method.invoke(lectern, input.get("page").getAsInt());
                }
            } else throw new IllegalArgumentException("Unsupported reference stimulus");
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
    }
    @Override public void load(BiConsumer<ResourceKey<Consumer<GameTestHelper>>, Consumer<GameTestHelper>> register) {
        register.accept(ResourceKey.create(Registries.TEST_FUNCTION, Identifier.parse("simulator:capture")), CaptureRedstone::capture);
    }
    static void capture(GameTestHelper helper) {
        runTimeline(helper.getLevel(), helper.absolutePos(BlockPos.ZERO),
            "Minecraft Java 26.2 GameTest, nonexperimental redstone",
            (tick, action) -> { if (tick == 0) action.run(); else helper.runAtTickTime(tick, action); },
            helper::succeed);
    }
    /** Schedules one timeline step; the two harnesses differ only in how a tick is reached. */
    public interface TickHook { void at(int tick, Runnable action); }
    /**
     * Replays the scenario against an already running level. `origin` is the absolute position of
     * relative (0,0,0); every command, watch entry and trace coordinate is expressed against it.
     * The action for tick t must run at the same phase of the server tick in both harnesses:
     * after the level tick, which is where GameTestTicker runs and where our own server calls back.
     */
    static void runTimeline(net.minecraft.server.level.ServerLevel level, BlockPos origin, String referenceLabel, TickHook hook, Runnable onFinished) {
        // Match referenceVersion.json: natural random ticks are external to this
        // loaded-region circuit model. Explicit randomTick stimuli still execute.
        level.getGameRules().set(net.minecraft.world.level.gamerules.GameRules.RANDOM_TICK_SPEED, 0, level.getServer());
        JsonObject result = scenario.deepCopy();
        result.addProperty("reference", referenceLabel);
        // Record the actual harness environment, not just our intended profile.
        // GameTest enables trade_rebalance even though redstone experiments are off.
        JsonObject environment = new JsonObject();
        environment.addProperty("javaVersion", System.getProperty("java.version"));
        environment.addProperty("randomTickSpeed", level.getGameRules().get(net.minecraft.world.level.gamerules.GameRules.RANDOM_TICK_SPEED));
        JsonArray featureFlags = new JsonArray();
        net.minecraft.world.flag.FeatureFlags.REGISTRY.toNames(level.enabledFeatures()).stream()
            .map(Object::toString).sorted().forEach(featureFlags::add);
        environment.add("featureFlags", featureFlags);
        JsonArray enabledPacks = new JsonArray();
        level.getServer().getPackRepository().getSelectedIds().stream().sorted().forEach(enabledPacks::add);
        environment.add("dataPacks", enabledPacks);
        result.add("referenceEnvironment", environment);
        result.add("origin", coordinates(origin));
        JsonArray frames = new JsonArray(); result.add("frames", frames);
        if(scenario.has("forceLoadedNeighborhood") && scenario.get("forceLoadedNeighborhood").getAsBoolean()) {
            for(int x=Math.floorDiv(origin.getX(),16)-1;x<=Math.floorDiv(origin.getX()+48,16)+1;++x)
                for(int z=Math.floorDiv(origin.getZ(),16)-1;z<=Math.floorDiv(origin.getZ()+48,16)+1;++z) {
                    level.getChunk(x,z);level.setChunkForced(x,z,true);
                }
        }
        for (int x = 0; x < 48; ++x) for (int y = 0; y < 6; ++y) for (int z = 0; z < 48; ++z) level.setBlock(origin.offset(x,y,z), Block.stateById(0), 18);
        traceLimit = scenario.has("updateTraceLimit") ? scenario.get("updateTraceLimit").getAsInt() : 0;
        traceTruncated = false; traceEntries = new JsonArray();
        int end = scenario.get("endTick").getAsInt();
        for (int t = 0; t <= end; ++t) {
            final int tick = t;
            hook.at(t, () -> {
                if (tick == 0) {
                    // Sampled at the first timeline tick, not at setup: an independent harness can
                    // only line up daylight and the 20 gt detector phase if it knows both clocks.
                    // 26.2 keeps day time in the WorldClock registry, not in level data.
                    environment.addProperty("gameTime", level.getGameTime());
                    level.dimensionType().defaultClock().ifPresent(
                        clock -> environment.addProperty("clockTicks", level.clockManager().getTotalTicks(clock)));
                }
                if (traceLimit > 0) {
                    // ServerLevel.tick clears the listener every tick when nothing subscribes,
                    // so reinstall it here; it then covers the next server tick and these commands.
                    traceOrigin = origin;
                    var updater = neighborUpdaterOf(level);
                    traceStack = stackOf(updater);
                    remainingInBurst = 0;
                    updater.setDebugListener(CaptureRedstone::recordTraceEntry);
                    appendTrace(new JsonPrimitive(tick));
                }
                for (var value : scenario.getAsJsonArray("commands")) {
                    JsonObject command = value.getAsJsonObject(); if (command.get("tick").getAsInt() != tick) continue;
                    applyCommand(level, origin.offset(pos(command.getAsJsonArray("pos"))), command);
                }
                if (scenario.has("discardDrops") && scenario.get("discardDrops").getAsBoolean()) {
                    // Isolate block logic from random dropped-hook trajectories;
                    // explicit contact inputs remain real persistent entities.
                    var bounds = net.minecraft.world.phys.AABB.encapsulatingFullBlocks(origin.offset(-4,-4,-4), origin.offset(52,10,52));
                    for (var drop : level.getEntitiesOfClass(ItemEntity.class, bounds)) drop.discard();
                }
                JsonObject frame = new JsonObject(); frame.addProperty("tick", tick); JsonArray states = new JsonArray(); JsonArray analogs = new JsonArray(); JsonArray inventories = new JsonArray();JsonArray bells=new JsonArray();JsonArray jukeboxes=new JsonArray();
                for (var value : scenario.getAsJsonArray("watch")) {
                    var absolute = origin.offset(pos(value.getAsJsonArray()));
                    states.add(Block.getId(level.getBlockState(absolute)));
                    var entity = level.getBlockEntity(absolute);
                    var state = level.getBlockState(absolute);
                    bells.add(entity instanceof net.minecraft.world.level.block.entity.BellBlockEntity bell && bell.shaking);
                    if(entity instanceof net.minecraft.world.level.block.entity.JukeboxBlockEntity box) {
                        var player=new JsonObject();player.addProperty("playing",box.getSongPlayer().isPlaying());player.addProperty("elapsed",box.getSongPlayer().getTicksSinceSongStarted());jukeboxes.add(player);
                    }else jukeboxes.add(JsonNull.INSTANCE);
                    analogs.add(entity instanceof ComparatorBlockEntity comparator ? comparator.getOutputSignal() : state.hasAnalogOutputSignal() ? state.getAnalogOutputSignal(level, absolute, Direction.NORTH) : -1);
                    JsonArray inventory = new JsonArray();
                    if (entity instanceof Container container) for (int slot = 0; slot < container.getContainerSize(); ++slot) {
                        var stack = container.getItem(slot);
                        if (!stack.isEmpty()) { var row = new JsonObject(); row.addProperty("slot", slot); row.addProperty("item", BuiltInRegistries.ITEM.getKey(stack.getItem()).toString()); row.addProperty("count", stack.getCount()); inventory.add(row); }
                    }
                    inventories.add(inventory);
                }
                frame.add("states", states); frame.add("analogs", analogs); frame.add("inventories", inventories); frames.add(frame);
                if (scenario.has("watchGroundItems")) {
                    // 直接调用原版 HopperBlockEntity.getItemsAtAndAbove，吸取范围判据不是我们自己写的。
                    JsonArray ground = new JsonArray();
                    for (var value : scenario.getAsJsonArray("watch")) {
                        var absolute = origin.offset(pos(value.getAsJsonArray()));
                        if (level.getBlockEntity(absolute) instanceof net.minecraft.world.level.block.entity.HopperBlockEntity hopper) {
                            JsonArray drops = new JsonArray();
                            for (var drop : net.minecraft.world.level.block.entity.HopperBlockEntity.getItemsAtAndAbove(level, hopper)) {
                                var row = new JsonObject();
                                row.addProperty("item", BuiltInRegistries.ITEM.getKey(drop.getItem().getItem()).toString());
                                row.addProperty("count", drop.getItem().getCount());
                                drops.add(row);
                            }
                            ground.add(drops);
                        } else ground.add(JsonNull.INSTANCE);
                    }
                    frame.add("groundItems", ground);
                }
                if(scenario.has("watchBells"))frame.add("bells",bells);
                if(scenario.has("watchJukeboxes"))frame.add("jukeboxes",jukeboxes);
                if (tick == end) {
                    if (traceLimit > 0) {
                        result.addProperty("updateTraceLimit", traceLimit);
                        result.addProperty("updateTraceTruncated", traceTruncated);
                        result.add("updateTrace", traceEntries);
                    }
                    try { Files.writeString(output, new GsonBuilder().setPrettyPrinting().create().toJson(result)); }
                    catch (Exception e) { throw new RuntimeException(e); }
                    onFinished.run();
                }
            });
        }
    }
    public static void main(String[] args) throws Exception {
        scenario = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject(); output = Path.of(args[1]);
        SharedConstants.tryDetectVersion(); TestFunctionLoader.registerLoader(new CaptureRedstone());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe", args[2], "--packs", args[3], "--tests", "simulator:capture", "--report", args[4]}, path -> {});
    }
}
