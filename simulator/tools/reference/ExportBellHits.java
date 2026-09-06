import com.google.gson.*;
import java.nio.file.*;
import net.minecraft.SharedConstants;
import net.minecraft.core.Direction;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BellAttachType;

/** Directly observe the original hit filter at its floating point boundary. */
public class ExportBellHits {
    public static void main(String[] args)throws Exception {
        SharedConstants.tryDetectVersion();Bootstrap.bootStrap();
        var method=BellBlock.class.getDeclaredMethod("isProperHit",BlockState.class,Direction.class,double.class);method.setAccessible(true);
        var result=new JsonObject();result.addProperty("reference","Minecraft Java 26.2 BellBlock.isProperHit with world-coordinate addition and subtraction");var cases=new JsonArray();result.add("cases",cases);
        double bound=(double).8124F;
        for(var attachment:BellAttachType.values())for(var facing:Direction.Plane.HORIZONTAL)for(var face:Direction.values())for(int y:new int[]{-100,0,64,29999980})for(double height:new double[]{0,.5,Math.nextDown(bound),bound,Math.nextUp(bound),.9,1}) {
            var state=Blocks.BELL.defaultBlockState().setValue(BellBlock.ATTACHMENT,attachment).setValue(BellBlock.FACING,facing);
            var row=new JsonObject();row.addProperty("attachment",attachment.getSerializedName());row.addProperty("facing",facing.getName());row.addProperty("face",face.getName());
            row.addProperty("y",y);row.addProperty("height",height);row.addProperty("ring",(boolean)method.invoke(Blocks.BELL,state,face,(y+height)-y));cases.add(row);
        }
        Files.writeString(Path.of(args[0]),new Gson().toJson(result)+"\n");
    }
}
