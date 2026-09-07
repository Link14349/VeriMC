#include "server.hpp"
#include "fileJobs.hpp"
#include "simulator/vmcbEncoding.hpp"
#include "simulator/simulator.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <random>
#include <sstream>
#include <unordered_set>

namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using Tcp = asio::ip::tcp;
using namespace simulator;
using Clock = std::chrono::steady_clock;
using simulatorServer::FileJob;
class Client;
struct Hub {
    asio::io_context& io;
    BlockRegistry registry;
    Simulator sim{registry};
    asio::steady_timer timer;
    std::vector<std::weak_ptr<Client>> clients;
    struct HistoryEntry { std::string name; std::unique_ptr<Simulator> state; BlockPos origin{}; };
    std::deque<HistoryEntry> undo, redo;
    std::unique_ptr<Simulator> runStart;
    bool running{};
    double speed{20};
    double fractionalTicks{};
    std::uint32_t frameId{};
    std::uint64_t traceEpoch{};
    bool pendingFull{};
    std::string token, host;
    std::string projectName{"未命名电路"};
    BlockPos projectOrigin{};
    std::map<std::string, std::shared_ptr<FileJob>> fileJobs;
    std::shared_ptr<FileJob> activeFileJob;
    asio::thread_pool fileWorkers{1};
    ~Hub() { for (auto& [id, job] : fileJobs) { (void)id; job->cancelled = true; } fileWorkers.join(); }
    std::shared_ptr<FileJob> beginFileJob(const std::string& operation);
    void exportFile(const std::shared_ptr<FileJob>& job, bool checkpoint, bool json);
    void importFile(const std::shared_ptr<FileJob>& job);
    void endFileJob(const std::shared_ptr<FileJob>& job, const std::string& error = {});
    void trimHistory();
    Clock::time_point lastPump{Clock::now()}, lastPublish{Clock::now()}, lastFileCleanup{Clock::now()};
    double eventsPerSecond{};
    std::uint64_t measuredEvents{};
    Clock::time_point measuredTime{Clock::now()};
    explicit Hub(asio::io_context& context, unsigned short port) : io(context), timer(context), host("127.0.0.1:" + std::to_string(port)) {
        std::random_device random; std::ostringstream value; value << std::hex; for (int i = 0; i < 8; ++i) value << random(); token = value.str();
    }
    Json status() const {
        Json probes = Json::array(); for (const auto& p : sim.getProbes()) probes.push_back({{"id", p.id}, {"pos", p.pos}, {"name", p.name}, {"mode", p.mode}, {"value", p.lastValue}, {"trigger", p.trigger}});
        return {{"type", "status"}, {"tick", sim.currentTick}, {"running", running}, {"speed", speed}, {"eventsPerSecond", running ? eventsPerSecond : 0}, {"blocks", sim.world.size()}, {"pending", sim.pendingEvents()}, {"updates", sim.statistics.updates}, {"events", sim.statistics.scheduledEvents}, {"storageBytes", sim.world.storageBytes()}, {"traceDropped", sim.traceDropped}, {"pauseReason", sim.pauseReason}, {"pendingActions", sim.pendingActionsJson()}, {"actionsDropped", sim.actionHistoryDropped()}, {"revision", sim.revision}, {"probes", probes}, {"canUndo", !undo.empty()}, {"canRedo", !redo.empty()}, {"name", projectName}};
    }
    void remember() { undo.push_back({projectName, sim.clone(), projectOrigin}); std::size_t bytes = 0; for (const auto& entry : undo) bytes += entry.state->estimatedBytes(); while (undo.size() > 1 && (undo.size() > 32 || bytes > 128u * 1024u * 1024u)) { bytes -= undo.front().state->estimatedBytes(); undo.pop_front(); } redo.clear(); runStart.reset(); }
    void demo(const std::string& kind = "basic");
    void start();
    void publish(bool full = false);
    void protectTrace();
    void command(const std::shared_ptr<Client>& client, const Json& message);
};
class Client : public std::enable_shared_from_this<Client> {
    ws::stream<Tcp::socket> socket;
    beast::flat_buffer readBuffer;
    Hub& hub;
    struct Message { bool binary; std::string data; };
    std::deque<Message> outgoing;
    std::size_t queuedBytes{};
public:
    bool alive{true}, awaitingAck{};
    std::uint32_t outstandingFrame{};
    Clock::time_point sentAt{Clock::now()};
    std::uint64_t traceCursor{};
    std::uint64_t traceAcknowledged{}, outstandingTraceEnd{}, traceEpoch{};
    std::unordered_set<StateId> knownStates;
    Client(Tcp::socket transport, Hub& owner) : socket(std::move(transport)), hub(owner) {}
    void accept(http::request<http::string_body> request) {
        socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::server)); socket.read_message_max(64 * 1024 * 1024);
        socket.async_accept(request, [self = shared_from_this()](beast::error_code ec) {
            if (ec) { self->alive = false; return; }
            self->hub.clients.push_back(self); self->sendJson({{"type", "ready"}, {"version", "26.2"}, {"protocolVersion", 3}, {"catalog", self->hub.registry.catalog()}, {"items", self->hub.registry.itemCatalog()}, {"gameEvents",self->hub.registry.gameEventCatalog()}});
            self->frame(self->hub.sim.world.cells(), true, ++self->hub.frameId); self->hub.protectTrace(); self->sendJson(self->hub.status()); self->read();
        });
    }
    void sendJson(const Json& value) { send(value.dump(), false); }
    void send(std::string data, bool binary) {
        if (!alive) return;
        if (queuedBytes + data.size() > 128u * 1024u * 1024u) { alive = false; beast::error_code ec; socket.next_layer().close(ec); return; }
        queuedBytes += data.size(); bool idle = outgoing.empty(); outgoing.push_back({binary, std::move(data)}); if (idle) write();
    }
    void write() {
        socket.binary(outgoing.front().binary);
        socket.async_write(asio::buffer(outgoing.front().data), [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) { self->alive = false; return; }
            self->queuedBytes -= self->outgoing.front().data.size(); self->outgoing.pop_front(); if (!self->outgoing.empty()) self->write();
        });
    }
    void read() {
        socket.async_read(readBuffer, [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) { self->alive = false; return; }
            int requestId = 0;
            try { auto message = Json::parse(beast::buffers_to_string(self->readBuffer.data())); requestId = message.value("requestId", 0); self->hub.command(self, message); }
            catch (const std::exception& error) { self->hub.running = false; self->sendJson({{"type", "error"}, {"requestId", requestId}, {"message", error.what()}}); }
            self->readBuffer.consume(self->readBuffer.size()); if (self->alive) self->read();
        });
    }
    void frame(const std::vector<Cell>& cells, bool full, std::uint32_t id) {
        const auto& trace = hub.sim.getTrace(); const auto first = hub.sim.traceDropped;
        if (full) { traceCursor = first; traceAcknowledged = first; traceEpoch = hub.traceEpoch; }
        if (traceCursor < first || traceCursor > first + trace.size()) throw std::logic_error("探针发送游标越界，不能跳过未送达的边沿");
        Json definitions = Json::array(); for (const auto& cell : cells) { if (knownStates.insert(cell.state).second) definitions.push_back(hub.registry.describe(cell.state)); if (auto motion = hub.sim.motionAt(cell.pos); motion && knownStates.insert(motion->movedState).second) definitions.push_back(hub.registry.describe(motion->movedState)); }
        if (!definitions.empty()) sendJson({{"type", "states"}, {"states", definitions}});
        std::string binary; binary.reserve(32 + cells.size() * 28);
        auto u32 = [&](std::uint32_t value) { for (int byte = 0; byte < 4; ++byte) binary.push_back(static_cast<char>((value >> (byte * 8)) & 255u)); };
        auto u64 = [&](std::uint64_t value) { u32(static_cast<std::uint32_t>(value)); u32(static_cast<std::uint32_t>(value >> 32)); };
        u32(0x32434d56); u32(full ? 1 : 2); u32(id); u32(static_cast<std::uint32_t>(cells.size())); u64(hub.sim.currentTick); u64(hub.sim.revision);
        for (const auto& cell : cells) {
            u32(static_cast<std::uint32_t>(cell.pos.x)); u32(static_cast<std::uint32_t>(cell.pos.y)); u32(static_cast<std::uint32_t>(cell.pos.z));
            u32(cell.state); u32(static_cast<std::uint32_t>(hub.sim.displayValue(cell.pos)));
            auto motion = hub.sim.motionAt(cell.pos); u32(motion ? motion->movedState : cell.state);
            auto flags = (hub.sim.viewerCount(cell.pos) > 0 ? 1024u : 0u) | (hub.sim.cartCount(cell.pos) > 0 ? 2048u : 0u);
            if(hub.registry[cell.state].device==Device::bell && hub.sim.bellRinging(cell.pos))flags|=4096u;
            if (motion) flags |= 1u | (motion->extending ? 2u : 0u) | (motion->source ? 4u : 0u) | (static_cast<unsigned>(motion->facing) << 3) | (motion->progress << 6) | (motion->previousProgress << 8);
            u32(flags);
        }
        awaitingAck = true; outstandingFrame = id; sentAt = Clock::now(); send(std::move(binary), true);
        Json edges = Json::array();
        const auto from = traceCursor;
        for (std::uint64_t i = traceCursor - first; i < trace.size(); ++i) { const auto& e = trace[static_cast<std::size_t>(i)]; edges.push_back({e.probeId, e.tick, e.sequence, e.value}); }
        traceCursor = first + trace.size();
        outstandingTraceEnd = traceCursor;
        // This terminates the frame, including empty traces. The client ACKs
        // only after both the scene and all trace edges have been processed.
        sendJson({{"type", "trace"}, {"frameId", id}, {"epoch", traceEpoch}, {"from", from}, {"next", traceCursor}, {"reset", full}, {"edges", edges}, {"dropped", first}});
    }
};
void Hub::demo(const std::string& kind) {
    sim.clear(); projectOrigin = {}; projectName = "脉冲与记忆 · 入门电路";
    if(kind=="notes") {
        projectName="音符实验 · 材质、演奏与振动";
        for(int x=-2;x<=8;++x)for(int z=-2;z<=3;++z)sim.world.set({x,0,z},registry.state("white_concrete"));
        sim.place({0,0,0},registry.state("copper_block"));sim.place({0,1,0},registry.state("note_block"));
        sim.place({-1,1,0},registry.state("lever",{{"face","floor"}}));
        sim.place({4,1,0},registry.state("sculk_sensor"));sim.place({5,1,0},registry.state("comparator",{{"facing","west"}}));sim.place({6,1,0},registry.state("redstone_lamp"));
        sim.addProbe({4,1,0},"振动强度");sim.addProbe({4,1,0},"演奏频率","analog");
        runStart=sim.clone();return;
    }
    if(kind=="vibrations") {
        projectName="振动实验 · 传播、隔绝与共振";
        for(int x=-1;x<=17;++x)for(int z=-1;z<=21;++z)sim.world.set({x,0,z},registry.state(z==0 || z==20?"white_concrete":"stone"));
        sim.place({0,1,0},registry.state("stone_button",{{"face","floor"}}));
        sim.place({4,1,0},registry.state("sculk_sensor"));sim.place({5,1,0},registry.state("amethyst_block"));
        sim.place({13,1,0},registry.state("sculk_sensor"));sim.place({14,1,0},registry.state("comparator",{{"facing","west"}}));
        sim.place({15,1,0},registry.state("redstone_wire"));sim.place({16,1,0},registry.state("redstone_lamp"));
        sim.place({0,1,20},registry.state("lever",{{"face","floor"}}));sim.place({2,1,20},registry.state("white_wool"));
        sim.place({4,1,20},registry.state("calibrated_sculk_sensor",{{"facing","north"}}));
        sim.addProbe({4,1,0},"初级信号");sim.addProbe({13,1,0},"共振信号");sim.addProbe({13,1,0},"共振频率","analog");sim.addProbe({4,1,20},"羊毛隔绝");
        runStart=sim.clone();return;
    }
    if (kind == "droppers") {
        projectName = "投掷器实验 · 入库与抛出";
        for (int x = -2; x <= 4; ++x) for (int z = -1; z <= 5; ++z) sim.world.set({x,0,z},registry.state("white_concrete"));
        for (int z : {0,3}) {
            sim.place({0,1,z},registry.state("dropper",{{"facing","east"}}));
            sim.stimulate({0,1,z},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",8}},{{"slot",4},{"item","redstone"},{"count",8}}})}});
            sim.place({-1,1,z},registry.state("stone_button",{{"face","floor"}}));
            sim.addProbe({0,1,z},z==0?"入库库存":"抛出库存");
        }
        sim.place({1,1,0},registry.state("barrel"));
        runStart = sim.clone(); return;
    }
    if (kind == "tripwire") {
        projectName = "绊线实验 · 接触与剪断";
        for (int x = -2; x <= 9; ++x) for (int z = -1; z <= 7; ++z) sim.world.set({x,0,z}, registry.state("white_concrete"));
        for (int z : {1,5}) {
            sim.place({-1,1,z}, registry.state("stone")); sim.place({7,1,z}, registry.state("stone"));
            sim.place({0,1,z}, registry.state("tripwire_hook", {{"facing","east"}}));
            sim.place({6,1,z}, registry.state("tripwire_hook", {{"facing","west"}}));
            for (int x = 1; x < 6; ++x) sim.place({x,1,z}, registry.state("tripwire"));
            sim.place({8,1,z}, registry.state("redstone_lamp"));
            sim.addProbe({6,1,z}, z == 1 ? "触发输出" : "剪断输出");
        }
        sim.addProbe({3,1,1}, "绊线接触");
        runStart = sim.clone(); return;
    }
    if (kind == "rails") {
        projectName = "铁轨实验 · 检测与传导";
        for (int x = -1; x <= 12; ++x) for (int z = -1; z <= 8; ++z) sim.world.set({x,0,z}, registry.state("white_concrete"));
        sim.place({0,1,0}, registry.state("detector_rail", {{"shape","east_west"}}));
        for (int x = 1; x <= 10; ++x) sim.place({x,1,0}, registry.state("powered_rail", {{"shape","east_west"}}));
        sim.place({0,1,1}, registry.state("comparator", {{"facing","north"}}));
        sim.place({0,1,2}, registry.state("redstone_wire")); sim.place({0,1,3}, registry.state("redstone_lamp"));
        for (int x = 0; x < 5; ++x) sim.place({x,1,6}, registry.state("activator_rail", {{"shape","east_west"}}));
        sim.place({-1,1,6}, registry.state("lever", {{"face","floor"}}));
        for (auto pos : {BlockPos{8,1,6}, BlockPos{8,1,5}, BlockPos{8,1,7}, BlockPos{9,1,6}}) sim.place(pos, registry.state("rail"));
        sim.place({7,1,6}, registry.state("lever", {{"face","floor"}}));
        sim.addProbe({0,1,0}, "矿车检测"); sim.addProbe({0,1,1}, "矿车库存"); sim.addProbe({9,1,0}, "传导末端"); sim.addProbe({0,1,6}, "激活接口");
        runStart = sim.clone(); return;
    }
    if (kind == "inventory") {
        projectName = "漏斗实验 · 传输与锁定";
        for (int x = -1; x <= 7; ++x) for (int z = -1; z <= 4; ++z) sim.world.set({x,0,z}, registry.state("white_concrete"));
        for (int x = 0; x < 5; ++x) sim.place({x,1,0}, registry.state("hopper", {{"facing","east"}}));
        sim.place({0,2,0}, registry.state("chest")); sim.place({5,1,0}, registry.state("barrel"));
        sim.stimulate({0,2,0}, {{"inventory", Json::array({{{"slot",0},{"item","stone"},{"count",16}}})}});
        sim.place({2,1,1}, registry.state("lever", {{"face","floor"}}));
        sim.place({5,1,1}, registry.state("comparator", {{"facing","north"}}));
        sim.place({5,1,2}, registry.state("redstone_wire")); sim.place({5,1,3}, registry.state("redstone_lamp"));
        sim.addProbe({0,1,0}, "首级漏斗"); sim.addProbe({2,1,0}, "锁定漏斗"); sim.addProbe({5,1,1}, "到货读数");
        runStart = sim.clone(); return;
    }
    if (kind == "environment") {
        projectName = "环境实验 · 感测与读数";
        for (int x = -1; x <= 10; ++x) for (int z = -1; z <= 9; ++z) sim.world.set({x,0,z},registry.state("white_concrete"));
        sim.place({0,1,0},registry.state("stone_pressure_plate"));
        for (int x : {1,2}) sim.place({x,1,0},registry.state("redstone_wire"));
        sim.place({3,1,0},registry.state("iron_door",{{"facing","north"}}));
        sim.place({0,1,4},registry.state("lectern")); sim.stimulate({0,1,4},{{"pages",15}});
        sim.place({1,1,4},registry.state("stone")); sim.place({2,1,4},registry.state("comparator",{{"facing","west"}}));
        sim.place({3,1,4},registry.state("redstone_wire")); sim.place({4,1,4},registry.state("redstone_lamp"));
        sim.place({7,1,0},registry.state("lightning_rod",{{"facing","up"}}));
        sim.place({8,1,0},registry.state("redstone_wire")); sim.place({9,1,0},registry.state("redstone_lamp"));
        sim.place({7,1,4},registry.state("daylight_detector")); sim.stimulate({7,1,4},{{"skyBrightness",15},{"sunAngle",0}});
        sim.place({8,1,4},registry.state("redstone_wire")); sim.place({9,1,4},registry.state("redstone_lamp"));
        sim.place({0,1,8},registry.state("heavy_weighted_pressure_plate")); sim.place({1,1,8},registry.state("redstone_wire"));
        sim.place({4,1,8},registry.state("target")); sim.place({5,1,8},registry.state("redstone_wire"));
        sim.addProbe({0,1,0},"压力板"); sim.addProbe({2,1,4},"讲台读数"); sim.addProbe({7,1,0},"雷击脉冲"); sim.addProbe({7,1,4},"天空输入");
        runStart = sim.clone(); return;
    }
    if (kind == "pistons") {
        projectName = "活塞实验 · 推动与黏连";
        for (int x = -1; x <= 8; ++x) for (int z = -1; z <= 5; ++z) sim.world.set({x,0,z},registry.state("obsidian"));
        for (int z : {0,4}) {
            sim.place({0,1,z},registry.state("lever",{{"face","floor"}}));
            sim.place({1,1,z},registry.state("redstone_wire")); sim.place({2,1,z},registry.state("redstone_wire"));
            sim.place({3,1,z},registry.state(z == 0 ? "piston" : "sticky_piston",{{"facing","east"}}));
            sim.place({4,1,z},registry.state(z == 0 ? "stone" : "slime_block"));
            if (z == 4) sim.place({4,2,z},registry.state("redstone_lamp"));
            sim.addProbe({0,1,z},z == 0 ? "推动输入" : "黏连输入"); sim.addProbe({3,1,z},z == 0 ? "活塞伸出" : "黏性活塞伸出");
        }
        runStart = sim.clone(); return;
    }
    for (int x = -1; x <= 12; ++x) for (int z = -1; z <= 5; ++z) sim.world.set({x, 0, z}, registry.state((z == 0 || z == 4) ? "white_concrete" : "stone"));
    auto wire = registry.state("redstone_wire");
    sim.place({0, 1, 0}, registry.state("lever", {{"face", "floor"}}));
    for (int x : {1, 2, 4, 5, 7, 8}) sim.place({x, 1, 0}, wire);
    sim.place({3, 1, 0}, registry.state("repeater", {{"facing", "west"}, {"delay", "2"}}));
    sim.place({6, 1, 0}, registry.state("repeater", {{"facing", "west"}, {"delay", "4"}}));
    sim.place({9, 1, 0}, registry.state("redstone_lamp"));
    sim.place({0, 1, 4}, registry.state("stone_button", {{"face", "floor"}}));
    for (int x : {1, 2, 4, 5}) sim.place({x, 1, 4}, wire);
    sim.place({3, 1, 4}, registry.state("repeater", {{"facing", "west"}}));
    sim.place({6, 1, 4}, registry.state("copper_bulb"));
    sim.place({7, 1, 4}, registry.state("comparator", {{"facing", "west"}}));
    sim.place({8, 1, 4}, wire); sim.place({9, 1, 4}, registry.state("redstone_lamp"));
    sim.addProbe({0, 1, 0}, "输入 / IN"); sim.addProbe({4, 1, 0}, "延迟 4 gt"); sim.addProbe({8, 1, 0}, "延迟 12 gt"); sim.addProbe({6, 1, 4}, "记忆 / Q");
    runStart = sim.clone();
}
void Hub::start() {
    timer.expires_after(std::chrono::milliseconds(8));
    timer.async_wait([this](beast::error_code ec) {
        if (ec) return;
        auto now = Clock::now(); double elapsed = std::chrono::duration<double>(now - lastPump).count(); lastPump = now;
        if (now - lastFileCleanup > std::chrono::seconds(1)) {
            lastFileCleanup = now;
            if (activeFileJob && now - activeFileJob->created > std::chrono::seconds(60) && activeFileJob->status().at("stage") == "等待上传") {
                auto expired = activeFileJob; expired->cancelled = true; endFileJob(expired, "等待上传超时");
            }
            for (auto it = fileJobs.begin(); it != fileJobs.end();) {
                if (it->second != activeFileJob && now - it->second->created > std::chrono::minutes(15)) it = fileJobs.erase(it); else ++it;
            }
        }
        bool stalled = false; bool connected = false;
        for (auto it = clients.begin(); it != clients.end();) {
            auto c = it->lock(); if (!c || !c->alive) { it = clients.erase(it); continue; }
            connected = true; if (c->awaitingAck && now - c->sentAt > std::chrono::seconds(2)) stalled = true; ++it;
        }
        if (running && (!connected || stalled)) { running = false; sim.pauseReason = connected ? "浏览器未确认数据，仿真已暂停以保留记录" : "浏览器已断开，仿真已暂停"; }
        protectTrace();
        if (running) {
            const auto before = sim.statistics.scheduledEvents;
            try {
                if (speed == 0) {
                    sim.advanceActive(8192, std::chrono::milliseconds(5));
                    if (sim.pendingEvents() == 0 && !sim.breakRequested) { running = false; sim.pauseReason = "电路已稳定，没有待执行事件"; }
                } else {
                    fractionalTicks += std::min(elapsed, 0.1) * speed;
                    auto ticks = static_cast<Tick>(fractionalTicks); fractionalTicks -= static_cast<double>(ticks);
                    sim.advanceTo(sim.currentTick + ticks, 8192, std::chrono::milliseconds(5));
                }
                if (sim.breakRequested) running = false;
            } catch (const std::exception& error) { running = false; sim.pauseReason = error.what(); }
            measuredEvents += sim.statistics.scheduledEvents - before;
        }
        auto measuredNow = Clock::now();
        auto measurementSeconds = std::chrono::duration<double>(measuredNow - measuredTime).count();
        if (measurementSeconds > 0.5) { eventsPerSecond = static_cast<double>(measuredEvents) / measurementSeconds; measuredEvents = 0; measuredTime = measuredNow; }
        if (now - lastPublish >= std::chrono::milliseconds(40)) { publish(); lastPublish = now; }
        start();
    });
}
void Hub::publish(bool full) {
    if (full) { ++traceEpoch; pendingFull = true; }
    std::vector<std::shared_ptr<Client>> active;
    for (const auto& weak : clients) if (auto c = weak.lock(); c && c->alive) active.push_back(c);
    protectTrace();
    if (std::any_of(active.begin(), active.end(), [](const auto& c) { return c->awaitingAck; })) return;
    full = pendingFull; pendingFull = false;
    auto cells = full ? sim.world.cells() : sim.takeChanges();
    if (full) sim.takeChanges();
    for (auto& c : active) { c->frame(cells, full, ++frameId); c->sendJson(status()); }
    protectTrace();
}
void Hub::protectTrace() {
    std::optional<std::uint64_t> first;
    for (const auto& weak : clients) if (auto c = weak.lock(); c && c->alive) {
        auto acknowledged = c->traceEpoch == traceEpoch ? c->traceAcknowledged : sim.traceDropped;
        first = first ? std::min(*first, acknowledged) : acknowledged;
    }
    sim.retainTraceFrom(first);
}
void Hub::command(const std::shared_ptr<Client>& client, const Json& message) {
    std::string cmd = message.at("cmd"); auto requestId = message.value("requestId", 0);
    if (cmd == "ack") {
        if (client->awaitingAck && message.at("frameId") == client->outstandingFrame && message.value("epoch", UINT64_MAX) == client->traceEpoch && message.value("traceEnd", UINT64_MAX) == client->outstandingTraceEnd) {
            client->awaitingAck = false; client->traceAcknowledged = client->outstandingTraceEnd; protectTrace();
        }
        return;
    }
    if (activeFileJob && cmd != "inspect" && cmd != "pause" && cmd != "fileStatus" && cmd != "cancelFile")
        throw std::invalid_argument("工程文件正在读写，请等待完成或取消");
    protectTrace();
    if (sim.traceBlocked() && (cmd == "play" || cmd == "step" || cmd == "stepEvent" || cmd == "place" || cmd == "remove" || cmd == "edit" || cmd == "interact" || cmd == "stimulate" || cmd == "probe")) {
        running = false;
        throw std::invalid_argument("探针缓冲等待浏览器确认，请稍后继续，或清除历史采样后重新运行");
    }
    if (sim.hasPendingActions() && (cmd == "play" || cmd == "step" || cmd == "stepEvent")) {
        running = false;
        throw std::invalid_argument("请先处理外部动作；可输入环境反馈，然后确认继续");
    }
    Json result = Json::object(); bool full = false;
    if (cmd == "play") { if (sim.faulted) throw std::invalid_argument("执行已中止，请先撤销或加载快照"); sim.breakRequested = false; sim.pauseReason.clear(); if (!runStart) runStart = sim.clone(); running = true; fractionalTicks = 0; lastPump = Clock::now(); measuredTime = lastPump; measuredEvents = 0; eventsPerSecond = 0; }
    else if (cmd == "pause") { running = false; }
    else if (cmd == "speed") { double next = message.at("value"); if (!std::isfinite(next) || next < 0 || next > 1000000) throw std::invalid_argument("无效运行速度"); speed = next; }
    else if (cmd == "traceBudget") {
        auto capacity = message.at("capacity").get<std::size_t>();
        if (capacity < 256 || capacity > 500000) throw std::invalid_argument("探针历史容量范围为 256–500000 条");
        sim.traceCapacity = capacity; protectTrace();
    }
    else if (cmd == "step" || cmd == "stepEvent") { running = false; sim.breakRequested = false; sim.pauseReason.clear(); if (!runStart) runStart = sim.clone(); if (cmd == "stepEvent") sim.stepEvent(); else { int count = message.value("count", 1); if (count < 1 || count > 10000) throw std::invalid_argument("单步范围为 1–10000 gt"); sim.advanceTo(sim.currentTick + static_cast<Tick>(count), 100000, std::chrono::milliseconds(40)); } }
    else if (cmd == "inspect") { result = sim.inspect(message.at("pos").get<BlockPos>()); }
    else if (cmd == "actionLog") { result = {{"actions", sim.actionHistory()}, {"dropped", sim.actionHistoryDropped()}}; }
    else if (cmd == "resolveAction") {
        if (message.at("revision") != sim.revision) throw std::invalid_argument("状态已变化，请查看最新外部动作后重试");
        const auto& id = message.at("id");
        if (!id.is_number_integer() || id < 1) throw std::invalid_argument("无效外部动作序号");
        auto pending = sim.pendingActionsJson();
        if (std::none_of(pending.begin(), pending.end(), [&](const auto& action) { return action.at("id") == id; })) throw std::invalid_argument("外部动作不存在或已确认");
        running = false; remember(); sim.resolveAction(id.get<std::uint64_t>());
    }
    else if (cmd == "exportFile" || cmd == "importFile") {
        const auto checkpoint = message.value("checkpoint", false);
        const auto format = message.value("format", std::string("vmcb"));
        if (format != "vmcb" && format != "json") throw std::invalid_argument("未知文件格式");
        auto job = beginFileJob(cmd == "exportFile" ? "export" : "import"); result = job->status();
        if (cmd == "exportFile") exportFile(job, checkpoint, format == "json");
    }
    else if (cmd == "fileStatus" || cmd == "cancelFile") {
        auto found = fileJobs.find(message.at("id").get<std::string>());
        if (found == fileJobs.end()) throw std::invalid_argument("文件任务不存在或已过期");
        auto job = found->second;
        if (cmd == "cancelFile" && activeFileJob == job) { job->cancelled = true; if (job->status().at("stage") == "等待上传") endFileJob(job, "文件操作已取消"); }
        result = job->status();
    }
    else if (cmd == "save") { result = sim.saveProject(projectName, message.value("checkpoint", false)); }
    else if (cmd == "vcd") { result = {{"text", sim.exportVcd()}}; }
    else if (cmd == "probe") { result["id"] = sim.addProbe(message.at("pos").get<BlockPos>(), message.value("name", std::string()), message.value("mode", std::string("output"))); }
    else if (cmd == "removeProbe") sim.removeProbe(message.at("id"));
    else if (cmd == "configureProbe") sim.configureProbe(message.at("id"), message);
    else if (cmd == "clearTrace") { sim.clearTrace(); full = true; }
    else if (cmd == "undo" || cmd == "redo") {
        running = false; auto& from = cmd == "undo" ? undo : redo; auto& to = cmd == "undo" ? redo : undo;
        if (!from.empty()) { to.push_back({projectName, sim.clone(), projectOrigin}); auto saved = std::move(from.back()); from.pop_back(); sim.restore(*saved.state); projectName = saved.name; projectOrigin = saved.origin; runStart.reset(); full = true; }
    }
    else if (cmd == "reset") { running = false; if (runStart) { sim.restore(*runStart); full = true; } }
    else if (cmd == "rename") projectName = message.at("name").get<std::string>().substr(0, 200);
    else if (cmd == "place" || cmd == "remove" || cmd == "interact" || cmd == "stimulate" || cmd == "edit" || cmd == "load" || cmd == "new" || cmd == "demo") {
        const bool resumeAfter = running && (cmd == "interact" || cmd == "stimulate");
        running = false; auto savedRunStart = resumeAfter ? std::move(runStart) : nullptr; remember();
        try {
            if (cmd == "new") { sim.clear(); projectOrigin = {}; projectName = "未命名电路"; full = true; }
            else if (cmd == "demo") { demo(message.value("kind", std::string("basic"))); full = true; }
            else if (cmd == "load") { sim.loadProject(message.at("project")); projectOrigin = {}; projectName = message.at("project").value("name", std::string("导入电路")); full = true; }
            else if (cmd == "edit") {
                if (message.at("blocks").size() > 100000) throw std::invalid_argument("一次编辑最多 10 万个方块");
                for (const auto& row : message.at("blocks")) { auto p = row.at("pos").get<BlockPos>(); auto id = registry.state(row.at("name"), row.value("properties", Json::object())); if (id == 0) sim.setBlock(p, 0); else sim.place(p, id); }
            } else {
                auto p = message.at("pos").get<BlockPos>();
                if (cmd == "remove") sim.setBlock(p, 0);
                else if (cmd == "place") sim.place(p, registry.state(message.at("name"), message.value("properties", Json::object())));
                else if (cmd == "interact") sim.interact(p);
                else sim.stimulate(p, message.at("stimulus"));
            }
        } catch (...) { sim.restore(*undo.back().state); undo.pop_back(); throw; }
        if (resumeAfter) { runStart = std::move(savedRunStart); running = !sim.breakRequested; }
    } else throw std::invalid_argument("未知命令：" + cmd);
    result["type"] = "reply"; result["requestId"] = requestId; result["cmd"] = cmd; client->sendJson(result);
    if (full) publish(true); else { publish(); client->sendJson(status()); }
}
void Hub::trimHistory() {
    std::size_t bytes = 0; for (const auto& entry : undo) bytes += entry.state->estimatedBytes();
    while (undo.size() > 1 && (undo.size() > 32 || bytes > 128u * 1024u * 1024u)) { bytes -= undo.front().state->estimatedBytes(); undo.pop_front(); }
}
std::shared_ptr<FileJob> Hub::beginFileJob(const std::string& operation) {
    if (activeFileJob) throw std::invalid_argument("已有文件操作进行中");
    for (auto it = fileJobs.begin(); it != fileJobs.end();) {
        const auto state = it->second->status().at("state");
        if (Clock::now() - it->second->created > std::chrono::minutes(15) || (state != "working" && (it->second->operation == "import" || state != "done"))) it = fileJobs.erase(it); else ++it;
    }
    if (fileJobs.size() >= 8) throw std::invalid_argument("文件任务过多，请等待临时下载过期");
    auto job = std::make_shared<FileJob>(operation); job->info = {projectName, projectOrigin}; job->revision = sim.revision;
    if (operation == "import") job->stage = "等待上传";
    running = false; activeFileJob = job; fileJobs.emplace(job->id, job); return job;
}
void Hub::endFileJob(const std::shared_ptr<FileJob>& job, const std::string& error) {
    job->finish(error); if (activeFileJob == job) activeFileJob.reset();
    for (const auto& weak : clients) if (auto client = weak.lock()) client->sendJson(status());
}
void Hub::exportFile(const std::shared_ptr<FileJob>& job, bool checkpoint, bool json) {
    try {
        // Clone on the owner thread at a completed event boundary. The worker
        // owns this copy; the live World's mutable lookup cache is never shared.
        auto snapshot = sim.clone();
        asio::post(fileWorkers, [this, job, checkpoint, json, snapshot = std::move(snapshot)] {
            std::string error;
            try {
                std::ofstream output(job->path, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("无法写入临时工程文件");
                if (json) {
                    job->progress("编码旧 JSON", 0, 0);
                    auto value = snapshot->saveProject(job->info.name, checkpoint, [job] { job->progress("编码旧 JSON", 0, 0); });
                    output << value.dump(2);
                    job->progress("文件已生成", static_cast<std::uint64_t>(output.tellp()), static_cast<std::uint64_t>(output.tellp()));
                } else writeVmcb(output, *snapshot, job->info, checkpoint, job->options());
                output.close();
                if (!output) throw std::runtime_error("关闭工程文件失败");
                job->info.name += checkpoint ? (json ? ".snapshot.verimc.json" : ".snapshot.vmcb") : (json ? ".verimc.json" : ".vmcb");
            } catch (const std::exception& exception) { error = exception.what(); }
            asio::post(io, [this, job, error] { endFileJob(job, error); });
        });
    } catch (const std::exception& error) { endFileJob(job, error.what()); }
}
void Hub::importFile(const std::shared_ptr<FileJob>& job) {
    const auto capacity = sim.traceCapacity, reserve = sim.traceAtomicReserve, budget = sim.updateBudget;
    asio::post(fileWorkers, [this, job, capacity, reserve, budget] {
        std::unique_ptr<Simulator> candidate; ProjectInfo info; std::string error;
        try {
            candidate = std::make_unique<Simulator>(registry); candidate->traceCapacity = capacity; candidate->traceAtomicReserve = reserve; candidate->updateBudget = budget;
            std::ifstream input(job->path, std::ios::binary); if (!input) throw std::runtime_error("无法读取上传工程");
            info = readProjectFile(input, *candidate, job->options());
        } catch (const std::exception& exception) { error = exception.what(); }
        asio::post(io, [this, job, error, info, candidate = std::move(candidate)]() mutable {
            if (job->cancelled && error.empty()) error = "文件操作已取消";
            if (sim.revision != job->revision && error.empty()) error = "当前工程已变化，未替换工程";
            if (error.empty()) {
                // Allocate the undo entry before committing; the exchange is
                // allocation-free and retains the old world without cloning it.
                try { undo.push_back({projectName, std::move(candidate), projectOrigin}); }
                catch (const std::exception& exception) { error = exception.what(); }
                if (error.empty()) {
                    sim.exchangeProject(*undo.back().state); projectName.swap(info.name); projectOrigin = info.origin;
                    redo.clear(); runStart.reset(); trimHistory();
                    // Publish on the regular pump after committing. A completed
                    // import must not be reported as failed due to rendering.
                    ++traceEpoch; pendingFull = true;
                }
            }
            endFileJob(job, error);
            std::error_code ignored; std::filesystem::remove(job->path, ignored);
        });
    });
}
class HttpSession : public std::enable_shared_from_this<HttpSession> {
    Tcp::socket socket;
    beast::flat_buffer buffer;
    http::request_parser<http::buffer_body> parser;
    Hub& hub;
    std::shared_ptr<FileJob> uploadJob;
    std::ofstream upload;
    std::array<char, 65536> uploadBuffer{};
    std::uint64_t uploaded{}, uploadTotal{};
    asio::steady_timer deadline;
    bool uploadFinished{};
    void startDeadline(std::chrono::seconds timeout = std::chrono::seconds(60)) {
        deadline.expires_after(timeout);
        deadline.async_wait([weak = weak_from_this()](beast::error_code error) {
            if (error) return;
            if (auto self = weak.lock()) { beast::error_code ignored; self->socket.close(ignored); if (self->uploadJob && !self->uploadFinished) self->failUpload("文件上传超时"); }
        });
    }
    void failUpload(const std::string& message) {
        if (uploadFinished) return; uploadFinished = true; upload.close();
        if (uploadJob) hub.endFileJob(uploadJob, message);
    }
    bool authenticated(const http::request<http::buffer_body>& request, const std::string& query = {}) {
        const std::string origin(request[http::field::origin]); const auto localhost = "localhost" + hub.host.substr(hub.host.find(':'));
        if (!origin.empty() && origin != "http://" + hub.host && origin != "http://" + localhost) return false;
        return request["X-Simulator-Token"] == hub.token || query == "token=" + hub.token;
    }
    static std::string encodedFilename(std::string name) {
        for (auto& ch : name) if (static_cast<unsigned char>(ch) < 32 || ch == '/' || ch == '\\') ch = '_';
        std::string suffix;
        for (const auto* extension : {".snapshot.verimc.json", ".verimc.json", ".snapshot.vmcb", ".vmcb"})
            if (name.ends_with(extension)) { suffix = extension; name.resize(name.size() - suffix.size()); break; }
        if (name.size() + suffix.size() > 200) { name.resize(200 - suffix.size()); while (!name.empty()) { try { simulator::vmcb::validUtf8(name); break; } catch (...) { name.pop_back(); } } }
        name += suffix;
        static constexpr char hex[] = "0123456789ABCDEF"; std::string encoded;
        for (char character : name) { const auto ch = static_cast<unsigned char>(character); if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_') encoded.push_back(static_cast<char>(ch)); else { encoded.push_back('%'); encoded.push_back(hex[ch >> 4]); encoded.push_back(hex[ch & 15]); } }
        return encoded;
    }
    void download(const std::shared_ptr<FileJob>& job) {
        if (job->operation != "export" || job->status().at("state") != "done") { reply(http::status::conflict, "文件尚未生成或操作失败"); return; }
        auto response = std::make_shared<http::response<http::file_body>>(http::status::ok, 11); beast::error_code error;
        response->body().open(job->path.c_str(), beast::file_mode::scan, error);
        if (error) { reply(http::status::not_found, "下载文件已过期"); return; }
        const auto fallback = job->info.name.ends_with(".json") ? "project.json" : "project.vmcb";
        response->set(http::field::content_type, "application/octet-stream"); response->set(http::field::content_disposition, std::string("attachment; filename=") + fallback + "; filename*=UTF-8''" + encodedFilename(job->info.name)); response->set(http::field::cache_control, "no-store"); response->set("X-Content-Type-Options", "nosniff"); response->set("Referrer-Policy", "no-referrer"); response->content_length(response->body().size()); response->keep_alive(false);
        hub.fileJobs.erase(job->id);
        startDeadline(std::chrono::minutes(15)); http::async_write(socket, *response, [self = shared_from_this(), response, job](beast::error_code, std::size_t) { self->deadline.cancel(); beast::error_code ignored; self->socket.shutdown(Tcp::socket::shutdown_send, ignored); });
    }
    void readUpload() {
        if (uploadFinished) return;
        if (uploadJob->cancelled) { failUpload("文件操作已取消"); reply(http::status::conflict, "文件操作已取消"); return; }
        parser.get().body().data = uploadBuffer.data(); parser.get().body().size = uploadBuffer.size(); startDeadline();
        http::async_read_some(socket, buffer, parser, [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (self->uploadFinished) return;
            self->deadline.cancel(); const auto count = self->uploadBuffer.size() - self->parser.get().body().size;
            if (count) { self->upload.write(self->uploadBuffer.data(), static_cast<std::streamsize>(count)); self->uploaded += count; }
            if (!self->upload) { self->failUpload("上传临时文件写入失败"); self->reply(http::status::internal_server_error, "上传临时文件写入失败"); return; }
            if (error && error != http::error::need_buffer) { self->failUpload("上传中断或超过文件预算"); self->reply(http::status::bad_request, "上传中断或超过文件预算"); return; }
            try { self->uploadJob->progress("上传文件", self->uploaded, self->uploadTotal); }
            catch (...) { self->failUpload("文件操作已取消"); self->reply(http::status::conflict, "文件操作已取消"); return; }
            if (!self->parser.is_done()) { self->readUpload(); return; }
            self->upload.close(); if (!self->upload) { self->failUpload("上传关闭失败"); self->reply(http::status::internal_server_error, "上传关闭失败"); return; }
            self->uploadFinished = true; self->hub.importFile(self->uploadJob); self->reply(http::status::accepted, self->uploadJob->status().dump(), "application/json");
        });
    }
    void respond() {
        const auto& request = parser.get(); std::string host(request[http::field::host]); auto localhost = "localhost" + hub.host.substr(hub.host.find(':'));
        if (host != hub.host && host != localhost) { reply(http::status::forbidden, "Invalid local host"); return; }
        if (ws::is_upgrade(request)) {
            std::string origin(request[http::field::origin]);
            if ((origin != "http://" + hub.host && origin != "http://" + localhost) || request.target() != "/socket?token=" + hub.token) { reply(http::status::forbidden, "Invalid local session"); return; }
            http::request<http::string_body> upgrade; upgrade.base() = request.base(); std::make_shared<Client>(std::move(socket), hub)->accept(std::move(upgrade)); return;
        }
        std::string target(request.target()), query; if (auto pos = target.find('?'); pos != std::string::npos) { query = target.substr(pos + 1); target.resize(pos); }
        if (target.starts_with("/api/files/")) {
            const auto tail = target.substr(11); const auto slash = tail.find('/'); const auto id = tail.substr(0, slash); const auto action = slash == std::string::npos ? "" : tail.substr(slash + 1);
            if (!authenticated(request, action == "download" ? query : "")) { reply(http::status::forbidden, "Invalid local session"); return; }
            auto found = hub.fileJobs.find(id); if (found == hub.fileJobs.end()) { reply(http::status::not_found, "文件任务不存在或已过期"); return; } auto job = found->second;
            if (request.method() == http::verb::get && action.empty()) { reply(http::status::ok, job->status().dump(), "application/json"); return; }
            if (request.method() == http::verb::get && action == "download") { download(job); return; }
            if (request.method() == http::verb::post && action == "upload") {
                if (hub.activeFileJob != job || job->operation != "import" || job->status().at("stage") != "等待上传" || job->cancelled) { reply(http::status::conflict, "上传任务状态不匹配"); return; }
                uploadTotal = parser.content_length().value_or(0); if (uploadTotal > 8ULL * 1024 * 1024 * 1024) { hub.endFileJob(job, "文件超过 8 GiB"); reply(http::status::payload_too_large, "文件超过 8 GiB"); return; }
                parser.body_limit(8ULL * 1024 * 1024 * 1024);
                uploadJob = job; upload.open(job->path, std::ios::binary | std::ios::trunc); if (!upload) { failUpload("无法创建上传文件"); reply(http::status::internal_server_error, "无法创建上传文件"); return; }
                job->progress("上传文件", 0, uploadTotal); if (parser.is_done()) { failUpload("上传文件为空"); reply(http::status::bad_request, "上传文件为空"); return; } readUpload(); return;
            }
            reply(http::status::method_not_allowed, "Unsupported file operation"); return;
        }
        if (request.method() != http::verb::get) { reply(http::status::method_not_allowed, "GET only"); return; }
        if (target == "/api/bootstrap") { reply(http::status::ok, Json{{"token", hub.token}, {"version", "26.2"}}.dump(), "application/json"); return; }
        if (target == "/health") { reply(http::status::ok, "ok"); return; }
        if (target == "/") target = "/index.html";
        if (target.find("..") != std::string::npos || target.find('%') != std::string::npos || target.find('\\') != std::string::npos) { reply(http::status::bad_request, "Invalid path"); return; }
        auto path = std::filesystem::path(SIMULATOR_WEB_DIR) / target.substr(1); std::ifstream file(path, std::ios::binary);
        if (!file) { reply(http::status::not_found, "Build the frontend first: cd simulator/apps/web && npm run build"); return; }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto ext = path.extension().string(); std::string mime = ext == ".html" ? "text/html; charset=utf-8" : ext == ".js" ? "text/javascript" : ext == ".css" ? "text/css" : ext == ".svg" ? "image/svg+xml" : "application/octet-stream";
        reply(http::status::ok, std::move(content), mime);
    }
public:
    HttpSession(Tcp::socket connection, Hub& owner) : socket(std::move(connection)), hub(owner), deadline(owner.io) { parser.body_limit(UINT64_MAX); parser.header_limit(8192); }
    void start() {
        startDeadline(); http::async_read_header(socket, buffer, parser, [self = shared_from_this()](beast::error_code error, std::size_t) {
            self->deadline.cancel(); if (error) return;
            try { self->respond(); } catch (const std::exception& exception) { if (self->uploadJob) self->failUpload(exception.what()); self->reply(http::status::bad_request, exception.what()); }
        });
    }
    void reply(http::status status, std::string body, const std::string& mime = "text/plain; charset=utf-8") {
        auto response = std::make_shared<http::response<http::string_body>>(status, 11);
        response->set(http::field::content_type, mime); response->set(http::field::cache_control, "no-store");
        response->set("X-Content-Type-Options", "nosniff"); response->set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'");
        response->body() = std::move(body); response->prepare_payload(); response->keep_alive(false);
        http::async_write(socket, *response, [self = shared_from_this(), response](beast::error_code, std::size_t) { beast::error_code ignored; self->socket.shutdown(Tcp::socket::shutdown_send, ignored); });
    }
};}
int runServer(unsigned short port) {
    asio::io_context io; Hub hub(io, port); hub.demo();
    Tcp::acceptor acceptor(io, {asio::ip::make_address("127.0.0.1"), port});
    std::function<void()> accept = [&] { acceptor.async_accept([&](beast::error_code ec, Tcp::socket socket) { if (!ec) std::make_shared<HttpSession>(std::move(socket), hub)->start(); if (acceptor.is_open()) accept(); }); }; accept();
    asio::signal_set signals(io, SIGINT, SIGTERM); signals.async_wait([&](beast::error_code, int) { io.stop(); });
    std::cout << "Simulator · Minecraft Java 26.2\nhttp://127.0.0.1:" << port << "\n" << std::flush;
    hub.start(); io.run(); return 0;
}
