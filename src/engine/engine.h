// Runs the collectors, keeps history and produces the state the UI and reports show.
#pragma once

#include <memory>
#include <string>

namespace why {

struct EngineOptions {
    std::string databasePath = ":memory:";
    std::string mode = "investigation";  // live or investigation.
    int historyDays = 180;
};

class Engine {
public:
    explicit Engine(EngineOptions options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Keeps watching in the background until stop().
    void start();
    void stop();
    // Runs one full scan on the calling thread. Used by the headless collection.
    void scanOnce();

    std::shared_ptr<const std::string> state() const;
    std::uint64_t revision() const;
    // Blocks until the revision moves past known, or the timeout passes. Returns the current revision.
    std::uint64_t waitForChange(std::uint64_t known, int timeoutMs) const;
    // action is tried_fix or reopen. Returns false for an unknown case or action.
    bool applyCaseAction(const std::string& key, const std::string& action);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Converts a state into a report: marks it as such and removes personal details.
std::string makeReport(const std::string& stateJson);

}  // namespace why
