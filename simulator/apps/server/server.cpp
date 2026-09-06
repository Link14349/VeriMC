#include "server.hpp"
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
class Client;
struct Hub {
    asio::io_context& io;
    BlockRegistry registry;
    Simulator sim{registry};
    asio::steady_timer timer;
    std::vector<std::weak_ptr<Client>> clients;
    struct HistoryEntry { std::string name; std::unique_ptr<Simulator> state; };
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
    Clock::time_point lastPump{Clock::now()}, lastPublish{Clock::now()};
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
    void remember() { undo.push_back({projectName, sim.clone()}); std::size_t bytes = 0; for (const auto& entry : undo) bytes += entry.state->estimatedBytes(); while (undo.size() > 1 && (undo.size() > 32 || bytes > 128u * 1024u * 1024u)) { bytes -= undo.front().state->estimatedBytes(); undo.pop_front(); } redo.clear(); runStart.reset(); }
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
            self->hub.clients.push_back(self); self->sendJson({{"type", "ready"}, {"version", "26.2"}, {"protocolVersion", 3}, {"catalog", self->hub.registry.catalog()}, {"items", self->hub.registry.itemCatalog()}});
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
    sim.clear(); projectName = "脉冲与记忆 · 入门电路";
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
    else if (cmd == "save") { result = sim.saveProject(projectName, message.value("checkpoint", false)); }
    else if (cmd == "vcd") { result = {{"text", sim.exportVcd()}}; }
    else if (cmd == "probe") { result["id"] = sim.addProbe(message.at("pos").get<BlockPos>(), message.value("name", std::string()), message.value("mode", std::string("output"))); }
    else if (cmd == "removeProbe") sim.removeProbe(message.at("id"));
    else if (cmd == "configureProbe") sim.configureProbe(message.at("id"), message);
    else if (cmd == "clearTrace") { sim.clearTrace(); full = true; }
    else if (cmd == "undo" || cmd == "redo") {
        running = false; auto& from = cmd == "undo" ? undo : redo; auto& to = cmd == "undo" ? redo : undo;
        if (!from.empty()) { to.push_back({projectName, sim.clone()}); auto saved = std::move(from.back()); from.pop_back(); sim.restore(*saved.state); projectName = saved.name; runStart.reset(); full = true; }
    }
    else if (cmd == "reset") { running = false; if (runStart) { sim.restore(*runStart); full = true; } }
    else if (cmd == "rename") projectName = message.at("name").get<std::string>().substr(0, 200);
    else if (cmd == "place" || cmd == "remove" || cmd == "interact" || cmd == "stimulate" || cmd == "edit" || cmd == "load" || cmd == "new" || cmd == "demo") {
        const bool resumeAfter = running && (cmd == "interact" || cmd == "stimulate");
        running = false; auto savedRunStart = resumeAfter ? std::move(runStart) : nullptr; remember();
        try {
            if (cmd == "new") { sim.clear(); projectName = "未命名电路"; full = true; }
            else if (cmd == "demo") { demo(message.value("kind", std::string("basic"))); full = true; }
            else if (cmd == "load") { sim.loadProject(message.at("project")); projectName = message.at("project").value("name", std::string("导入电路")); full = true; }
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
class HttpSession : public std::enable_shared_from_this<HttpSession> {
    Tcp::socket socket; beast::flat_buffer buffer; http::request_parser<http::string_body> parser; Hub& hub;
public:
    HttpSession(Tcp::socket connection, Hub& owner) : socket(std::move(connection)), hub(owner) { parser.body_limit(1024); parser.header_limit(8192); }
    void start() {
        http::async_read(socket, buffer, parser, [self = shared_from_this()](beast::error_code ec, std::size_t) { if (!ec) self->respond(self->parser.release()); });
    }
    void respond(http::request<http::string_body> request) {
        std::string host(request[http::field::host]); auto expectedLocalhost = "localhost" + hub.host.substr(hub.host.find(':'));
        if (host != hub.host && host != expectedLocalhost) { reply(http::status::forbidden, "Invalid local host"); return; }
        if (ws::is_upgrade(request)) {
            std::string origin(request[http::field::origin]);
            if ((origin != "http://" + hub.host && origin != "http://" + expectedLocalhost) || request.target() != "/socket?token=" + hub.token) { reply(http::status::forbidden, "Invalid local session"); return; }
            std::make_shared<Client>(std::move(socket), hub)->accept(std::move(request)); return;
        }
        if (request.method() != http::verb::get) { reply(http::status::method_not_allowed, "GET only"); return; }
        std::string target(request.target());
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
    void reply(http::status status, std::string body, const std::string& mime = "text/plain") {
        auto response = std::make_shared<http::response<http::string_body>>(status, 11);
        response->set(http::field::content_type, mime); response->set(http::field::cache_control, "no-store");
        response->set("X-Content-Type-Options", "nosniff"); response->set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'");
        response->body() = std::move(body); response->prepare_payload(); response->keep_alive(false);
        http::async_write(socket, *response, [self = shared_from_this(), response](beast::error_code, std::size_t) { beast::error_code ignored; self->socket.shutdown(Tcp::socket::shutdown_send, ignored); });
    }
};
}
int runServer(unsigned short port) {
    asio::io_context io; Hub hub(io, port); hub.demo();
    Tcp::acceptor acceptor(io, {asio::ip::make_address("127.0.0.1"), port});
    std::function<void()> accept = [&] { acceptor.async_accept([&](beast::error_code ec, Tcp::socket socket) { if (!ec) std::make_shared<HttpSession>(std::move(socket), hub)->start(); if (acceptor.is_open()) accept(); }); }; accept();
    asio::signal_set signals(io, SIGINT, SIGTERM); signals.async_wait([&](beast::error_code, int) { io.stop(); });
    std::cout << "Simulator · Minecraft Java 26.2\nhttp://127.0.0.1:" << port << "\n" << std::flush;
    hub.start(); io.run(); return 0;
}
