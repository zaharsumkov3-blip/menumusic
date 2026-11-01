#include "Library.hpp"
#include "Core.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
using namespace geode::prelude;
namespace mm {
std::string utf8(std::filesystem::path const& p) {
    auto s = p.u8string();
    return {reinterpret_cast<char const*>(s.data()), s.size()};
}
Library& Library::get() { static Library value; return value; }
Library::Library() {
    m_folder = Mod::get()->getSaveDir() / "tracks";
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    if (ec) log::warn("Menu Music: could not create tracks directory: {}", ec.message());
    auto files = Mod::get()->getSavedValue<std::vector<std::string>>("files");
    auto names = Mod::get()->getSavedValue<std::vector<std::string>>("titles");
    auto current = Mod::get()->getSavedValue<int>("current", -1);
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!safeMediaName(files[i])) continue;
        ec.clear();
        if (!std::filesystem::is_regular_file(m_folder / files[i], ec)) continue;
        if (static_cast<int>(i) == current) m_current = static_cast<int>(m_tracks.size());
        m_tracks.push_back({files[i], i < names.size() ? names[i] : files[i]});
    }
    // Incomplete transfers from a previous crash are safe to discard.
    ec.clear();
    for (std::filesystem::directory_iterator it(m_folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".part") {
            std::error_code ignored;
            std::filesystem::remove(it->path(), ignored);
        }
    }
}
void Library::writeSettings() {
    std::vector<std::string> files, names;
    for (auto const& t : m_tracks) { files.push_back(t.file); names.push_back(t.title); }
    Mod::get()->setSavedValue("files", files);
    Mod::get()->setSavedValue("titles", names);
    Mod::get()->setSavedValue("current", m_current);
}
std::string Library::save() {
    writeSettings();
    auto result = Mod::get()->saveData();
    if (result.isErr()) return "Could not save the playlist. Check disk space and permissions.";
    return {};
}
std::string Library::selectedPath() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_tracks.size())) return {};
    std::error_code ec;
    auto p = m_folder / m_tracks[m_current].file;
    if (!std::filesystem::is_regular_file(p, ec)) return {};
    return utf8(p);
}
std::string Library::currentTitle() const {
    if (selectedPath().empty()) return "Default menu music";
    return displayName(m_tracks[m_current].title);
}
std::filesystem::path Library::temporaryPath() {
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    if (ec) throw std::runtime_error("Cannot create the audio save folder.");
    static unsigned serial = 0;
    auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return m_folder / ("track_" + std::to_string(ticks) + "_" + std::to_string(++serial) + ".part");
}
void Library::play() {
    // The popup is only exposed from the main menu. Never start a menu track
    // from an async callback while a real level is running.
    if (GameManager::sharedState()->m_playLayer) return;
    auto path = selectedPath();
    if (path.empty()) {
        FMODAudioEngine::sharedEngine()->stopMusic(0);
        GameManager::sharedState()->playMenuMusic();
    }
    else FMODAudioEngine::sharedEngine()->playMusic(path, true, 0.0f, 0);
}
std::string Library::select(int index) {
    if (index < -1 || index >= static_cast<int>(m_tracks.size())) return "Track not found.";
    if (index >= 0) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(m_folder / m_tracks[index].file, ec))
            return "Audio file is missing. Remove it and import it again.";
    }
    auto old = m_current;
    m_current = index;
    if (auto error = save(); !error.empty()) {
        m_current = old; writeSettings(); return error;
    }
    ++m_revision;
    play();
    return {};
}
std::string Library::step(bool forward) {
    if (m_tracks.empty()) return "Your playlist is empty. Add a song first.";
    return select(stepIndex(m_current, static_cast<int>(m_tracks.size()), forward));
}
std::string Library::shuffle() {
    if (m_tracks.empty()) return "Your playlist is empty. Add a song first.";
    return select(randomIndex(m_current, static_cast<int>(m_tracks.size()), m_rng));
}
std::string Library::remove(std::string const& file) {
    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](auto const& t) { return t.file == file; });
    if (it == m_tracks.end()) return "Track no longer exists.";
    int index = static_cast<int>(it - m_tracks.begin());
    auto oldTracks = m_tracks;
    auto oldCurrent = m_current;
    m_tracks.erase(it);
    if (m_current == index) m_current = -1;
    else if (m_current > index) --m_current;
    if (auto error = save(); !error.empty()) {
        m_tracks = std::move(oldTracks); m_current = oldCurrent; writeSettings(); return error;
    }
    ++m_revision;
    if (oldCurrent == index) play();
    // Keep managed audio on disk: FMOD may cache/open the stream until exit.
    // Removing an item never deletes the user's original file.
    return {};
}
std::string Library::importTemporary(std::filesystem::path const& path, std::string title) {
    std::filesystem::path final;
    try {
        std::array<unsigned char, 16> header{};
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot read imported audio.");
        in.read(reinterpret_cast<char*>(header.data()), header.size());
        auto ext = audioExtension({header.data(), static_cast<std::size_t>(in.gcount())});
        in.close();
        if (ext.empty()) throw std::runtime_error("Not a supported MP3, OGG, WAV or FLAC file. Use a direct audio link, not a webpage.");
        final = path; final.replace_extension(ext);
        std::filesystem::rename(path, final);
        auto engine = FMODAudioEngine::sharedEngine();
        FMOD::Sound* sound = nullptr;
        if (!engine || !engine->m_system) throw std::runtime_error("Audio engine is not ready. Try again from the main menu.");
        auto result = engine->m_system->createStream(utf8(final).c_str(), FMOD_DEFAULT, nullptr, &sound);
        if (result != FMOD_OK || !sound) throw std::runtime_error("FMOD cannot decode this audio file.");
        unsigned int ms = 0;
        auto lengthResult = sound->getLength(&ms, FMOD_TIMEUNIT_MS);
        sound->release();
        if (lengthResult != FMOD_OK || ms == 0) throw std::runtime_error("Audio has no readable duration.");
        title = trim(title);
        if (title.empty()) title = "Downloaded track";
        auto old = m_current;
        m_tracks.push_back({utf8(final.filename()), title});
        m_current = static_cast<int>(m_tracks.size()) - 1;
        if (auto error = save(); !error.empty()) {
            m_tracks.pop_back(); m_current = old; writeSettings();
            throw std::runtime_error(error);
        }
        ++m_revision;
        play();
        return {};
    } catch (std::exception const& e) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        if (!final.empty()) std::filesystem::remove(final, ignored);
        return e.what();
    }
}
}
