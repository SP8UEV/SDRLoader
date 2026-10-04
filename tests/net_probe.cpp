// net_probe.cpp -- fetch one https address with the program's own downloader and
// report size and SHA-256. A manual check of the network path (TLS, redirects,
// proxies); not part of the automatic tests, which never touch the network.
//
//   sdi-net-probe <https-url> <file>
#include "../src/net.hpp"
#include "../src/util.hpp"

#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::printf("usage: sdi-net-probe <https-url> <file>\n");
        return 2;
    }
    std::wstring err;
    if (!sdi::net::download(argv[1], argv[2], 64ull << 20, nullptr, nullptr, err)) {
        std::printf("failed: %s\n", sdi::narrow(err).c_str());
        return 1;
    }
    uint64_t size = 0;
    std::string hash;
    sdi::fileSize(argv[2], size);
    sdi::sha256File(argv[2], hash);
    std::printf("ok: %llu bytes, sha256 %s\n", static_cast<unsigned long long>(size), hash.c_str());
    return 0;
}
