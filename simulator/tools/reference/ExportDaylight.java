import com.google.gson.*;
import java.nio.file.*;
import net.minecraft.util.Mth;

/** Observable numeric fixtures for the 26.2 daylight formula and Mth table. */
public class ExportDaylight {
    public static void main(String[] args) throws Exception {
        JsonArray samples = new JsonArray();
        for (int step = 0; step <= 720; ++step) {
            float degrees = step * .5F;
            JsonObject sample = new JsonObject(); sample.addProperty("angle", degrees);
            JsonArray outputs = new JsonArray();
            for (int sky = 0; sky <= 15; ++sky) {
                float angle = degrees * (float)(Math.PI / 180.0);
                float offset = angle < (float)Math.PI ? 0.0F : (float)(Math.PI * 2);
                angle += (offset - angle) * .2F;
                outputs.add(Mth.clamp(Math.round(sky * Mth.cos(angle)), 0, 15));
            }
            sample.add("outputs", outputs); samples.add(sample);
        }
        JsonObject result = new JsonObject();
        result.addProperty("reference", "Java 26.2 Mth.cos table with DaylightDetectorBlock float arithmetic; numeric fixture, not a world-lighting simulation");
        result.add("samples", samples);
        Files.writeString(Path.of(args[0]), new Gson().toJson(result));
    }
}
