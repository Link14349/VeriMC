import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.entity.SkullBlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.NoteBlockInstrument;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

/** Run the original note block event with an isolated world random stream. */
public class ExportNoteRandom extends TestFunctionLoader {
    static Path output;
    @Override public void load(BiConsumer<ResourceKey<Consumer<GameTestHelper>>,Consumer<GameTestHelper>> register) {
        register.accept(ResourceKey.create(Registries.TEST_FUNCTION,Identifier.parse("simulator:capture")),ExportNoteRandom::capture);
    }
    static void capture(GameTestHelper helper) {
        try {
            var level=helper.getLevel();var pos=helper.absolutePos(new BlockPos(4,3,4));
            var result=new JsonObject();result.addProperty("reference","Minecraft Java 26.2 NoteBlock.triggerEvent; seed reset immediately before each isolated call");
            var cases=new JsonArray();result.add("cases",cases);
            var method=NoteBlock.class.getDeclaredMethod("triggerEvent",BlockState.class,Level.class,BlockPos.class,int.class,int.class);method.setAccessible(true);
            var randomState=LegacyRandomSource.class.getDeclaredField("seed");randomState.setAccessible(true);
            var customField=SkullBlockEntity.class.getDeclaredField("noteBlockSound");customField.setAccessible(true);
            for(long seed:new long[]{0,1,-1,Long.MIN_VALUE})for(var instrument:NoteBlockInstrument.values())for(boolean custom:new boolean[]{false,true}) {
                if(custom && !instrument.hasCustomSound())continue;
                level.setBlock(pos.above(),Blocks.AIR.defaultBlockState(),18);
                var state=Blocks.NOTE_BLOCK.defaultBlockState().setValue(NoteBlock.INSTRUMENT,instrument).setValue(NoteBlock.NOTE,17);
                level.setBlock(pos,state,18);
                if(custom) {
                    level.setBlock(pos.above(),Blocks.PLAYER_HEAD.defaultBlockState(),18);
                    customField.set(level.getBlockEntity(pos.above()),Identifier.parse("simulator:test/bell"));
                }
                level.getRandom().setSeed(seed);
                boolean played=(boolean)method.invoke(Blocks.NOTE_BLOCK,state,level,pos,0,0);
                var row=new JsonObject();row.addProperty("seedBits",Long.toUnsignedString(seed,16));row.addProperty("instrument",instrument.getSerializedName());
                row.addProperty("custom",custom);row.addProperty("played",played);row.addProperty("randomState",((AtomicLong)randomState.get(level.getRandom())).get());cases.add(row);
            }
            Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");helper.succeed();
        }catch(Exception e){throw new RuntimeException(e);}
    }
    public static void main(String[] args)throws Exception {
        output=Path.of(args[0]);SharedConstants.tryDetectVersion();TestFunctionLoader.registerLoader(new ExportNoteRandom());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe",args[1],"--packs",args[2],"--tests","simulator:capture","--report",args[3]},path->{});
    }
}
