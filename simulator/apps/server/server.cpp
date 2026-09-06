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
    std::string token, host;
    std::string projectName{"未命名电路"};
    Clock::time_point lastPump{Clock::now()}, lastPublish{Clock::now()};
    double measuredTps{};
    Tick measuredTick{};
    Clock::time_point measuredTime{Clock::now()};
    explicit Hub(asio::io_context& context, unsigned short port) : io(context), timer(context), host("127.0.0.1:" + std::to_string(port)) {
        std::random_device random; std::ostringstream value; value << std::hex; for (int i = 0; i < 8; ++i) value << random(); token = value.str();
    }
    Json status() const {
        Json probes = Json::array(); for (const auto& p : sim.getProbes()) probes.push_back({{"id", p.id}, {"pos", p.pos}, {"name", p.name}, {"mode", p.mode}, {"value", p.lastValue}, {"trigger", p.trigger}});
        return {{"type", "status"}, {"tick", sim.currentTick}, {"running", running}, {"speed", speed}, {"measuredTps", measuredTps}, {"blocks", sim.world.size()}, {"pending", sim.pendingEvents()}, {"updates", sim.statistics.updates}, {"events", sim.statistics.scheduledEvents}, {"storageBytes", sim.world.storageBytes()}, {"traceDropped", sim.traceDropped}, {"pauseReason", sim.pauseReason}, {"revision", sim.revision}, {"probes", probes}, {"canUndo", !undo.empty()}, {"canRedo", !redo.empty()}, {"name", projectName}};
    }
    void remember() { undo.push_back({projectName, sim.clone()}); std::size_t bytes = 0; for (const auto& entry : undo) bytes += entry.state->estimatedBytes(); while (undo.size() > 1 && (undo.size() > 32 || bytes > 128u * 1024u * 1024u)) { bytes -= undo.front().state->estimatedBytes(); undo.pop_front(); } redo.clear(); runStart.reset(); }
    void demo();
    void start();
    void publish(bool full = false);
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
    std::unordered_set<StateId> knownStates;
    Client(Tcp::socket transport, Hub& owner) : socket(std::move(transport)), hub(owner) {}
    void accept(http::request<http::string_body> request) {
        socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::server)); socket.read_message_max(64 * 1024 * 1024);
        socket.async_accept(request, [self = shared_from_this()](beast::error_code ec) {
            if (ec) { self->alive = false; return; }
            self->hub.clients.push_back(self); self->sendJson({{"type", "ready"}, {"version", "26.2"}, {"catalog", self->hub.registry.catalog()}});
            self->frame(self->hub.sim.world.cells(), true, ++self->hub.frameId); self->sendJson(self->hub.status()); self->read();
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
        Json definitions = Json::array(); for (const auto& cell : cells) if (knownStates.insert(cell.state).second) definitions.push_back(hub.registry.describe(cell.state));
        if (!definitions.empty()) sendJson({{"type", "states"}, {"states", definitions}});
        std::string binary; binary.reserve(32 + cells.size() * 20);
        auto u32 = [&](std::uint32_t value) { for (int byte = 0; byte < 4; ++byte) binary.push_back(static_cast<char>((value >> (byte * 8)) & 255u)); };
        auto u64 = [&](std::uint64_t value) { u32(static_cast<std::uint32_t>(value)); u32(static_cast<std::uint32_t>(value >> 32)); };
        u32(0x31434d56); u32(full ? 1 : 2); u32(id); u32(static_cast<std::uint32_t>(cells.size())); u64(hub.sim.currentTick); u64(hub.sim.revision);
        for (const auto& cell : cells) { u32(static_cast<std::uint32_t>(cell.pos.x)); u32(static_cast<std::uint32_t>(cell.pos.y)); u32(static_cast<std::uint32_t>(cell.pos.z)); u32(cell.state); u32(static_cast<std::uint32_t>(hub.sim.displayValue(cell.pos))); }
        awaitingAck = true; outstandingFrame = id; sentAt = Clock::now(); send(std::move(binary), true);
        const auto& trace = hub.sim.getTrace(); const auto first = hub.sim.traceDropped;
        Json edges = Json::array();
        if (traceCursor < first) traceCursor = first;
        if (traceCursor > first + trace.size()) traceCursor = first;
        for (std::uint64_t i = traceCursor - first; i < trace.size(); ++i) { const auto& e = trace[static_cast<std::size_t>(i)]; edges.push_back({e.probeId, e.tick, e.sequence, e.value}); }
        traceCursor = first + trace.size();
        if (!edges.empty() || full) sendJson({{"type", "trace"}, {"reset", full}, {"edges", edges}, {"dropped", first}});
    }
};
void Hub::demo() {
    sim.clear(); projectName = "脉冲与记忆 · 入门电路";
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
        if (running) {
            try {
                Tick target = sim.currentTick;
                if (speed == 0) target += 100000;
                else { fractionalTicks += std::min(elapsed, 0.1) * speed; auto ticks = static_cast<Tick>(fractionalTicks); fractionalTicks -= static_cast<double>(ticks); target += ticks; }
                sim.advanceTo(target, 8192, std::chrono::milliseconds(5)); if (sim.breakRequested) running = false;
            } catch (const std::exception& error) { running = false; sim.pauseReason = error.what(); }
        }
        auto measurementSeconds = std::chrono::duration<double>(now - measuredTime).count();
        if (measurementSeconds > 0.5) { measuredTps = sim.currentTick >= measuredTick ? static_cast<double>(sim.currentTick - measuredTick) / measurementSeconds : 0; measuredTick = sim.currentTick; measuredTime = now; }
        if (now - lastPublish >= std::chrono::milliseconds(40)) { publish(); lastPublish = now; }
        start();
    });
}
void Hub::publish(bool full) {
    std::vector<std::shared_ptr<Client>> active;
    for (const auto& weak : clients) if (auto c = weak.lock(); c && c->alive) active.push_back(c);
    if (!full && std::any_of(active.begin(), active.end(), [](const auto& c) { return c->awaitingAck; })) return;
    auto cells = full ? sim.world.cells() : sim.takeChanges();
    if (full) sim.takeChanges();
    for (auto& c : active) { if (full) c->traceCursor = sim.traceDropped; c->frame(cells, full, ++frameId); c->sendJson(status()); }
}
void Hub::command(const std::shared_ptr<Client>& client, const Json& message) {
    std::string cmd = message.at("cmd"); auto requestId = message.value("requestId", 0);
    if (cmd == "ack") { if (message.at("frameId") == client->outstandingFrame) client->awaitingAck = false; return; }
    Json result = Json::object(); bool full = false;
    if (cmd == "play") { sim.breakRequested = false; sim.pauseReason.clear(); if (!runStart) runStart = sim.clone(); running = true; fractionalTicks = 0; lastPump = Clock::now(); }
    else if (cmd == "pause") { running = false; }
    else if (cmd == "speed") { double next = message.at("value"); if (!std::isfinite(next) || next < 0 || next > 1000000) throw std::invalid_argument("无效运行速度"); speed = next; }
    else if (cmd == "step" || cmd == "stepEvent") { running = false; sim.breakRequested = false; sim.pauseReason.clear(); if (!runStart) runStart = sim.clone(); if (cmd == "stepEvent") sim.stepEvent(); else { int count = message.value("count", 1); if (count < 1 || count > 10000) throw std::invalid_argument("单步范围为 1–10000 gt"); sim.advanceTo(sim.currentTick + static_cast<Tick>(count), 100000, std::chrono::milliseconds(40)); } }
    else if (cmd == "inspect") { result = sim.inspect(message.at("pos").get<BlockPos>()); }
    else if (cmd == "save") { result = sim.saveProject(projectName, message.value("checkpoint", false)); }
    else if (cmd == "vcd") { result = {{"text", sim.exportVcd()}}; }
    else if (cmd == "probe") { result["id"] = sim.addProbe(message.at("pos").get<BlockPos>(), message.value("name", std::string()), message.value("mode", std::string("output"))); }
    else if (cmd == "removeProbe") sim.removeProbe(message.at("id"));
    else if (cmd == "configureProbe") sim.configureProbe(message.at("id"), message);
    else if (cmd == "clearTrace") { sim.clearTrace(); for (auto& weak : clients) if (auto c = weak.lock()) c->traceCursor = 0; full = true; }
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
            else if (cmd == "demo") { demo(); full = true; }
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
