import com.google.gson.*;
import java.nio.file.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.block.NoteBlock;
import net.minecraft.world.level.block.state.properties.NoteBlockInstrument;

/** Fixed-version instrument properties and observable pitch values. */
public class ExportNotes {
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();Bootstrap.bootStrap();
        var result=new JsonObject();result.addProperty("version","26.2");
        var instruments=new JsonArray();
        for(var instrument:NoteBlockInstrument.values()) {
            var row=new JsonObject();row.addProperty("name",instrument.getSerializedName());
            row.addProperty("tunable",instrument.isTunable());row.addProperty("above",instrument.worksAboveNoteBlock());
            row.addProperty("custom",instrument.hasCustomSound());row.addProperty("sound",instrument.getSoundEvent().value().location().toString());
            instruments.add(row);
        }
        result.add("instruments",instruments);
        var blocks=new JsonObject();
        for(var block:BuiltInRegistries.BLOCK) blocks.addProperty(BuiltInRegistries.BLOCK.getKey(block).toString(),block.defaultBlockState().instrument().getSerializedName());
        result.add("blocks",blocks);
        var pitches=new JsonArray();for(int note=0;note<=24;++note)pitches.add(NoteBlock.getPitchFromNote(note));result.add("pitches",pitches);
        Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");
    }
}
