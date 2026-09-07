import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.*;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.*;
import net.minecraft.world.item.*;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

/** Fixed original compost probabilities and isolated insertion observations. */
public class ExportComposting extends TestFunctionLoader {
    static Path output,rules;
    @Override public void load(BiConsumer<ResourceKey<Consumer<GameTestHelper>>,Consumer<GameTestHelper>> register) {
        register.accept(ResourceKey.create(Registries.TEST_FUNCTION,Identifier.parse("simulator:capture")),ExportComposting::capture);
    }
    static void capture(GameTestHelper helper) {
        try {
            var level=helper.getLevel();var pos=helper.absolutePos(new BlockPos(4,3,4));
            for(var p:BlockPos.betweenClosed(pos.offset(-3,-3,-3),pos.offset(3,2,3)))level.setBlock(p,Blocks.AIR.defaultBlockState(),18);
            var data=new JsonObject();data.addProperty("version","26.2");data.addProperty("reference","Minecraft Java 26.2 ComposterBlock.COMPOSTABLES");var probabilities=new JsonObject();data.add("items",probabilities);
            var fixture=new JsonObject();fixture.addProperty("reference","Minecraft Java 26.2 isolated ComposterBlock.insertItem; fixed world random seed per input");var cases=new JsonArray();fixture.add("cases",cases);
            var field=LegacyRandomSource.class.getDeclaredField("seed");field.setAccessible(true);
            for(var item:BuiltInRegistries.ITEM) {
                if(!ComposterBlock.COMPOSTABLES.containsKey(item) && item!=Items.STONE)continue;
                var itemId=BuiltInRegistries.ITEM.getKey(item).toString();
                if(item!=Items.STONE)probabilities.addProperty(itemId,ComposterBlock.COMPOSTABLES.getFloat(item));
                for(int fill=0;fill<=8;++fill)for(long seed:new long[]{0,1,-1,17}) {
                    var state=Blocks.COMPOSTER.defaultBlockState().setValue(ComposterBlock.LEVEL,fill);level.setBlock(pos,state,2);
                    var stack=new ItemStack(item);level.getRandom().setSeed(seed);
                    ComposterBlock.insertItem(null,state,level,stack,pos);
                    var row=new JsonObject();row.addProperty("item",itemId);row.addProperty("before",fill);row.addProperty("seed",Long.toUnsignedString(seed));
                    row.addProperty("after",level.getBlockState(pos).getValue(ComposterBlock.LEVEL));row.addProperty("count",stack.getCount());row.addProperty("randomState",((AtomicLong)field.get(level.getRandom())).get());cases.add(row);
                }
            }
            var gson=new GsonBuilder().setPrettyPrinting().create();Files.writeString(rules,gson.toJson(data)+"\n");Files.writeString(output,gson.toJson(fixture)+"\n");helper.succeed();
        }catch(Exception e){throw new RuntimeException(e);}
    }
    public static void main(String[] args)throws Exception {
        output=Path.of(args[0]);rules=Path.of(args[4]);SharedConstants.tryDetectVersion();TestFunctionLoader.registerLoader(new ExportComposting());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe",args[1],"--packs",args[2],"--tests","simulator:capture","--report",args[3]},path->{});
    }
}
