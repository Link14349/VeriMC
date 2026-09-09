import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.mojang.authlib.GameProfile;
import com.mojang.authlib.yggdrasil.ServicesKeySet;
import com.mojang.serialization.Lifecycle;
import java.net.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.HashSet;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import net.minecraft.SharedConstants;
import net.minecraft.SystemReport;
import net.minecraft.commands.Commands;
import net.minecraft.core.BlockPos;
import net.minecraft.core.MappedRegistry;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.Registries;
import net.minecraft.gizmos.GizmoCollector;
import net.minecraft.gizmos.Gizmos;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.Services;
import net.minecraft.server.WorldLoader;
import net.minecraft.server.WorldStem;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.progress.LoggingLevelLoadListener;
import net.minecraft.server.notifications.EmptyNotificationService;
import net.minecraft.server.notifications.NotificationManager;
import net.minecraft.server.packs.repository.PackRepository;
import net.minecraft.server.packs.repository.ServerPacksSource;
import net.minecraft.server.permissions.LevelBasedPermissionSet;
import net.minecraft.server.permissions.PermissionSet;
import net.minecraft.server.players.NameAndId;
import net.minecraft.server.players.PlayerList;
import net.minecraft.server.players.ProfileResolver;
import net.minecraft.server.players.UserNameToIdResolver;
import net.minecraft.util.Util;
import net.minecraft.util.datafix.DataFixers;
import net.minecraft.util.debugchart.LocalSampleLogger;
import net.minecraft.util.debugchart.SampleLogger;
import net.minecraft.world.flag.FeatureFlags;
import net.minecraft.world.level.DataPackConfig;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.dimension.LevelStem;
import net.minecraft.world.level.gamerules.GameRules;
import net.minecraft.world.level.levelgen.WorldDimensions;
import net.minecraft.world.level.levelgen.WorldGenSettings;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;
import net.minecraft.world.level.levelgen.structure.BoundingBox;
import net.minecraft.world.level.storage.LevelDataAndDimensions;
import net.minecraft.world.level.storage.LevelStorageSource;
import net.minecraft.world.level.storage.PrimaryLevelData;
import net.minecraft.world.level.storage.ServerLevelData;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.AABB;

/**
 * Replays the same timeline as {@link CaptureRedstone} on a server whose enabled feature set is
 * exactly {@code FeatureFlags.VANILLA_SET} and whose only datapack is {@code vanilla}.
 *
 * <p>{@code GameTestServer.ENABLED_FEATURES} is {@code private static final} and equals
 * {@code {vanilla, trade_rebalance}}, so the GameTest harness cannot be reduced to strict vanilla.
 * This tool therefore boots its own {@link MinecraftServer} instead. No game code is modified:
 * the world is built from the same public {@code WorldLoader} path GameTestServer uses, only with
 * a different {@link WorldDataConfiguration}.
 *
 * <p>To make the two harnesses comparable the region is prepared exactly like GameTest prepares a
 * test structure — the same 48x6x48 box cleared to air, the same barrier shell from
 * {@code TestInstanceBlockEntity.encaseStructure}, the same forced chunks — and the origin, game
 * time and day time are taken from the fixture instead of being drawn at random.
 */
public class CaptureRedstoneVanilla extends MinecraftServer {
    static final Services NO_SERVICES = new Services(
        null, ServicesKeySet.EMPTY, null, new MockUserNameToIdResolver(), new MockProfileResolver());
    static final WorldOptions WORLD_OPTIONS = new WorldOptions(0L, false, false);
    static final int SIZE_X = 48, SIZE_Y = 6, SIZE_Z = 48;
    static final int WARMUP_TICKS = 20;

    static BlockPos origin = BlockPos.ZERO;
    static long forcedGameTime = -1, forcedClockTicks = 0;
    static Map<Integer, Runnable> pending = new HashMap<>();
    static boolean prepared = false, finished = false;
    static int timelineTick = 0, warmup = -1;

    private final LocalSampleLogger sampleLogger = new LocalSampleLogger(4);

    public CaptureRedstoneVanilla(Thread thread, LevelStorageSource.LevelStorageAccess access,
                                  PackRepository packRepository, WorldStem stem) {
        super(thread, access, packRepository, stem, Optional.of(new GameRules(FeatureFlags.VANILLA_SET)),
            Proxy.NO_PROXY, DataFixers.getDataFixer(), NO_SERVICES,
            LoggingLevelLoadListener.forDedicatedServer(), false, new NotificationManager());
    }

