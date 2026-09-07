#pragma once
#include "simulator/simulator.hpp"
#include "simulator/vmcbFormat.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <random>

namespace simulatorServer {
inline std::string randomId() {
    static constexpr char hex[] = "0123456789abcdef"; std::random_device random; std::string value;
    for (unsigned i = 0; i < 8; ++i) { auto bits = random(); for (unsigned j = 0; j < 8; ++j) { value.push_back(hex[bits & 15]); bits >>= 4; } } return value;
}
struct FileJob {
    std::string id{randomId()}, operation;
    std::filesystem::path directory, path;
    simulator::ProjectInfo info;
    std::atomic<bool> cancelled{};
    std::mutex mutex;
    std::string state{"working"}, stage{"准备文件"}, error;
    std::uint64_t done{}, total{}, revision{};
    std::chrono::steady_clock::time_point created{std::chrono::steady_clock::now()};
    explicit FileJob(std::string kind) : operation(std::move(kind)) {
        directory = std::filesystem::temp_directory_path() / ("verimc-" + id);
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("无法创建工程临时目录");
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all); path = directory / "project.tmp";
    }
    ~FileJob() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    void progress(const std::string& phase, std::uint64_t completed, std::uint64_t size) {
        if (cancelled.load()) throw std::runtime_error("文件操作已取消");
        std::lock_guard lock(mutex); stage = phase; done = completed; total = size;
    }
    void finish(std::string message = {}) {
        std::lock_guard lock(mutex); error = std::move(message); state = cancelled.load() ? "cancelled" : error.empty() ? "done" : "failed";
        if (!error.empty()) { std::error_code ignored; std::filesystem::remove(path, ignored); }
    }
    simulator::Json status() {
        std::lock_guard lock(mutex);
        return {{"id",id},{"operation",operation},{"state",state},{"stage",stage},{"done",std::to_string(done)},{"total",std::to_string(total)},{"error",error}};
    }
    simulator::ProjectFileOptions options() {
        simulator::ProjectFileOptions result; result.progress = [this](const auto& phase, auto completed, auto size) { progress(phase, completed, size); }; return result;
    }
};
}
