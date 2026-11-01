#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
namespace mm {
struct JobState {
    std::atomic_bool alive{true};
    std::atomic_bool cancel{false};
    std::atomic<std::uint64_t> bytes{0};
};
struct TransferResult {
    std::filesystem::path file;
    std::string error;
};
TransferResult copyAudio(std::filesystem::path const& source,
                         std::filesystem::path const& temporary,
                         std::shared_ptr<JobState> const& state);
TransferResult downloadAudio(std::string const& url,
                             std::filesystem::path const& temporary,
                             std::shared_ptr<JobState> const& state);
}
