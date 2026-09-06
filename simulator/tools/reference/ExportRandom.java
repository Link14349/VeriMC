import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

/** Exact primitive bits and draw counts from the pinned game's random source. */
public class ExportRandom {
    static class CountedRandom extends LegacyRandomSource {
        long draws;
        CountedRandom(long seed) { super(seed); }
        @Override public int next(int bits) { ++draws; return super.next(bits); }
    }
    public static void main(String[] args) throws Exception {
        var result = new JsonObject();
        result.addProperty("reference", "Minecraft Java 26.2 LegacyRandomSource, BitRandomSource and RandomSource.triangle; primitive fixture, not whole-world random consumption");
        var cases = new JsonArray(); result.add("cases", cases);
        var state = LegacyRandomSource.class.getDeclaredField("seed"); state.setAccessible(true);
        for (long seed : new long[]{0, 1, -1, Long.MIN_VALUE, Long.MAX_VALUE, 0x123456789abcdefL}) {
            var test = new JsonObject(); test.addProperty("seedBits", Long.toUnsignedString(seed, 16)); cases.add(test);
            var random = new CountedRandom(seed); var calls = new JsonArray(); test.add("calls", calls);
            for (int i = 0; i < 1000; ++i) {
                var row = new JsonObject(); calls.add(row);
                switch (i % 10) {
                    case 0 -> { row.addProperty("op", "int"); row.addProperty("value", random.nextInt()); }
                    case 1, 2, 3 -> {
                        int bound = i % 10 == 1 ? 1 : i % 10 == 2 ? (i / 10 % 2 == 0 ? 7 : 1 << 30) : (i % 4 == 3 ? (1 << 30) + 1 : Integer.MAX_VALUE);
                        row.addProperty("op", "bound"); row.addProperty("bound", bound); row.addProperty("value", random.nextInt(bound));
                    }
                    case 4 -> { row.addProperty("op", "long"); row.addProperty("bits", Long.toUnsignedString(random.nextLong(), 16)); }
                    case 5 -> { row.addProperty("op", "bool"); row.addProperty("value", random.nextBoolean()); }
                    case 6 -> { row.addProperty("op", "float"); row.addProperty("bits", Integer.toUnsignedString(Float.floatToRawIntBits(random.nextFloat()), 16)); }
                    case 7 -> { row.addProperty("op", "double"); row.addProperty("bits", Long.toUnsignedString(Double.doubleToRawLongBits(random.nextDouble()), 16)); }
                    case 8 -> { row.addProperty("op", "triangleDouble"); row.addProperty("bits", Long.toUnsignedString(Double.doubleToRawLongBits(random.triangle(3.75, .123)), 16)); }
                    case 9 -> { row.addProperty("op", "triangleFloat"); row.addProperty("bits", Integer.toUnsignedString(Float.floatToRawIntBits(random.triangle(-.25F, .137F)), 16)); }
                }
                row.addProperty("state", ((AtomicLong)state.get(random)).get()); row.addProperty("draws", random.draws);
            }
        }
        Files.writeString(Path.of(args[0]), new Gson().toJson(result));
    }
}