    static CaptureRedstoneVanilla create(Thread thread, LevelStorageSource.LevelStorageAccess access, PackRepository packRepository) {
        packRepository.reload();
        // The whole point of this harness: vanilla flags only, and only the vanilla datapack.
        // GameTestServer instead enables every available pack and allFlags minus the two experiments.
        WorldDataConfiguration configuration =
            new WorldDataConfiguration(new DataPackConfig(List.of("vanilla"), List.of()), FeatureFlags.VANILLA_SET);
        LevelSettings settings = new LevelSettings("Vanilla Reference Level", GameType.CREATIVE,
            LevelSettings.DifficultySettings.DEFAULT, true, configuration);
        WorldLoader.PackConfig packConfig = new WorldLoader.PackConfig(packRepository, configuration, false, true);
        WorldLoader.InitConfig initConfig =
            new WorldLoader.InitConfig(packConfig, Commands.CommandSelection.DEDICATED, LevelBasedPermissionSet.OWNER);
        try {
            WorldStem stem = Util.<WorldStem>blockUntilDone(executor -> WorldLoader.load(
                initConfig,
                context -> {
                    Registry<LevelStem> noDatapackDimensions = new MappedRegistry<>(Registries.LEVEL_STEM, Lifecycle.stable()).freeze();
                    WorldDimensions worldDimensions = context.datapackWorldgen()
                        .lookupOrThrow(Registries.WORLD_PRESET).getOrThrow(WorldPresets.FLAT).value().createWorldDimensions();
                    WorldDimensions.Complete dimensions = worldDimensions.bake(noDatapackDimensions);
                    PrimaryLevelData levelData = new PrimaryLevelData(settings, dimensions.specialWorldProperty(), dimensions.lifecycle());
                    return new WorldLoader.DataLoadOutput<>(
                        new LevelDataAndDimensions.WorldDataAndGenSettings(levelData, new WorldGenSettings(WORLD_OPTIONS, worldDimensions)),
                        dimensions.dimensionsRegistryAccess());
                },
                WorldStem::new, Util.backgroundExecutor(), executor)).get();
            return new CaptureRedstoneVanilla(thread, access, packRepository, stem);
        } catch (Exception e) {
            throw new RuntimeException("Failed to load the vanilla-only world", e);
        }
    }

    @Override protected boolean initServer() {
        this.setPlayerList(new PlayerList(this, this.registries(), this.playerDataStorage, new EmptyNotificationService()) {});
        Gizmos.withCollector(GizmoCollector.NOOP);
        this.loadLevel();
        return true;
    }

    /** Rebuilds the state GameTest hands a test function: cleared box, barrier shell, forced chunks. */
    static void prepareRegion(ServerLevel level) {
        BoundingBox box = BoundingBox.fromCorners(origin, origin.offset(SIZE_X - 1, SIZE_Y - 1, SIZE_Z - 1));
        // TestInstanceBlockEntity.forceLoadChunks
        for (int cx = box.minX() >> 4; cx <= box.maxX() >> 4; ++cx)
            for (int cz = box.minZ() >> 4; cz <= box.maxZ() >> 4; ++cz) { level.getChunk(cx, cz); level.setChunkForced(cx, cz, true); }
        // StructureUtils.clearSpaceForStructure: everything inside the box sits above ground height, so it is all air.
        BlockPos.betweenClosedStream(box).forEach(pos -> {
            level.setBlock(pos.immutable(), Blocks.AIR.defaultBlockState(), 818);
            level.updateNeighborsAt(pos.immutable(), Blocks.AIR);
        });
        level.getBlockTicks().clearArea(box);
        level.clearBlockEvents(box);
        for (Entity entity : level.getEntitiesOfClass(Entity.class, AABB.of(box), e -> !(e instanceof Player))) entity.discard();
        // TestInstanceBlockEntity.encaseStructure with sky_access = false, which is the default our
        // fixture pack uses, so the shell has a floor, four walls and a ceiling.
        BlockPos low = origin.offset(-1, -1, -1), high = origin.offset(SIZE_X, SIZE_Y, SIZE_Z);
        BlockPos.betweenClosedStream(low, high).forEach(pos -> {
            boolean edge = pos.getX() == low.getX() || pos.getX() == high.getX()
                || pos.getZ() == low.getZ() || pos.getZ() == high.getZ() || pos.getY() == low.getY();
            if (edge || pos.getY() == high.getY()) level.setBlockAndUpdate(pos.immutable(), Blocks.BARRIER.defaultBlockState());
        });
    }

    /**
     * GameTest draws its origin and clock from the running world; here both come from the fixture.
     * Game time drives the 20 gt daylight detector poll, the world clock drives the sun angle, and
     * both are set to the values the GameTest capture recorded at its own first timeline tick.
     */
    static void alignClocks(ServerLevel level) {
        if (forcedGameTime < 0) return;
        try {
            var field = ServerLevel.class.getDeclaredField("serverLevelData");
            field.setAccessible(true);
            ((ServerLevelData)field.get(level)).setGameTime(forcedGameTime);
        } catch (ReflectiveOperationException e) { throw new RuntimeException(e); }
        level.dimensionType().defaultClock().ifPresent(clock -> level.clockManager().setTotalTicks(clock, forcedClockTicks));
    }

