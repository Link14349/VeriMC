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
    static Map<BlockPos, List<Entity>> occupants = new HashMap<>();
    static Map<BlockPos, List<Player>> viewers = new HashMap<>();
    static BlockPos pos(JsonArray p) { return new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt()); }
    static JsonArray coordinates(BlockPos p) { JsonArray a = new JsonArray(); a.add(p.getX()); a.add(p.getY()); a.add(p.getZ()); return a; }
    static void applyCommand(GameTestHelper helper, BlockPos pos, JsonObject command) {
        var level = helper.getLevel();
        if (command.has("stateId")) {
            var placed = Block.stateById(command.get("stateId").getAsInt());
            if (command.has("playerPlace")) {
                var player=helper.makeMockPlayer(GameType.CREATIVE);
                player.setYRot(placed.getValue(ChestBlock.FACING).getOpposite().toYRot());
                var hit=new net.minecraft.world.phys.BlockHitResult(net.minecraft.world.phys.Vec3.atCenterOf(pos.below()),Direction.UP,pos.below(),false);
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
                else if (state.getBlock() instanceof ButtonBlock button) { if (!state.getValue(ButtonBlock.POWERED)) button.press(state, level, pos, null); }
                else if (state.getBlock() instanceof NoteBlock note) {
                    var player=helper.makeMockPlayer(GameType.CREATIVE);
                    var hit=new net.minecraft.world.phys.BlockHitResult(net.minecraft.world.phys.Vec3.atCenterOf(pos),Direction.UP,pos,false);
                    var method=NoteBlock.class.getDeclaredMethod("useWithoutItem",BlockState.class,Level.class,BlockPos.class,Player.class,net.minecraft.world.phys.BlockHitResult.class);
                    method.setAccessible(true);method.invoke(note,state,level,pos,player,hit);
                }
                else throw new IllegalArgumentException("Unsupported reference interaction");
                return;
            }
            var input = command.getAsJsonObject("stimulus");
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
            } else if (state.getBlock() instanceof NoteBlock note) {
                var player=helper.makeMockPlayer(GameType.CREATIVE);
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
                    var player = helper.makeMockPlayer(GameType.CREATIVE);
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
                    while (players.size() < target) { var player = helper.makeMockPlayer(GameType.CREATIVE); players.add(player); container.startOpen(player); }
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
        JsonObject result = scenario.deepCopy();
        result.addProperty("reference", "Minecraft Java 26.2 GameTest, nonexperimental redstone");
        result.add("origin", coordinates(helper.absolutePos(BlockPos.ZERO)));
        JsonArray frames = new JsonArray(); result.add("frames", frames);
        if(scenario.has("forceLoadedNeighborhood") && scenario.get("forceLoadedNeighborhood").getAsBoolean()) {
            var base=helper.absolutePos(BlockPos.ZERO);
            for(int x=Math.floorDiv(base.getX(),16)-1;x<=Math.floorDiv(base.getX()+48,16)+1;++x)
                for(int z=Math.floorDiv(base.getZ(),16)-1;z<=Math.floorDiv(base.getZ()+48,16)+1;++z) {
                    helper.getLevel().getChunk(x,z);helper.getLevel().setChunkForced(x,z,true);
                }
        }
        for (int x = 0; x < 48; ++x) for (int y = 0; y < 6; ++y) for (int z = 0; z < 48; ++z) helper.getLevel().setBlock(helper.absolutePos(new BlockPos(x,y,z)), Block.stateById(0), 18);
        int end = scenario.get("endTick").getAsInt();
        for (int t = 0; t <= end; ++t) {
            final int tick = t;
            Runnable run = () -> {
                for (var value : scenario.getAsJsonArray("commands")) {
                    JsonObject command = value.getAsJsonObject(); if (command.get("tick").getAsInt() != tick) continue;
                    var absolute = helper.absolutePos(pos(command.getAsJsonArray("pos")));
                    applyCommand(helper, absolute, command);
                }
                if (scenario.has("discardDrops") && scenario.get("discardDrops").getAsBoolean()) {
                    // Isolate block logic from random dropped-hook trajectories;
                    // explicit contact inputs remain real persistent entities.
                    var bounds = net.minecraft.world.phys.AABB.encapsulatingFullBlocks(helper.absolutePos(new BlockPos(-4,-4,-4)), helper.absolutePos(new BlockPos(52,10,52)));
                    for (var drop : helper.getLevel().getEntitiesOfClass(ItemEntity.class, bounds)) drop.discard();
                }
                JsonObject frame = new JsonObject(); frame.addProperty("tick", tick); JsonArray states = new JsonArray(); JsonArray analogs = new JsonArray(); JsonArray inventories = new JsonArray();
                for (var value : scenario.getAsJsonArray("watch")) {
                    var absolute = helper.absolutePos(pos(value.getAsJsonArray()));
                    states.add(Block.getId(helper.getLevel().getBlockState(absolute)));
                    var entity = helper.getLevel().getBlockEntity(absolute);
                    var state = helper.getLevel().getBlockState(absolute);
                    analogs.add(entity instanceof ComparatorBlockEntity comparator ? comparator.getOutputSignal() : state.hasAnalogOutputSignal() ? state.getAnalogOutputSignal(helper.getLevel(), absolute, Direction.NORTH) : -1);
                    JsonArray inventory = new JsonArray();
                    if (entity instanceof Container container) for (int slot = 0; slot < container.getContainerSize(); ++slot) {
                        var stack = container.getItem(slot);
                        if (!stack.isEmpty()) { var row = new JsonObject(); row.addProperty("slot", slot); row.addProperty("item", BuiltInRegistries.ITEM.getKey(stack.getItem()).toString()); row.addProperty("count", stack.getCount()); inventory.add(row); }
                    }
                    inventories.add(inventory);
                }
                frame.add("states", states); frame.add("analogs", analogs); frame.add("inventories", inventories); frames.add(frame);
                if (tick == end) {
                    try { Files.writeString(output, new GsonBuilder().setPrettyPrinting().create().toJson(result)); }
                    catch (Exception e) { throw new RuntimeException(e); }
                    helper.succeed();
                }
            };
            if (t == 0) run.run(); else helper.runAtTickTime(t, run);
        }
    }
    public static void main(String[] args) throws Exception {
        scenario = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject(); output = Path.of(args[1]);
        SharedConstants.tryDetectVersion(); TestFunctionLoader.registerLoader(new CaptureRedstone());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe", args[2], "--packs", args[3], "--tests", "simulator:capture", "--report", args[4]}, path -> {});
    }
}
