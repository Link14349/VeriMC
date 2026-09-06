import com.google.gson.*;
import java.nio.file.*;
import java.util.*;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.ticks.*;

/** Calls the pinned game's real scheduler with synthetic tick consumers. */
public class ExportScheduler {
    static final String[] types = {"a", "b", "c"};
    static final LevelTicks<String> ticks = new LevelTicks<>(key -> true);
    static final Set<Long> loaded = new HashSet<>();
    static long nextOrder;
    static JsonArray coordinates(BlockPos pos) {
        var row = new JsonArray(); row.add(pos.getX()); row.add(pos.getY()); row.add(pos.getZ()); return row;
    }
    static BlockPos position(JsonObject row) {
        var p = row.getAsJsonArray("pos"); return new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt());
    }
    static JsonObject event(BlockPos pos, int type, long tick, int priority) {
        var row = new JsonObject(); row.add("pos", coordinates(pos)); row.addProperty("type", type);
        row.addProperty("tick", tick); row.addProperty("priority", priority); row.addProperty("order", nextOrder++); return row;
    }
    static void schedule(JsonObject row) {
        var pos = position(row); var chunk = new ChunkPos(pos.getX() >> 4, pos.getZ() >> 4);
        if (loaded.add(chunk.pack())) ticks.addContainer(chunk, new LevelChunkTicks<>());
        ticks.schedule(new ScheduledTick<>(types[row.get("type").getAsInt()], pos, row.get("tick").getAsLong(), TickPriority.byValue(row.get("priority").getAsInt()), row.get("order").getAsLong()));
    }
    public static void main(String[] args) throws Exception {
        var result = new JsonObject();
        result.addProperty("reference", "Minecraft Java 26.2 LevelTicks and LevelChunkTicks; synthetic callbacks, all chunks loaded");
        var initial = new JsonArray(); result.add("initial", initial);
        for (int i = 0; i < 180; ++i) {
            var pos = new BlockPos((i % 9 - 4) * 16 + i % 3, i / 27, (i % 7 - 3) * 16);
            var row = event(pos, i % 3, i * 13 % 11, i * 5 % 7 - 3);
            initial.add(row); schedule(row);
            if (i % 7 == 0) { var duplicate = event(pos, i % 3, 0, -3); initial.add(duplicate); schedule(duplicate); }
        }
        var probes = new JsonArray(); result.add("probes", probes);
        for (int i : new int[]{0, 2, 5, 10, 19, 35, 70, 140, 180}) probes.add(initial.get(i).deepCopy());
        var runs = new JsonArray(); result.add("runs", runs);
        for (int t = 10; t < 31; ++t) {
            final int tick = t, limit = t < 25 ? 11 : 65536;
            var run = new JsonObject(); run.addProperty("tick", tick); run.addProperty("limit", limit); runs.add(run);
            var injections = new JsonArray(); run.add("injections", injections);
            var output = new JsonArray(); run.add("events", output);
            ticks.tick(tick, limit, (pos, type) -> {
                int index = output.size();
                if (tick < 14 && (index == 0 || index == 3)) {
                    var source = index == 0 ? probes.get(1).getAsJsonObject() : probes.get(0).getAsJsonObject();
                    var injected = event(position(source), source.get("type").getAsInt(), tick, index == 0 ? -2 : 1);
                    var injection = new JsonObject(); injection.addProperty("after", index); injection.add("event", injected);
                    injections.add(injection); schedule(injected);
                }
                var row = new JsonObject(); row.add("pos", coordinates(pos)); row.addProperty("type", Arrays.asList(types).indexOf(type));
                var queued = new StringBuilder(); var collected = new StringBuilder();
                for (var value : probes) {
                    var probe = value.getAsJsonObject(); var probePos = position(probe); var probeType = types[probe.get("type").getAsInt()];
                    queued.append(ticks.hasScheduledTick(probePos, probeType) ? '1' : '0');
                    collected.append(ticks.willTickThisTick(probePos, probeType) ? '1' : '0');
                }
                row.addProperty("queued", queued.toString()); row.addProperty("collected", collected.toString()); output.add(row);
            });
            run.addProperty("remaining", ticks.count());
        }
        Files.writeString(Path.of(args[0]), new GsonBuilder().setPrettyPrinting().create().toJson(result));
    }
}