    @Override protected void tickServer(BooleanSupplier haveTime) {
        super.tickServer(haveTime);
        if (finished) { this.halt(false); return; }
        ServerLevel level = this.overworld();
        if (!prepared) {
            // GameTest places its structure several ticks before the test function runs, so the
            // forced-chunk tickets have already propagated and the region is in the block ticking
            // range. Without the same warm-up our first scheduled ticks would silently not fire.
            if (warmup < 0) { prepareRegion(level); warmup = WARMUP_TICKS; return; }
            if (warmup-- > 0) return;
            alignClocks(level);
            CaptureRedstone.runTimeline(level, origin,
                "Minecraft Java 26.2 strict vanilla-only dedicated server, nonexperimental redstone",
                (tick, action) -> pending.put(tick, action), () -> finished = true);
            prepared = true;
            timelineTick = 0;
            Runnable first = pending.remove(0);
            if (first != null) first.run();
            return;
        }
        Runnable action = pending.remove(++timelineTick);
        if (action != null) action.run();
    }

    @Override protected void waitUntilNextTick() { this.runAllTasks(); }
    @Override protected SampleLogger getTickTimeLogger() { return this.sampleLogger; }
    @Override public boolean isTickTimeLoggingEnabled() { return false; }
    @Override public SystemReport fillServerSystemReport(SystemReport report) { report.setDetail("Type", "Vanilla reference server"); return report; }
    @Override protected void onServerExit() { super.onServerExit(); System.exit(finished ? 0 : 1); }
    @Override public boolean isHardcore() { return false; }
    @Override public LevelBasedPermissionSet operatorUserPermissions() { return LevelBasedPermissionSet.ALL; }
    @Override public PermissionSet getFunctionCompilationPermissions() { return LevelBasedPermissionSet.OWNER; }
    @Override public boolean shouldRconBroadcast() { return false; }
    @Override public boolean isDedicatedServer() { return true; }
    @Override public int getRateLimitPacketsPerSecond() { return 0; }
    @Override public int getCommandSpamThresholdSeconds() { return 0; }
    @Override public int getChatSpamThresholdSeconds() { return 0; }
    @Override public boolean useNativeTransport() { return false; }
    @Override public boolean isPublished() { return false; }
    @Override public boolean shouldInformAdmins() { return false; }
    @Override public boolean isSingleplayerOwner(NameAndId nameAndId) { return false; }
    @Override public int getMaxPlayers() { return 1; }

    static class MockProfileResolver implements ProfileResolver {
        @Override public Optional<GameProfile> fetchByName(String name) { return Optional.empty(); }
        @Override public Optional<GameProfile> fetchById(UUID id) { return Optional.empty(); }
    }

    static class MockUserNameToIdResolver implements UserNameToIdResolver {
        private final Set<NameAndId> savedIds = new HashSet<>();
        @Override public void add(NameAndId nameAndId) { this.savedIds.add(nameAndId); }
        @Override public Optional<NameAndId> get(String name) {
            return this.savedIds.stream().filter(e -> e.name().equals(name)).findFirst().or(() -> Optional.of(NameAndId.createOffline(name)));
        }
        @Override public Optional<NameAndId> get(UUID id) { return this.savedIds.stream().filter(e -> e.id().equals(id)).findFirst(); }
        @Override public void resolveOfflineUsers(boolean value) {}
        @Override public void save() {}
    }

    /** args: scenario output universe originX originY originZ [gameTime dayTime] */
    public static void main(String[] args) throws Exception {
        JsonObject scenario = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        CaptureRedstone.scenario = scenario;
        CaptureRedstone.output = Path.of(args[1]);
        origin = new BlockPos(Integer.parseInt(args[3]), Integer.parseInt(args[4]), Integer.parseInt(args[5]));
        if (args.length > 7) { forcedGameTime = Long.parseLong(args[6]); forcedClockTicks = Long.parseLong(args[7]); }
        Path universe = Path.of(args[2]);
        if (Files.exists(universe)) org.apache.commons.io.FileUtils.deleteDirectory(universe.toFile());
        Files.createDirectories(universe);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Util.startTimerHackThread();
        LevelStorageSource.LevelStorageAccess access = LevelStorageSource.createDefault(universe).createAccess("vanillaworld");
        PackRepository packRepository = ServerPacksSource.createPackRepository(access);
        MinecraftServer.spin(thread -> create(thread, access, packRepository));
    }
}
