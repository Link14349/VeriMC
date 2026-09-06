import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.Registries;
import net.minecraft.gametest.framework.*;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.Container;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.LegacyRandomSource;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Isolate the original dropper call before entity motion or ambient RNG use. */
public class ExportDropperMotion extends TestFunctionLoader {
    static Path output;
    static JsonArray vector(Vec3 value) {
        var result = new JsonArray(); result.add(value.x); result.add(value.y); result.add(value.z); return result;
    }
    static JsonArray bits(Vec3 value) {
        var result = new JsonArray();
        for (double v : new double[]{value.x,value.y,value.z}) result.add(Long.toUnsignedString(Double.doubleToRawLongBits(v),16));
        return result;
    }
    @Override public void load(BiConsumer<ResourceKey<Consumer<GameTestHelper>>,Consumer<GameTestHelper>> register) {
        register.accept(ResourceKey.create(Registries.TEST_FUNCTION,Identifier.parse("simulator:capture")),ExportDropperMotion::capture);
    }
    static void capture(GameTestHelper helper) {
        try {
            var level = helper.getLevel(); var pos = helper.absolutePos(new BlockPos(4,3,4));
            var result = new JsonObject(); result.addProperty("reference","Minecraft Java 26.2 DropperBlock.dispenseFrom; RNG reset immediately before each isolated call; entity not ticked");
            var cases = new JsonArray(); result.add("cases",cases);
            var bounds = new AABB(pos).inflate(4);
            for (var p : BlockPos.betweenClosed(pos.offset(-3,-3,-3),pos.offset(3,3,3))) level.setBlock(p,Blocks.AIR.defaultBlockState(),18);
            var method = DropperBlock.class.getDeclaredMethod("dispenseFrom",ServerLevel.class,BlockState.class,BlockPos.class); method.setAccessible(true);
            var randomState = LegacyRandomSource.class.getDeclaredField("seed"); randomState.setAccessible(true);
            for (long seed : new long[]{0,1,-1,Long.MIN_VALUE,Long.MAX_VALUE,0x123456789abcdefL}) for (var direction : Direction.values()) {
                for (var item : level.getEntitiesOfClass(ItemEntity.class,bounds)) item.discard();
                var state = Blocks.DROPPER.defaultBlockState().setValue(DispenserBlock.FACING,direction);
                level.setBlock(pos,state,18);
                var container = (Container)level.getBlockEntity(pos); container.clearContent(); container.setItem(4,new ItemStack(Items.STONE,2));
                level.getRandom().setSeed(seed);
                method.invoke(Blocks.DROPPER,level,level.getBlockState(pos),pos);
                var entities = level.getEntitiesOfClass(ItemEntity.class,bounds);
                if (entities.size()!=1) throw new IllegalStateException("Expected exactly one drop");
                var item = entities.getFirst(); var row = new JsonObject(); cases.add(row);
                row.addProperty("seedBits",Long.toUnsignedString(seed,16)); row.addProperty("facing",direction.getName());
                var source = new JsonArray(); source.add(pos.getX());source.add(pos.getY());source.add(pos.getZ());row.add("source",source);
                row.add("position",vector(item.position())); row.add("positionBits",bits(item.position())); row.add("velocity",vector(item.getDeltaMovement()));row.add("velocityBits",bits(item.getDeltaMovement()));
                row.addProperty("randomState",((AtomicLong)randomState.get(level.getRandom())).get());
                row.addProperty("remaining",container.getItem(4).getCount());row.addProperty("count",item.getItem().getCount());
            }
            Files.writeString(output,new GsonBuilder().setPrettyPrinting().create().toJson(result)); helper.succeed();
        } catch(Exception e) { throw new RuntimeException(e); }
    }
    public static void main(String[] args) throws Exception {
        output=Path.of(args[0]);SharedConstants.tryDetectVersion();TestFunctionLoader.registerLoader(new ExportDropperMotion());
        GameTestMainUtil.runGameTestServer(new String[]{"--universe",args[1],"--packs",args[2],"--tests","simulator:capture","--report",args[3]},path->{});
    }
}
