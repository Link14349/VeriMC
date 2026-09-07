import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.component.DataComponents;
import net.minecraft.core.registries.*;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.*;
import net.minecraft.world.item.*;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.entity.JukeboxBlockEntity;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

/** Tick the original player without interleaving ambient world random calls. */
public class ExportJukeboxPlayback extends TestFunctionLoader {
    static Path output;
    @Override public void load(BiConsumer<ResourceKey<Consumer<GameTestHelper>>,Consumer<GameTestHelper>> register) {
        register.accept(ResourceKey.create(Registries.TEST_FUNCTION,Identifier.parse("simulator:capture")),ExportJukeboxPlayback::capture);
    }
    static void capture(GameTestHelper helper) {
        try {
            var level=helper.getLevel();var pos=helper.absolutePos(new BlockPos(4,3,4));
            for(var p:BlockPos.betweenClosed(pos.offset(-3,-3,-3),pos.offset(3,3,3)))level.setBlock(p,Blocks.AIR.defaultBlockState(),18);
            var result=new JsonObject();result.addProperty("reference","Minecraft Java 26.2 JukeboxBlockEntity.tick; isolated full song playback and world RNG");
            var cases=new JsonArray();result.add("cases",cases);var field=LegacyRandomSource.class.getDeclaredField("seed");field.setAccessible(true);
            for(var item:BuiltInRegistries.ITEM) {
                var stack=new ItemStack(item);if(!stack.has(DataComponents.JUKEBOX_PLAYABLE))continue;
                level.setBlock(pos,Blocks.AIR.defaultBlockState(),18);level.setBlock(pos,Blocks.JUKEBOX.defaultBlockState(),18);
                var box=(JukeboxBlockEntity)level.getBlockEntity(pos);box.setTheItem(stack);
                var song=JukeboxSong.fromStack(stack).orElseThrow().value();var row=new JsonObject();cases.add(row);
                row.addProperty("item",BuiltInRegistries.ITEM.getKey(item).toString());row.addProperty("lengthTicks",song.lengthInTicks());row.addProperty("analog",box.getComparatorOutput());
                var samples=new JsonArray();row.add("samples",samples);level.getRandom().setSeed(17);
                for(int ticks=0;ticks<=song.lengthInTicks()+22;++ticks) {
                    if(ticks>0)JukeboxBlockEntity.tick(level,pos,level.getBlockState(pos),box);
                    if(ticks<=2 || ticks%20==0 || ticks>=song.lengthInTicks()+18) {
                        var sample=new JsonObject();sample.addProperty("ticks",ticks);sample.addProperty("playing",box.getSongPlayer().isPlaying());
                        sample.addProperty("elapsed",box.getSongPlayer().getTicksSinceSongStarted());sample.addProperty("randomState",((AtomicLong)field.get(level.getRandom())).get());samples.add(sample);
                    }
                }
            }
            Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");helper.succeed();
        }catch(Exception e){throw new RuntimeException(e);}
    }
    public static void main(String[] args)throws Exception {
        output=Path.of(args[0]);SharedConstants.tryDetectVersion();TestFunctionLoader.registerLoader(new ExportJukeboxPlayback());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe",args[1],"--packs",args[2],"--tests","simulator:capture","--report",args[3]},path->{});
    }
}
