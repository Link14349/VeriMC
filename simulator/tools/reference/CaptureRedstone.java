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
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.component.WritableBookContent;
import net.minecraft.server.network.Filterable;
import net.minecraft.network.chat.Component;

/** Executes our input timeline inside the unmodified Java 26.2 GameTest world. */
public class CaptureRedstone extends TestFunctionLoader {
    static JsonObject scenario;
    static Path output;
    static Map<BlockPos, List<Entity>> occupants = new HashMap<>();
    static BlockPos pos(JsonArray p) { return new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt()); }
    static JsonArray coordinates(BlockPos p) { JsonArray a = new JsonArray(); a.add(p.getX()); a.add(p.getY()); a.add(p.getZ()); return a; }
    static void applyCommand(GameTestHelper helper, BlockPos pos, JsonObject command) {
        var level = helper.getLevel();
        if (command.has("stateId")) { level.setBlock(pos, Block.stateById(command.get("stateId").getAsInt()), 3); return; }
        var state = level.getBlockState(pos);
        try {
            if (command.has("interact")) {
                if (state.getBlock() instanceof DoorBlock door) door.setOpen(null, level, state, pos, !state.getValue(DoorBlock.OPEN));
                else throw new IllegalArgumentException("Unsupported reference interaction");
                return;
            }
            var input = command.getAsJsonObject("stimulus");
            if (state.getBlock() instanceof BasePressurePlateBlock plate) {
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
                JsonObject frame = new JsonObject(); frame.addProperty("tick", tick); JsonArray states = new JsonArray(); JsonArray analogs = new JsonArray();
                for (var value : scenario.getAsJsonArray("watch")) {
                    var absolute = helper.absolutePos(pos(value.getAsJsonArray()));
                    states.add(Block.getId(helper.getLevel().getBlockState(absolute)));
                    var entity = helper.getLevel().getBlockEntity(absolute);
                    var state = helper.getLevel().getBlockState(absolute);
                    analogs.add(entity instanceof ComparatorBlockEntity comparator ? comparator.getOutputSignal() : state.hasAnalogOutputSignal() ? state.getAnalogOutputSignal(helper.getLevel(), absolute, Direction.NORTH) : -1);
                }
                frame.add("states", states); frame.add("analogs", analogs); frames.add(frame);
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
