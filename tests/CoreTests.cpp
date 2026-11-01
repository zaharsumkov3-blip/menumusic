#include "../src/Core.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace mm;
int main() {
    assert(stepIndex(-1, 0, true) == -1);
    assert(stepIndex(-1, 3, true) == 0);
    assert(stepIndex(-1, 3, false) == 2);
    assert(stepIndex(0, 3, false) == 2);
    assert(stepIndex(2, 3, true) == 0);
    assert(stepIndex(0, 1, true) == 0);
    assert(stepIndex(99, 3, false) == 2);
    std::mt19937 rng(123);
    assert(randomIndex(-1, 0, rng) == -1);
    assert(randomIndex(0, 1, rng) == 0);
    for (int n = 2; n < 20; ++n) {
        for (int current = -1; current < n; ++current) {
            for (int k = 0; k < 100; ++k) {
                int next = randomIndex(current, n, rng);
                assert(next >= 0 && next < n && next != current);
            }
        }
    }
    assert(safeMediaName("track_123_1.mp3"));
    assert(safeMediaName("test.flac"));
    for (auto s : {"", "../bad.mp3", "a/track.ogg", "C:\\test.wav", ".mp3", "a..mp3", "file.exe", "file.mp3:evil"})
        assert(!safeMediaName(s));
    auto ext = [](std::initializer_list<unsigned char> list) {
        std::vector<unsigned char> b(list);
        return audioExtension(b);
    };
    assert(ext({'I','D','3',4}) == ".mp3");
    assert(ext({0xff,0xfb,0x90,0}) == ".mp3");
    assert(ext({0xff,0xff,0xff,0xff}).empty());
    assert(ext({'O','g','g','S'}) == ".ogg");
    assert(ext({'f','L','a','C'}) == ".flac");
    assert(ext({'R','I','F','F',0,0,0,0,'W','A','V','E'}) == ".wav");
    assert(ext({'R','I','F','F'}).empty());
    assert(ext({'<','h','t','m','l','>'}).empty());
    assert(ext({}).empty());
    assert(trim(" \t song \r\n") == "song");
    assert(trim(" \n\t").empty());
    assert(isHttps("https://example.com/song.mp3?token=123"));
    assert(isHttps("https://example.com"));
    for (auto s : {"http://example.com", "file:///C:/a.mp3", "https://", "https://user:pass@host/song", "https://host/a\r\nb", "https:///song", "https://host\\a"})
        assert(!isHttps(s));
    assert(displayName("Hello") == "Hello");
    assert(displayName("Песня Ёжика") == "Pesnya Yozhika");
    assert(displayName("Щука и Чай") == "Shchuka i Chay");
    assert(displayName("") == "Track");
    assert(!displayName("🎵").empty());
    assert(displayName(std::string(1000, 'A')).size() <= 100);
    std::cout << "All core tests passed (selection, shuffle, paths, file signatures, URLs, display names).\n";
}
