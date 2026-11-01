#include "Transfer.hpp"
#include "Core.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace mm {
namespace {
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v) : h(v) {
        if (!h) throw std::runtime_error("Network initialization failed (" + std::to_string(GetLastError()) + ").");
    }
    ~Handle() { WinHttpCloseHandle(h); }
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
};
void check(BOOL ok) {
    if (!ok) throw std::runtime_error("Network error " + std::to_string(GetLastError()) + ".");
}
void checkCancelled(std::shared_ptr<JobState> const& s) {
    if (s->cancel || !s->alive) throw std::runtime_error("Cancelled.");
}
std::wstring widen(std::string const& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid URL encoding.");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
TransferResult fail(std::filesystem::path const& p, std::exception const& e) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return {{}, e.what()};
}
}
TransferResult copyAudio(std::filesystem::path const& source,
                         std::filesystem::path const& temporary,
                         std::shared_ptr<JobState> const& state) {
    try {
        checkCancelled(state);
        auto size = std::filesystem::file_size(source);
        if (size == 0 || size > MaxBytes) throw std::runtime_error("File must be between 1 byte and 64 MiB.");
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!in || !out) throw std::runtime_error("Cannot open the source or save folder.");
        std::array<char, 65536> buffer;
        std::uint64_t total = 0;
        while (in) {
            checkCancelled(state);
            in.read(buffer.data(), buffer.size());
            auto count = in.gcount();
            if (!count) break;
            total += static_cast<std::uint64_t>(count);
            if (total > MaxBytes) throw std::runtime_error("File is larger than 64 MiB.");
            out.write(buffer.data(), count);
            if (!out) throw std::runtime_error("Cannot save audio. Check free disk space.");
            state->bytes = total;
        }
        if (in.bad() || total != size) throw std::runtime_error("Source file changed or could not be read.");
        out.close();
        if (!out) throw std::runtime_error("Could not finish writing the file.");
        return {temporary, {}};
    } catch (std::exception const& e) { return fail(temporary, e); }
}
TransferResult downloadAudio(std::string const& url,
                             std::filesystem::path const& temporary,
                             std::shared_ptr<JobState> const& state) {
    try {
        if (!isHttps(url)) throw std::runtime_error("Use a direct HTTPS audio URL without a login/password.");
        checkCancelled(state);
        auto wide = widen(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        check(WinHttpCrackUrl(wide.c_str(), 0, 0, &parts));
        if (parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength)
            throw std::runtime_error("Only HTTPS links are supported.");
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (auto p = path.find(L'#'); p != path.npos) path.resize(p);
        if (path.empty()) path = L"/";
        Handle session(WinHttpOpen(L"MenuMusicPlayer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        check(WinHttpSetTimeouts(session.h, 10000, 10000, 10000, 10000));
        Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
        Handle request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        check(WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)));
        DWORD redirects = 5;
        check(WinHttpSetOption(request.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects)));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        check(WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
        check(WinHttpReceiveResponse(request.h, nullptr));
        DWORD status = 0, length = sizeof(status);
        check(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX));
        if (status != 200) throw std::runtime_error("Server returned HTTP " + std::to_string(status) + ".");
        // Stream to disk, never buffer the complete response in RAM. Enforce the
        // cap on actual bytes even when Content-Length is missing or dishonest.
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write to the mod save folder.");
        std::array<char, 65536> buffer;
        std::uint64_t total = 0;
        for (;;) {
            checkCancelled(state);
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("Download timed out. Try a faster direct link.");
            DWORD read = 0;
            check(WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read));
            if (!read) break;
            total += read;
            if (total > MaxBytes) throw std::runtime_error("Download is larger than 64 MiB.");
            out.write(buffer.data(), read);
            if (!out) throw std::runtime_error("Cannot save audio. Check free disk space.");
            state->bytes = total;
        }
        if (!total) throw std::runtime_error("Server returned an empty file.");
        out.close();
        if (!out) throw std::runtime_error("Could not finish writing the file.");
        return {temporary, {}};
    } catch (std::exception const& e) { return fail(temporary, e); }
}
}
