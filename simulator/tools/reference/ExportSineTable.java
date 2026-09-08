import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.SharedConstants;
import net.minecraft.server.Bootstrap;
import net.minecraft.util.Mth;

/** Exports the whole 65,536 entry sine table and the daylight index boundaries. */
public class ExportSineTable {
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Field field = Mth.class.getDeclaredField("SIN");
        field.setAccessible(true);
        float[] table = (float[])field.get(null);
        if (table.length != 65536) throw new IllegalStateException("Unexpected sine table size " + table.length);
        JsonObject result = new JsonObject();
        result.addProperty("version", "26.2");
        JsonArray bits = new JsonArray();
        for (float value : table) bits.add(Float.floatToRawIntBits(value));
        result.add("sineBits", bits);
        // Daylight only ever calls Mth.cos, so record the index and value it lands on for the
        // angles the block can actually produce, including both sides of the pi threshold.
        JsonArray cosSamples = new JsonArray();
        for (int degrees = 0; degrees < 360; ++degrees) {
            for (int step = -1; step <= 1; ++step) {
                float raw = Math.nextAfter((float)degrees, step < 0 ? Float.NEGATIVE_INFINITY : step > 0 ? Float.POSITIVE_INFINITY : (float)degrees);
                if (raw < 0.0F) continue; // The block only ever receives angles in [0, 360].
                float angle = raw * (float)(Math.PI / 180.0);
                float offset = angle < (float)Math.PI ? 0.0F : (float)(Math.PI * 2);
                float shifted = angle + (offset - angle) * 0.2F;
                JsonObject sample = new JsonObject();
                sample.addProperty("degrees", raw);
                sample.addProperty("shifted", shifted);
                sample.addProperty("index", (int)((long)(shifted * 10430.378350470453 + 16384.0) & 65535L));
                sample.addProperty("cosBits", Float.floatToRawIntBits(Mth.cos(shifted)));
                JsonArray strengths = new JsonArray();
                for (int sky = 0; sky <= 15; ++sky) {
                    int target = sky;
                    if (target > 0) target = Math.round(target * Mth.cos(shifted));
                    strengths.add(Mth.clamp(target, 0, 15));
                }
                sample.add("strengths", strengths);
                cosSamples.add(sample);
            }
        }
        result.add("cosSamples", cosSamples);
        Files.writeString(Path.of(args[0]), new GsonBuilder().create().toJson(result));
        System.out.println("Exported " + table.length + " sine entries and " + cosSamples.size() + " daylight samples");
    }
}
