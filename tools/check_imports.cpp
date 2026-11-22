// ADR-0008: fails if the executable imports any network-capable DLL.
// Plain C++ PE reader, no Windows APIs, so it runs on the build host (Windows or Linux).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static const char* kForbidden[] = {
    "ws2_32.dll", "wsock32.dll", "winhttp.dll", "wininet.dll", "urlmon.dll", "iphlpapi.dll",
    "dnsapi.dll", "webservices.dll", "mswsock.dll", "rasapi32.dll", "winhttpcom.dll", "netapi32.dll",
};

template <class T> static bool rd(const std::vector<uint8_t>& b, size_t off, T& out) {
    if (off + sizeof(T) > b.size()) return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: check_imports <file.exe>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    uint32_t pe = 0;
    if (b.size() < 64 || b[0] != 'M' || b[1] != 'Z' || !rd(b, 0x3C, pe)) { std::fprintf(stderr, "not a PE file\n"); return 2; }
    uint16_t nsec = 0, optSize = 0, magic = 0;
    rd(b, pe + 6, nsec); rd(b, pe + 20, optSize);
    size_t opt = pe + 24; rd(b, opt, magic);
    size_t ddOff = opt + (magic == 0x20B ? 112 : 96);
    uint32_t impRva = 0; rd(b, ddOff + 8, impRva);
    size_t secTab = opt + optSize;
    auto rvaToOff = [&](uint32_t rva) -> size_t {
        for (int i = 0; i < nsec; ++i) {
            uint32_t va = 0, vs = 0, raw = 0, rs = 0;
            size_t s = secTab + i * 40;
            rd(b, s + 12, va); rd(b, s + 8, vs); rd(b, s + 20, raw); rd(b, s + 16, rs);
            if (rva >= va && rva < va + std::max(vs, rs)) return raw + (rva - va);
        }
        return 0;
    };
    std::vector<std::string> dlls;
    for (size_t d = rvaToOff(impRva); d && d + 20 <= b.size(); d += 20) {
        uint32_t nameRva = 0; rd(b, d + 12, nameRva);
        if (!nameRva) break;
        size_t n = rvaToOff(nameRva);
        std::string s;
        while (n < b.size() && b[n]) s += char(std::tolower(b[n++]));
        dlls.push_back(s);
    }
    int bad = 0;
    for (auto& s : dlls) {
        bool forbidden = false;
        for (auto* fb : kForbidden) if (s == fb) forbidden = true;
        std::printf("  %s %s\n", forbidden ? "FORBIDDEN" : "ok       ", s.c_str());
        bad += forbidden;
    }
    if (bad) { std::fprintf(stderr, "check_imports: %d forbidden network DLL(s) imported\n", bad); return 1; }
    std::printf("check_imports: %zu DLLs, no network libraries\n", dlls.size());
    return 0;
}
