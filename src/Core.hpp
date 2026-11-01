#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <random>

namespace mm {
inline constexpr std::uint64_t MaxBytes = 64ULL * 1024 * 1024;
inline int stepIndex(int current, int count, bool forward) {
    if (count <= 0) return -1;
    if (current < 0 || current >= count) return forward ? 0 : count - 1;
    return (current + (forward ? 1 : count - 1)) % count;
}
inline int randomIndex(int current, int count, std::mt19937& rng) {
    if (count <= 0) return -1;
    if (count == 1) return 0;
    if (current < 0 || current >= count)
        return std::uniform_int_distribution<int>(0, count - 1)(rng);
    int i = std::uniform_int_distribution<int>(0, count - 2)(rng);
    return i >= current ? i + 1 : i;
}
inline bool safeMediaName(std::string_view s) {
    if (s.empty() || s.front() == '.' || s.find("..") != s.npos) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    auto p = s.rfind('.');
    if (p == s.npos) return false;
    auto ext = s.substr(p);
    return ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac";
}
inline std::string audioExtension(std::span<unsigned char const> b) {
    auto at = [&](std::size_t p, std::string_view s) {
        if (b.size() < p + s.size()) return false;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (b[p + i] != static_cast<unsigned char>(s[i])) return false;
        return true;
    };
    if (at(0, "OggS")) return ".ogg";
    if (at(0, "fLaC")) return ".flac";
    if (at(0, "RIFF") && at(8, "WAVE")) return ".wav";
    if (at(0, "ID3")) return ".mp3";
    // MPEG audio sync; reject reserved MPEG version, layer, bitrate and sample rate.
    if (b.size() >= 4 && b[0] == 0xff && (b[1] & 0xe0) == 0xe0 &&
        (b[1] & 0x18) != 0x08 && (b[1] & 6) != 0 &&
        (b[2] & 0xf0) != 0xf0 && (b[2] & 0xf0) != 0 && (b[2] & 0x0c) != 0x0c)
        return ".mp3";
    return {};
}
inline std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
    while (!s.empty() && ws(s.back())) s.pop_back();
    auto it = std::find_if_not(s.begin(), s.end(), ws);
    s.erase(s.begin(), it);
    return s;
}
inline bool isHttps(std::string_view s) {
    if (s.size() <= 8 || s.substr(0, 8) != "https://") return false;
    auto end = s.find_first_of("/?#", 8);
    auto host = s.substr(8, end == s.npos ? s.size() - 8 : end - 8);
    if (host.empty() || host.find('@') != host.npos) return false;
    for (unsigned char c : s) if (c <= 32 || c == 127 || c == '\\') return false;
    return true;
}
// GD bitmap fonts do not reliably contain Cyrillic. Persist UTF-8 names unchanged,
// but transliterate Russian names for display instead of showing empty glyphs.
inline std::string displayName(std::string_view s) {
    static constexpr std::array<std::string_view, 32> lower = {
        "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p",
        "r","s","t","u","f","kh","ts","ch","sh","shch","","y","","e","yu","ya"
    };
    std::string out;
    for (std::size_t i = 0; i < s.size() && out.size() < 100;) {
        auto c = static_cast<unsigned char>(s[i++]);
        std::uint32_t cp = c;
        if (c >= 0xc2 && c <= 0xdf && i < s.size()) {
            auto d = static_cast<unsigned char>(s[i]);
            if ((d & 0xc0) != 0x80) { out += '?'; continue; }
            ++i; cp = ((c & 31) << 6) | (d & 63);
        } else if (c >= 128) {
            while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) ++i;
            out += '?'; continue;
        }
        if (cp >= 0x410 && cp <= 0x44f) {
            bool upper = cp < 0x430;
            auto part = std::string(lower[cp - (upper ? 0x410 : 0x430)]);
            if (upper && !part.empty()) part[0] -= 32;
            out += part;
        } else if (cp == 0x401 || cp == 0x451) out += cp == 0x401 ? "Yo" : "yo";
        else if (cp >= 32 && cp <= 126) out += static_cast<char>(cp);
        else out += '?';
    }
    out = trim(out);
    return out.empty() ? "Track" : out;
}
}
