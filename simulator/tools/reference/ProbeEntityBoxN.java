import java.util.List;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.phys.AABB;

/**
 * Probe (not a fixture): print the registered box of every container entity and enumerate the block
 * cells whose {@code getEntityContainer} query would report it when it is spawned at a cell centre.
 *
 * <p>{@code HopperBlockEntity.getEntityContainer(level, x, y, z)} tests the entity boxes against
 * {@code new AABB(x-0.5, y-0.5, z-0.5, x+0.5, y+0.5, z+0.5)}; for a cell {@code c} that box is
 * exactly {@code [c, c+1]} on every axis. The capture convention puts the entity's feet at the cell
 * centre, so this is a pure function of the registered width/height and can be evaluated without a
 * server. The result is the reach of one declared container entity in block cells, which is the
 * number the device-layer protocol boundary has to be written against.
 */
public class ProbeEntityBoxN {
    static final List<String> TYPES = List.of(
        "chest_minecart", "hopper_minecart", "oak_chest_boat", "bamboo_chest_raft", "bamboo_raft", "oak_boat");

    public static void main(String[] args) {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        for (String name : TYPES) {
            EntityType<?> type = BuiltInRegistries.ENTITY_TYPE.getValue(Identifier.withDefaultNamespace(name));
            // The declared cell is the origin; the entity is placed at its centre, feet on y+0.5.
            AABB box = type.getDimensions().makeBoundingBox(0.5, 0.5, 0.5);
            StringBuilder cells = new StringBuilder();
            int nx = 0, ny = 0, nz = 0, total = 0;
            for (int cx = -3; cx <= 3; ++cx)
                for (int cy = -3; cy <= 3; ++cy)
                    for (int cz = -3; cz <= 3; ++cz) {
                        AABB query = new AABB(cx, cy, cz, cx + 1.0, cy + 1.0, cz + 1.0);
                        if (!box.intersects(query)) continue;
                        ++total;
                        cells.append(' ').append(cx).append(',').append(cy).append(',').append(cz);
                    }
            for (int c = -3; c <= 3; ++c) {
                if (box.intersects(new AABB(c, 0, 0, c + 1.0, 1.0, 1.0))) ++nx;
                if (box.intersects(new AABB(0, c, 0, 1.0, c + 1.0, 1.0))) ++ny;
                if (box.intersects(new AABB(0, 0, c, 1.0, 1.0, c + 1.0))) ++nz;
            }
            System.out.printf("%s width=%.6f height=%.6f box=[%.6f,%.6f]x[%.6f,%.6f]x[%.6f,%.6f] cells=%dx%dx%d=%d%n",
                name, type.getWidth(), type.getHeight(), box.minX, box.maxX, box.minY, box.maxY, box.minZ, box.maxZ,
                nx, ny, nz, total);
            System.out.println("  reported cells (relative to the declared cell):" + cells);
        }
    }
}
