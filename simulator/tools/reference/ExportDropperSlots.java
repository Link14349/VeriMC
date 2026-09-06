import com.google.gson.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.SharedConstants;
import net.minecraft.server.Bootstrap;
import net.minecraft.core.BlockPos;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.entity.DropperBlockEntity;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

/** Original nine-slot selection, isolated from unrelated world random calls. */
public class ExportDropperSlots {
    static class CountedRandom extends LegacyRandomSource {
        long draws;
        CountedRandom(long seed) { super(seed); }
        @Override public int next(int bits) { ++draws; return super.next(bits); }
    }
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion(); Bootstrap.bootStrap();
        // This isolated fixture needs only nonempty one-stone stacks. Registry
        // component loading normally happens later in server data-pack reload.
        Items.STONE.builtInRegistryHolder().bindComponents(net.minecraft.core.component.DataComponentMap.builder()
            .set(net.minecraft.core.component.DataComponents.MAX_STACK_SIZE, 64).build());
        var result = new JsonObject(); result.addProperty("reference", "Minecraft Java 26.2 DispenserBlockEntity.getRandomSlot used by DropperBlockEntity; isolated RNG stream");
        var cases = new JsonArray(); result.add("cases", cases);
        var field = LegacyRandomSource.class.getDeclaredField("seed"); field.setAccessible(true);
        for (long seed : new long[]{0, 1, -1, Long.MIN_VALUE, Long.MAX_VALUE, 0x123456789abcdefL}) {
            var test = new JsonObject(); cases.add(test); test.addProperty("seedBits", Long.toUnsignedString(seed,16));
            var random = new CountedRandom(seed); var calls = new JsonArray(); test.add("calls", calls);
            var dropper = new DropperBlockEntity(BlockPos.ZERO, Blocks.DROPPER.defaultBlockState());
            for (int i = 0; i < 100; ++i) {
                int mask = i == 0 ? 0 : i == 1 ? 511 : (i * 137 + (i >> 1)) & 511;
                for (int slot = 0; slot < 9; ++slot) dropper.setItem(slot, (mask & (1 << slot)) == 0 ? ItemStack.EMPTY : new ItemStack(Items.STONE));
                var row = new JsonObject(); calls.add(row); row.addProperty("mask", mask);
                row.addProperty("slot", dropper.getRandomSlot(random)); row.addProperty("state", ((AtomicLong)field.get(random)).get()); row.addProperty("draws", random.draws);
            }
        }
        Files.writeString(Path.of(args[0]), new Gson().toJson(result));
    }
}
