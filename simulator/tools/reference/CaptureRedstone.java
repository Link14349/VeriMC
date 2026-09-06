import com.google.gson.*;
import java.nio.file.*;
import java.util.function.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.ComparatorBlockEntity;

/** Executes our input timeline inside the unmodified Java 26.2 GameTest world. */
public class CaptureRedstone extends TestFunctionLoader {
    static JsonObject scenario;
    static Path output;
    static BlockPos pos(JsonArray p) { return new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt()); }
    static JsonArray coordinates(BlockPos p) { JsonArray a = new JsonArray(); a.add(p.getX()); a.add(p.getY()); a.add(p.getZ()); return a; }
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
                    helper.getLevel().setBlock(absolute, Block.stateById(command.get("stateId").getAsInt()), 3);
                }
                JsonObject frame = new JsonObject(); frame.addProperty("tick", tick); JsonArray states = new JsonArray(); JsonArray analogs = new JsonArray();
                for (var value : scenario.getAsJsonArray("watch")) {
                    var absolute = helper.absolutePos(pos(value.getAsJsonArray()));
                    states.add(Block.getId(helper.getLevel().getBlockState(absolute)));
                    var entity = helper.getLevel().getBlockEntity(absolute);
                    analogs.add(entity instanceof ComparatorBlockEntity comparator ? comparator.getOutputSignal() : -1);
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
