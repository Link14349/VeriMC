import com.google.gson.*;
import java.nio.file.*;
import java.util.*;
import net.minecraft.SharedConstants;
import net.minecraft.server.Bootstrap;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.block.TargetBlock;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

/** Call the pinned original hit-strength function around rounding boundaries. */
public class ExportTarget {
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion(); Bootstrap.bootStrap();
        var method=TargetBlock.class.getDeclaredMethod("getRedstoneStrength",BlockHitResult.class,Vec3.class);method.setAccessible(true);
        var result=new JsonObject();result.addProperty("reference","Minecraft Java 26.2 TargetBlock.getRedstoneStrength; world-coordinate fractions and boundary neighbors");
        var cases=new JsonArray();result.add("cases",cases);
        var samples=new ArrayList<Double>(List.of(0.0,1.0,.5,.1,.9));
        for(int i=1;i<15;++i)for(double point:new double[]{i/30.0,1-i/30.0}) {
            samples.add(Math.nextDown(point));samples.add(point);samples.add(Math.nextUp(point));
        }
        for(var pos:List.of(new BlockPos(0,0,0),new BlockPos(-19,-4,17),new BlockPos(29999900,64,-29999900),new BlockPos(-29999900,-32,29999900)))for(var face:Direction.values()) {
            var row=new JsonObject();cases.add(row);var source=new JsonArray();source.add(pos.getX());source.add(pos.getY());source.add(pos.getZ());row.add("source",source);row.addProperty("face",face.getName());
            var outputs=new JsonArray();row.add("samples",outputs);
            int normal=face.getAxis()==Direction.Axis.Y?1:face.getAxis()==Direction.Axis.Z?2:0;
            for(double point:samples)for(int offset=1;offset<=2;++offset) {
                double[] hit={.5,.5,.5};hit[normal]=face.getAxisDirection()==Direction.AxisDirection.POSITIVE?1:0;hit[(normal+offset)%3]=point;
                var location=new Vec3(pos.getX()+hit[0],pos.getY()+hit[1],pos.getZ()+hit[2]);
                var entry=new JsonObject();var coordinates=new JsonArray();for(double value:hit)coordinates.add(value);entry.add("hit",coordinates);
                entry.addProperty("power",(int)method.invoke(null,new BlockHitResult(location,face,pos,false),location));outputs.add(entry);
            }
        }
        Files.writeString(Path.of(args[0]),new Gson().toJson(result));
    }
}
