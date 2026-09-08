import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockBehaviour;

/** Records which redstone-relevant callbacks each 26.2 block actually overrides. */
public class ExportBlockCapabilities {
    // Only callbacks that can drive or observe redstone; shape/collision overrides are excluded
    // because almost every connecting block has them and they carry no signal behaviour.
    private static final List<String> CALLBACKS = List.of(
        "neighborChanged", "tick", "triggerEvent", "affectNeighborsAfterRemoval", "onPlace",
        "isSignalSource", "getSignal", "getDirectSignal", "ownSignal",
        "hasAnalogOutputSignal", "getAnalogOutputSignal", "getTicker", "newBlockEntity");

    private static Set<String> overriddenCallbacks(final Block block) {
        Set<String> found = new LinkedHashSet<>();
        for (Class<?> type = block.getClass(); type != null && type != BlockBehaviour.class; type = type.getSuperclass()) {
            for (Method method : type.getDeclaredMethods()) {
                if (CALLBACKS.contains(method.getName())) found.add(method.getName());
            }
        }
        return found;
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        JsonObject result = new JsonObject();
        result.addProperty("version", "26.2");
        result.add("callbacks", new GsonBuilder().create().toJsonTree(CALLBACKS));
        JsonArray blocks = new JsonArray();
        for (Block block : BuiltInRegistries.BLOCK) {
            Set<String> callbacks = overriddenCallbacks(block);
            var defaultState = block.defaultBlockState();
            boolean analog = defaultState.hasAnalogOutputSignal();
            boolean signal = false;
            for (var state : block.getStateDefinition().getPossibleStates()) {
                if (state.isSignalSource()) { signal = true; break; }
            }
            boolean redstone = analog || signal || callbacks.contains("neighborChanged") || callbacks.contains("tick")
                || callbacks.contains("triggerEvent") || callbacks.contains("affectNeighborsAfterRemoval")
                || callbacks.contains("getSignal") || callbacks.contains("getDirectSignal") || callbacks.contains("ownSignal");
            if (!redstone) continue;
            JsonObject row = new JsonObject();
            row.addProperty("name", BuiltInRegistries.BLOCK.getKey(block).toString());
            row.addProperty("className", block.getClass().getSimpleName());
            row.addProperty("analogSource", analog);
            row.addProperty("signalSource", signal);
            row.addProperty("blockEntity", defaultState.hasBlockEntity());
            JsonArray names = new JsonArray();
            callbacks.stream().sorted().forEach(names::add);
            row.add("callbacks", names);
            blocks.add(row);
        }
        result.add("blocks", blocks);
        Files.writeString(Path.of(args[0]), new GsonBuilder().create().toJson(result));
        System.out.println("Exported " + blocks.size() + " redstone-relevant blocks of " + BuiltInRegistries.BLOCK.size());
    }
}
