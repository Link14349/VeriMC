import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.SupportType;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Property;

/** Exports observable state data, not game implementation source. */
public class ExportReference {
    private static <T extends Comparable<T>> String propertyValue(BlockState state, Property<T> property) {
        return property.getName(state.getValue(property));
    }
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        JsonObject result = new JsonObject();
        result.addProperty("version", "26.2");
        JsonArray blocks = new JsonArray();
        for (Block block : BuiltInRegistries.BLOCK) {
            JsonObject blockInfo = new JsonObject();
            blockInfo.addProperty("name", BuiltInRegistries.BLOCK.getKey(block).toString());
            blockInfo.addProperty("className", block.getClass().getSimpleName());
            blockInfo.addProperty("defaultState", Block.getId(block.defaultBlockState()));
            JsonArray states = new JsonArray();
            for (var state : block.getStateDefinition().getPossibleStates()) {
                JsonObject stateInfo = new JsonObject();
                stateInfo.addProperty("id", Block.getId(state));
                JsonObject properties = new JsonObject();
                for (var property : state.getProperties()) {
                    properties.addProperty(property.getName(), propertyValue(state, property));
                }
                stateInfo.add("properties", properties);
                stateInfo.addProperty("conductor", state.isRedstoneConductor(EmptyBlockGetter.INSTANCE, BlockPos.ZERO));
                stateInfo.addProperty("fullCube", state.isCollisionShapeFullBlock(EmptyBlockGetter.INSTANCE, BlockPos.ZERO));
                stateInfo.addProperty("analogSource", state.hasAnalogOutputSignal());
                stateInfo.addProperty("blockEntity", state.hasBlockEntity());
                stateInfo.addProperty("replaceable", state.canBeReplaced());
                stateInfo.addProperty("signalSource", state.isSignalSource());
                stateInfo.addProperty("pushReaction", state.getPistonPushReaction().name());
                stateInfo.addProperty("destroySpeed", state.getDestroySpeed(EmptyBlockGetter.INSTANCE, BlockPos.ZERO));
                int supportMask = 0;
                int rigidMask = 0;
                int centerMask = 0;
                JsonArray weak = new JsonArray();
                JsonArray strong = new JsonArray();
                for (Direction direction : Direction.values()) {
                    if (state.isFaceSturdy(EmptyBlockGetter.INSTANCE, BlockPos.ZERO, direction)) supportMask |= 1 << direction.ordinal();
                    if (state.isFaceSturdy(EmptyBlockGetter.INSTANCE, BlockPos.ZERO, direction, SupportType.RIGID)) rigidMask |= 1 << direction.ordinal();
                    if (state.isFaceSturdy(EmptyBlockGetter.INSTANCE, BlockPos.ZERO, direction, SupportType.CENTER)) centerMask |= 1 << direction.ordinal();
                    weak.add(state.getSignal(EmptyBlockGetter.INSTANCE, BlockPos.ZERO, direction));
                    strong.add(state.getDirectSignal(EmptyBlockGetter.INSTANCE, BlockPos.ZERO, direction));
                }
                stateInfo.addProperty("supportMask", supportMask);
                stateInfo.addProperty("rigidMask", rigidMask);
                stateInfo.addProperty("centerMask", centerMask);
                stateInfo.add("weakSignal", weak);
                stateInfo.add("strongSignal", strong);
                states.add(stateInfo);
            }
            blockInfo.add("states", states);
            blocks.add(blockInfo);
        }
        result.add("blocks", blocks);
        Files.writeString(Path.of(args[0]), new GsonBuilder().create().toJson(result));
        System.out.println("Exported " + blocks.size() + " block definitions");
    }
}
