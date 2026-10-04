// net.hpp -- HTTPS downloads through WinHTTP (Windows' own HTTP stack, so the
// system's proxy settings and certificate store apply).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace sdi::net {

using Progress = std::function<void(uint64_t got, uint64_t total)>;  // total 0 = unknown

// Fetches `url` (https only; redirects followed, never to plain http) into
// `dest`, through `dest`.part. At most `maxBytes`.
bool download(const std::wstring& url, const std::wstring& dest, uint64_t maxBytes, const Progress& progress,
              const std::atomic<bool>* cancel, std::wstring& err);

}  // namespace sdi::net
