#pragma once
#include <filesystem>
#include <random>
#include <string>
#include <vector>
namespace mm {
std::string utf8(std::filesystem::path const& path);
struct Track { std::string file; std::string title; };
class Library {
    std::vector<Track> m_tracks;
    int m_current = -1;
    std::size_t m_revision = 0;
    std::filesystem::path m_folder;
    std::mt19937 m_rng{std::random_device{}()};
    void writeSettings();
    std::string save();
    Library();
public:
    static Library& get();
    std::vector<Track> const& tracks() const { return m_tracks; }
    int current() const { return m_current; }
    std::size_t revision() const { return m_revision; }
    std::string selectedPath() const;
    std::string currentTitle() const;
    std::filesystem::path temporaryPath();
    std::string select(int index);
    std::string step(bool forward);
    std::string shuffle();
    std::string remove(std::string const& file);
    std::string importTemporary(std::filesystem::path const& path, std::string title);
    void play();
};
}
