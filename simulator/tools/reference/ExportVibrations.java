import com.google.gson.*;
import java.nio.file.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.gameevent.vibrations.VibrationSystem;

/** Export registry observations from the fixed reference build. */
public class ExportVibrations {
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();Bootstrap.bootStrap();
        var result=new JsonObject();result.addProperty("version","26.2");var events=new JsonArray();
        BuiltInRegistries.GAME_EVENT.listElements().forEach(holder -> {
            var row=new JsonObject();row.addProperty("name",holder.key().identifier().toString());
            row.addProperty("radius",holder.value().notificationRadius());
            row.addProperty("frequency",VibrationSystem.getGameEventFrequency(holder));events.add(row);
        });
        result.add("events",events);Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(result));
    }
}
