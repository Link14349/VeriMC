#pragma once
#include "world.hpp"
#include <functional>
#include <iterator>
#include <span>

namespace simulator {
// A table is consumed one row at a time. Binary readers own at most one page
// of decoded records; the simulator never assembles a whole-project JSON DOM.
class ProjectRows {
public:
    explicit ProjectRows(std::function<bool(Json&)> nextRow) : next(std::move(nextRow)) {}
    struct Iterator {
        std::function<bool(Json&)>* next;
        Json value;
        bool done{};
        const Json& operator*() const { return value; }
        Iterator& operator++() { done = !(*next)(value); return *this; }
        bool operator!=(std::default_sentinel_t) const { return !done; }
    };
    Iterator begin() { Iterator it{&next, {}}; ++it; return it; }
    std::default_sentinel_t end() const { return {}; }
private:
    std::function<bool(Json&)> next;
};
class ProjectSource {
public:
    virtual ~ProjectSource() = default;
    virtual const Json& metadata() const = 0;
    virtual void loadWorld(World& world) = 0;
    virtual ProjectRows rows(const std::string& table) = 0;
    virtual void check() const {}
    virtual bool restoreBudgets() const { return false; }
};
class ProjectSink {
public:
    virtual ~ProjectSink() = default;
    virtual void begin(const Json& metadata, const World& world, std::span<const StateId> additionalStates) = 0;
    virtual void beginTable(const std::string& table) = 0;
    virtual void row(Json value) = 0;
    virtual void endTable() = 0;
    virtual void finish() = 0;
    virtual void check() const {}
};
}
