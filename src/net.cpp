#include "net.hpp"

#include "i18n.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cwchar>
#include <string>
#include <vector>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace sdi::net {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

std::wstring userAgent() { return L"SDRLoader/" SDI_VERSION_W; }

}  // namespace

bool download(const std::wstring& url, const std::wstring& dest, uint64_t maxBytes, const Progress& progress,
              const std::atomic<bool>* cancel, std::wstring& err) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    uc.dwExtraInfoLength = 1;  // keep the query string with the path
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        err = tr(L"only https:// addresses are fetched", L"pobierane są tylko adresy https://");
        return false;
    }
    std::wstring object(path, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    Handle session(WinHttpOpen(userAgent().c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)  // before Windows 8.1 there is no automatic proxy
        session.h = WinHttpOpen(userAgent().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        err = lastErrorText(GetLastError());
        return false;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // a Windows without TLS 1.3
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 60000);

    Handle connection(WinHttpConnect(session.h, host, uc.nPort, 0));
    if (!connection) {
        err = lastErrorText(GetLastError());
        return false;
    }
    Handle request(WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        err = lastErrorText(GetLastError());
        return false;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        err = lastErrorText(GetLastError());
        return false;
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        wchar_t b[96];
        swprintf(b, 96, L"%ls %lu", tr(L"the server answered HTTP", L"serwer odpowiedział HTTP"),
                 static_cast<unsigned long>(status));
        err = b;
        return false;
    }
    uint64_t total = 0;
    wchar_t cl[32] = {};
    len = sizeof cl;
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &len,
                            WINHTTP_NO_HEADER_INDEX))
        total = wcstoull(cl, nullptr, 10);
    if (total > maxBytes) {
        err = tr(L"the file is larger than expected -- not fetched", L"plik jest większy niż oczekiwano -- nie pobrano");
        return false;
    }

    const std::wstring part = dest + L".part";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = lastErrorText(GetLastError());
        return false;
    }
    std::vector<char> buf(1 << 16);
    uint64_t got = 0;
    bool ok = true;
    for (;;) {
        if (cancel && cancel->load()) {
            err = tr(L"cancelled", L"anulowano");
            ok = false;
            break;
        }
        DWORD n = 0;
        if (!WinHttpReadData(request.h, buf.data(), static_cast<DWORD>(buf.size()), &n)) {
            err = lastErrorText(GetLastError());
            ok = false;
            break;
        }
        if (n == 0) break;
        got += n;
        if (got > maxBytes) {
            err = tr(L"the file is larger than expected -- not used", L"plik jest większy niż oczekiwano -- nie użyto");
            ok = false;
            break;
        }
        DWORD put = 0;
        if (!WriteFile(f, buf.data(), n, &put, nullptr) || put != n) {
            err = lastErrorText(GetLastError());
            ok = false;
            break;
        }
        if (progress) progress(got, total);
    }
    CloseHandle(f);
    if (ok && total && got != total) {
        err = tr(L"the download was cut short", L"pobieranie zostało przerwane");
        ok = false;
    }
    if (ok && !MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = lastErrorText(GetLastError());
        ok = false;
    }
    if (!ok) DeleteFileW(part.c_str());
    return ok;
}

}  // namespace sdi::net
