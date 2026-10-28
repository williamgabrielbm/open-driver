// Language: C++20; file: microsoft_catalog.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <wininet.h>

#ifdef OPEN_DRIVER_HAS_CURL_OPENSSL
#include <curl/curl.h>
#endif

#include "microsoft_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <system_error>
#include <thread>

namespace open_driver {
namespace {

constexpr wchar_t browser_user_agent_w[] =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36 Edg/141.0.0.0";
constexpr char browser_user_agent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36 Edg/141.0.0.0";

class InternetHandle final {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) : value_(value) {}
    ~InternetHandle() { if (value_ != nullptr) WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET get() const { return value_; }

private:
    HINTERNET value_{};
};

class WinInetHandle final {
public:
    WinInetHandle() = default;
    explicit WinInetHandle(HINTERNET value) : value_(value) {}
    ~WinInetHandle() { if (value_ != nullptr) InternetCloseHandle(value_); }
    WinInetHandle(const WinInetHandle&) = delete;
    WinInetHandle& operator=(const WinInetHandle&) = delete;
    [[nodiscard]] HINTERNET get() const { return value_; }

private:
    HINTERNET value_{};
};

std::string windows_error_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (size == 0 || buffer == nullptr) return "Windows error " + std::to_string(error);
    std::wstring wide(buffer, size);
    LocalFree(buffer);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ')) {
        wide.pop_back();
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return "Windows error " + std::to_string(error);
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

CatalogError make_error(std::string operation, DWORD error) {
    return {.operation = std::move(operation), .windows_error = error,
            .message = windows_error_message(error)};
}

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::string trim(std::string value) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::string html_decode(std::string value) {
    const std::array<std::pair<const char*, const char*>, 6> entities{{
        {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "},
    }};
    for (const auto& [encoded, decoded] : entities) {
        std::size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, std::strlen(encoded), decoded);
            position += std::strlen(decoded);
        }
    }
    return value;
}

std::string strip_html(const std::string& input) {
    std::string output = std::regex_replace(input, std::regex("<[^>]*>"), " ");
    output = html_decode(output);
    output = std::regex_replace(output, std::regex("[\\r\\n\\t ]+"), " ");
    return trim(output);
}

std::string url_encode(const std::string& value) {
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            output << static_cast<char>(character);
        } else {
            output << '%' << std::setw(2) << static_cast<unsigned int>(character);
        }
    }
    return output.str();
}

struct HttpResult {
    std::vector<unsigned char> body;
    DWORD status{};
    std::optional<CatalogError> error;
};

HttpResult http_request_winhttp(
    const std::string& url,
    const wchar_t* method,
    const std::string& body = {},
    const wchar_t* content_type = nullptr) {
    HttpResult result;
    const auto wide_url = wide_from_utf8(url);
    if (wide_url.empty()) {
        result.error = make_error("URL encoding", ERROR_INVALID_NAME);
        return result;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
        result.error = make_error("WinHttpCrackUrl", GetLastError());
        return result;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        result.error = make_error("URL scheme", ERROR_INVALID_NAME);
        return result;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    InternetHandle session(WinHttpOpen(
        browser_user_agent_w, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.get() == nullptr) {
        result.error = make_error("WinHttpOpen", GetLastError());
        return result;
    }
    WinHttpSetTimeouts(session.get(), 15000, 15000, 30000, 30000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    secure_protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(
        session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS,
        &secure_protocols, sizeof(secure_protocols));

    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (connection.get() == nullptr) {
        result.error = make_error("WinHttpConnect", GetLastError());
        return result;
    }
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(
        connection.get(), method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (request.get() == nullptr) {
        result.error = make_error("WinHttpOpenRequest", GetLastError());
        return result;
    }
    DWORD auto_logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_LOW;
    WinHttpSetOption(
        request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY,
        &auto_logon, sizeof(auto_logon));
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP |
                          WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(
        request.get(), WINHTTP_OPTION_DECOMPRESSION,
        &decompression, sizeof(decompression));

    std::wstring headers;
    if (content_type != nullptr) headers = std::wstring(L"Content-Type: ") + content_type + L"\r\n";
    void* request_body = body.empty() ? WINHTTP_NO_REQUEST_DATA
                                      : const_cast<char*>(body.data());
    if (!WinHttpSendRequest(
            request.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(-1), request_body,
            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        result.error = make_error("WinHttpSendRequest", GetLastError());
        return result;
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        result.error = make_error("WinHttpReceiveResponse", GetLastError());
        return result;
    }

    DWORD status_size = sizeof(result.status);
    WinHttpQueryHeaders(
        request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (result.status < 200 || result.status >= 300) {
        result.error = make_error("HTTP status " + std::to_string(result.status), ERROR_BAD_NET_RESP);
        return result;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            result.error = make_error("WinHttpQueryDataAvailable", GetLastError());
            return result;
        }
        if (available == 0) break;
        const auto start = result.body.size();
        result.body.resize(start + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), result.body.data() + start, available, &read)) {
            result.error = make_error("WinHttpReadData", GetLastError());
            return result;
        }
        result.body.resize(start + read);
    }
    return result;
}

HttpResult http_request_wininet(
    const std::string& url,
    const wchar_t* method,
    const std::string& body = {},
    const wchar_t* content_type = nullptr) {
    HttpResult result;
    const auto wide_url = wide_from_utf8(url);
    if (wide_url.empty()) {
        result.error = make_error("WinINet URL encoding", ERROR_INVALID_NAME);
        return result;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
        result.error = make_error("WinINet URL parsing", GetLastError());
        return result;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        result.error = make_error("WinINet URL scheme", ERROR_INVALID_NAME);
        return result;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    WinInetHandle session(InternetOpenW(
        browser_user_agent_w, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
    if (session.get() == nullptr) {
        result.error = make_error("InternetOpenW", GetLastError());
        return result;
    }
    DWORD timeout = 30000;
    InternetSetOptionW(session.get(), INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session.get(), INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session.get(), INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    WinInetHandle connection(InternetConnectW(
        session.get(), host.c_str(), parts.nPort, nullptr, nullptr,
        INTERNET_SERVICE_HTTP, 0, 0));
    if (connection.get() == nullptr) {
        result.error = make_error("InternetConnectW", GetLastError());
        return result;
    }
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_KEEP_CONNECTION;
    if (parts.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
    const wchar_t* accept_types[] = {L"*/*", nullptr};
    WinInetHandle request(HttpOpenRequestW(
        connection.get(), method, path.c_str(), nullptr, nullptr,
        accept_types, flags, 0));
    if (request.get() == nullptr) {
        result.error = make_error("HttpOpenRequestW", GetLastError());
        return result;
    }

    std::wstring headers;
    if (content_type != nullptr) headers = std::wstring(L"Content-Type: ") + content_type + L"\r\n";
    void* request_body = body.empty() ? nullptr : const_cast<char*>(body.data());
    if (!HttpSendRequestW(
            request.get(), headers.empty() ? nullptr : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(-1),
            request_body, static_cast<DWORD>(body.size()))) {
        result.error = make_error("HttpSendRequestW", GetLastError());
        return result;
    }
    DWORD status_size = sizeof(result.status);
    if (!HttpQueryInfoW(
            request.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &result.status, &status_size, nullptr)) {
        result.error = make_error("HttpQueryInfoW", GetLastError());
        return result;
    }
    if (result.status < 200 || result.status >= 300) {
        result.error = make_error(
            "WinINet HTTP status " + std::to_string(result.status), ERROR_BAD_NET_RESP);
        return result;
    }
    std::array<unsigned char, 64 * 1024> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!InternetReadFile(
                request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            result.error = make_error("InternetReadFile", GetLastError());
            return result;
        }
        if (read == 0) break;
        result.body.insert(result.body.end(), buffer.begin(), buffer.begin() + read);
    }
    return result;
}

#ifdef OPEN_DRIVER_HAS_CURL_OPENSSL
std::size_t curl_write_body(
    char* data, std::size_t size, std::size_t count, void* context) {
    const auto bytes = size * count;
    auto* body = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = reinterpret_cast<unsigned char*>(data);
    body->insert(body->end(), begin, begin + bytes);
    return bytes;
}

void append_certificate_store(
    const wchar_t* store_name, DWORD location, std::string& pem_bundle) {
    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0, location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
        store_name);
    if (store == nullptr) return;
    PCCERT_CONTEXT certificate = nullptr;
    while ((certificate = CertEnumCertificatesInStore(store, certificate)) != nullptr) {
        DWORD required = 0;
        if (!CryptBinaryToStringA(
                certificate->pbCertEncoded, certificate->cbCertEncoded,
                CRYPT_STRING_BASE64HEADER, nullptr, &required) || required == 0) {
            continue;
        }
        std::string encoded(required, '\0');
        if (!CryptBinaryToStringA(
                certificate->pbCertEncoded, certificate->cbCertEncoded,
                CRYPT_STRING_BASE64HEADER, encoded.data(), &required)) {
            continue;
        }
        if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
        pem_bundle += encoded;
        if (!pem_bundle.ends_with("\n")) pem_bundle += "\r\n";
    }
    CertCloseStore(store, 0);
}

std::string windows_certificate_bundle() {
    std::string result;
    for (const auto location : {
             CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        append_certificate_store(L"ROOT", location, result);
        append_certificate_store(L"CA", location, result);
    }
    return result;
}

HttpResult http_request_curl(
    const std::string& url,
    const wchar_t* method,
    const std::string& body = {},
    const wchar_t* content_type = nullptr) {
    HttpResult result;
    static const CURLcode initialization = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialization != CURLE_OK) {
        result.error = CatalogError{
            .operation = "curl_global_init",
            .windows_error = static_cast<DWORD>(initialization),
            .message = curl_easy_strerror(initialization),
        };
        return result;
    }
    const auto* version = curl_version_info(CURLVERSION_NOW);
    const std::string ssl_version =
        version != nullptr && version->ssl_version != nullptr ? version->ssl_version : "";
    if (ssl_version.find("OpenSSL") == std::string::npos) {
        result.error = CatalogError{
            .operation = "libcurl TLS backend",
            .windows_error = ERROR_NOT_SUPPORTED,
            .message = "expected OpenSSL, found " +
                (ssl_version.empty() ? std::string("unknown backend") : ssl_version),
        };
        return result;
    }

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) {
        result.error = CatalogError{
            .operation = "curl_easy_init",
            .windows_error = ERROR_NOT_ENOUGH_MEMORY,
            .message = "could not create request handle",
        };
        return result;
    }

    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, browser_user_agent);
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle.get(), CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    const auto certificate_bundle = windows_certificate_bundle();
    curl_blob certificate_blob{
        .data = const_cast<char*>(certificate_bundle.data()),
        .len = certificate_bundle.size(),
        .flags = CURL_BLOB_COPY,
    };
    if (!certificate_bundle.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_CAINFO_BLOB, &certificate_blob);
    }
    curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write_body);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &result.body);

    curl_slist* raw_headers = nullptr;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        nullptr, &curl_slist_free_all);
    if (content_type != nullptr) {
        const auto utf8_content_type = [&]() {
            const std::wstring value(content_type);
            if (value.empty()) return std::string{};
            const int required = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            std::string converted(static_cast<std::size_t>(required), '\0');
            if (required > 0) WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                converted.data(), required, nullptr, nullptr);
            return converted;
        }();
        raw_headers = curl_slist_append(
            raw_headers, ("Content-Type: " + utf8_content_type).c_str());
        if (raw_headers == nullptr) {
            result.error = CatalogError{
                .operation = "curl_slist_append",
                .windows_error = ERROR_NOT_ENOUGH_MEMORY,
                .message = "could not allocate the Content-Type header",
            };
            return result;
        }
        headers.reset(raw_headers);
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    }
    if (std::wcscmp(method, L"POST") == 0) {
        curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
    }

    const auto code = curl_easy_perform(handle.get());
    if (code != CURLE_OK) {
        result.error = CatalogError{
            .operation = "libcurl/OpenSSL request",
            .windows_error = static_cast<DWORD>(code),
            .message = error_buffer[0] != '\0' ? error_buffer.data()
                                                : curl_easy_strerror(code),
        };
        return result;
    }
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    result.status = static_cast<DWORD>(status);
    if (result.status < 200 || result.status >= 300) {
        result.error = CatalogError{
            .operation = "libcurl HTTP status " + std::to_string(result.status),
            .windows_error = ERROR_BAD_NET_RESP,
            .message = "server returned HTTP " + std::to_string(result.status),
        };
    }
    return result;
}
#endif

HttpResult http_request_once(
    const std::string& url,
    const wchar_t* method,
    const std::string& body = {},
    const wchar_t* content_type = nullptr) {
#ifdef OPEN_DRIVER_HAS_CURL_OPENSSL
    // Prefer the self-contained OpenSSL transport. Some Windows installations
    // can reach the host but have a broken Schannel security package, which
    // makes both WinHTTP and WinINet fail before the HTTP request is sent.
    auto openssl = http_request_curl(url, method, body, content_type);
    if (!openssl.error) return openssl;
#endif
    auto primary = http_request_winhttp(url, method, body, content_type);
    if (!primary.error) return primary;
    auto fallback = http_request_wininet(url, method, body, content_type);
    if (!fallback.error) return fallback;
#ifdef OPEN_DRIVER_HAS_CURL_OPENSSL
    const auto openssl_operation = openssl.error->operation;
    openssl.error->operation = "WinHTTP, WinINet, and libcurl/OpenSSL";
    openssl.error->message = "libcurl/OpenSSL " + openssl_operation + ": " +
                             openssl.error->message + "; WinHTTP " +
                             primary.error->operation + ": " + primary.error->message +
                             "; WinINet " + fallback.error->operation + ": " +
                             fallback.error->message;
    return openssl;
#else
    const auto fallback_operation = fallback.error->operation;
    fallback.error->operation = "WinHTTP and WinINet";
    fallback.error->message = "WinHTTP " + primary.error->operation + ": " +
                              primary.error->message + "; WinINet " +
                              fallback_operation + ": " + fallback.error->message;
    return fallback;
#endif
}

bool retryable_http_failure(const HttpResult& result) {
    if (!result.error) return false;
    if (result.status == 408 || result.status == 425 || result.status == 429) return true;
    if (result.status >= 500) return true;
    if (result.status != 0) return false;
    return result.error->windows_error != ERROR_INVALID_NAME &&
           result.error->windows_error != ERROR_INVALID_PARAMETER;
}

HttpResult http_request(
    const std::string& url,
    const wchar_t* method,
    const std::string& body = {},
    const wchar_t* content_type = nullptr) {
    constexpr std::array delays{std::chrono::milliseconds(250),
                                std::chrono::milliseconds(750)};
    HttpResult result;
    std::size_t attempts = 0;
    for (std::size_t attempt = 0; attempt <= delays.size(); ++attempt) {
        ++attempts;
        result = http_request_once(url, method, body, content_type);
        if (!retryable_http_failure(result) || attempt == delays.size()) break;
        std::this_thread::sleep_for(delays[attempt]);
    }
    if (result.error && attempts == 3) {
        result.error->message += "; falhou após 3 tentativas automáticas";
    }
    return result;
}

std::vector<std::string> table_cells(const std::string& row) {
    std::vector<std::string> cells;
    const std::regex expression("<td[^>]*>([\\s\\S]*?)</td>", std::regex::icase);
    for (auto iterator = std::sregex_iterator(row.begin(), row.end(), expression);
         iterator != std::sregex_iterator(); ++iterator) {
        cells.push_back(strip_html((*iterator)[1].str()));
    }
    return cells;
}

std::optional<std::string> element_text_by_id(
    const std::string& html, const std::string& id, const char* tag = "span") {
    const std::regex expression(
        "<" + std::string(tag) + "[^>]*id=[\"']" + id +
        "[\"'][^>]*>([\\s\\S]*?)</" + tag + ">",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(html, match, expression)) return std::nullopt;
    return strip_html(match[1].str());
}

std::optional<std::string> labeled_div_text(
    const std::string& html, const std::string& id, const std::string& label) {
    const auto content = element_text_by_id(html, id, "div");
    if (!content) return std::nullopt;
    std::string result = *content;
    if (result.starts_with(label)) result.erase(0, label.size());
    return trim(result);
}

std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto separator = value.find(',', start);
        auto item = trim(value.substr(start, separator == std::string::npos
            ? std::string::npos : separator - start));
        if (!item.empty()) result.push_back(std::move(item));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return result;
}

std::uint32_t parse_us_date(const std::string& value) {
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int year = 0;
    if (sscanf_s(value.c_str(), "%u/%u/%u", &month, &day, &year) != 3 ||
        month == 0 || month > 12 || day == 0 || day > 31 || year < 1900) return 0;
    return year * 10000U + month * 100U + day;
}

std::optional<std::string> assignment(
    const std::string& script, std::size_t index, const char* property) {
    const std::string pattern =
        "downloadInformation\\[0\\]\\.files\\[" + std::to_string(index) +
        "\\]\\." + property + "\\s*=\\s*'([^']*)'";
    std::smatch match;
    if (!std::regex_search(script, match, std::regex(pattern, std::regex::icase))) return std::nullopt;
    return html_decode(match[1].str());
}

}  // namespace

HttpsTextResult fetch_https_text(const std::string& url) {
    HttpsTextResult result;
    if (!url.starts_with("https://")) {
        result.error = make_error("HTTPS source URL", ERROR_INVALID_NAME);
        return result;
    }
    const auto response = http_request(url, L"GET");
    if (response.error) {
        result.error = response.error;
        return result;
    }
    result.body.assign(response.body.begin(), response.body.end());
    return result;
}

CatalogSearchResult search_microsoft_update_catalog(const std::string& query) {
    CatalogSearchResult result;
    constexpr std::array semantic_retry_delays{std::chrono::milliseconds(750),
                                                std::chrono::milliseconds(2000)};
    for (std::size_t attempt = 0; attempt <= semantic_retry_delays.size(); ++attempt) {
        const auto response = http_request(
            "https://www.catalog.update.microsoft.com/Search.aspx?q=" + url_encode(query), L"GET");
        if (response.error) {
            result.error = response.error;
            return result;
        }
        const std::string html(response.body.begin(), response.body.end());
        result.entries = parse_catalog_search_html(html);
        const bool recognizable_empty_result =
            html.find("catalogBody_noResultText") != std::string::npos ||
            html.find("catalogBody_noResults") != std::string::npos ||
            html.find("No items match your search") != std::string::npos;
        if (!result.entries.empty() || recognizable_empty_result) return result;
        if (attempt != semantic_retry_delays.size()) {
            std::this_thread::sleep_for(semantic_retry_delays[attempt]);
        }
    }
    result.error = make_error("catalog search response after 3 attempts", ERROR_INVALID_DATA);
    return result;
}

std::vector<CatalogSearchEntry> parse_catalog_search_html(std::string_view html_view) {
    std::vector<CatalogSearchEntry> entries;
    const std::string html(html_view);
    const std::regex row_expression(
        "<tr[^>]*id=\"([0-9a-fA-F-]{36})_R[0-9]+\"[^>]*>([\\s\\S]*?)</tr>",
        std::regex::icase);
    for (auto iterator = std::sregex_iterator(html.begin(), html.end(), row_expression);
         iterator != std::sregex_iterator(); ++iterator) {
        CatalogSearchEntry entry;
        entry.catalog_update_id = (*iterator)[1].str();
        const auto cells = table_cells((*iterator)[2].str());
        if (cells.size() >= 7) {
            entry.title = cells[1];
            entry.products = cells[2];
            entry.classification = cells[3];
            entry.last_updated = cells[4];
            entry.version = cells[5];
            std::string digits;
            for (const char character : cells[6]) if (std::isdigit(static_cast<unsigned char>(character))) digits += character;
            if (!digits.empty()) entry.size_bytes = std::strtoull(digits.c_str(), nullptr, 10);
        }
        std::smatch original_size;
        const auto row = (*iterator)[2].str();
        if (std::regex_search(row, original_size, std::regex(
                "_originalSize\"[^>]*>([0-9]+)<", std::regex::icase))) {
            entry.size_bytes = std::strtoull(original_size[1].str().c_str(), nullptr, 10);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

CatalogDetailsResult get_microsoft_update_catalog_details(
    const std::string& catalog_update_id) {
    CatalogDetailsResult result;
    constexpr std::array semantic_retry_delays{std::chrono::milliseconds(750),
                                                std::chrono::milliseconds(2000)};
    for (std::size_t attempt = 0; attempt <= semantic_retry_delays.size(); ++attempt) {
        const auto response = http_request(
            "https://www.catalog.update.microsoft.com/ScopedViewInline.aspx?updateid=" +
                url_encode(catalog_update_id),
            L"GET");
        if (response.error) {
            result.error = response.error;
            return result;
        }
        const std::string html(response.body.begin(), response.body.end());
        result.details = parse_catalog_details_html(catalog_update_id, html);
        if (result.details) return result;
        if (attempt != semantic_retry_delays.size()) {
            std::this_thread::sleep_for(semantic_retry_delays[attempt]);
        }
    }
    result.error = make_error("catalog details metadata after 3 attempts", ERROR_INVALID_DATA);
    return result;
}

std::optional<CatalogDetails> parse_catalog_details_html(
    const std::string& catalog_update_id,
    std::string_view html_view) {
    const std::string html(html_view);
    CatalogDetails details;
    details.catalog_update_id = catalog_update_id;
    details.description = element_text_by_id(html, "ScopedViewHandler_desc").value_or("");
    details.architectures = split_list(
        labeled_div_text(html, "archDiv", "Architecture:").value_or(""));
    details.products = split_list(
        labeled_div_text(html, "productsDiv", "Supported products:").value_or(""));
    details.languages = split_list(
        labeled_div_text(html, "languagesDiv", "Supported languages:").value_or(""));
    details.company = element_text_by_id(html, "ScopedViewHandler_company").value_or("");
    details.manufacturer = element_text_by_id(html, "ScopedViewHandler_manufacturer").value_or("");
    details.driver_class = element_text_by_id(html, "ScopedViewHandler_driverClass").value_or("");
    details.model = element_text_by_id(html, "ScopedViewHandler_driverModel").value_or("");
    details.provider = element_text_by_id(html, "ScopedViewHandler_driverProvider").value_or("");
    details.version = element_text_by_id(html, "ScopedViewHandler_version").value_or("");
    details.version_date_yyyymmdd = parse_us_date(
        element_text_by_id(html, "ScopedViewHandler_versionDate").value_or(""));

    const auto hardware_start = html.find("id=\"driverhwIDs\"");
    const auto hardware_end = html.find("id=\"ScopedViewHandler_labelSupportedUpdateIDs\"",
                                        hardware_start);
    if (hardware_start != std::string::npos && hardware_end != std::string::npos) {
        const auto section = html_decode(
            html.substr(hardware_start, hardware_end - hardware_start));
        const std::regex hardware_expression(
            "([A-Za-z0-9_*&.-]+\\\\[A-Za-z0-9_*&.\\\\-]+)", std::regex::icase);
        for (auto iterator = std::sregex_iterator(section.begin(), section.end(), hardware_expression);
             iterator != std::sregex_iterator(); ++iterator) {
            const auto value = (*iterator)[1].str();
            if (std::find(details.hardware_ids.begin(), details.hardware_ids.end(), value) ==
                details.hardware_ids.end()) details.hardware_ids.push_back(value);
        }
    }
    if (details.architectures.empty() || details.hardware_ids.empty()) return std::nullopt;
    return details;
}

CatalogFileResult get_microsoft_update_catalog_files(const std::string& catalog_update_id) {
    CatalogFileResult result;
    static const std::regex guid_expression(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    if (!std::regex_match(catalog_update_id, guid_expression)) {
        result.error = make_error("catalog update ID", ERROR_INVALID_DATA);
        return result;
    }

    const std::string json = "[{\"size\":0,\"languages\":\"\",\"uidInfo\":\"" +
        catalog_update_id + "\",\"updateID\":\"" + catalog_update_id + "\"}]";
    const auto response = http_request(
        "https://www.catalog.update.microsoft.com/DownloadDialog.aspx", L"POST",
        "updateIDs=" + url_encode(json), L"application/x-www-form-urlencoded");
    if (response.error) {
        result.error = response.error;
        return result;
    }
    const std::string script(response.body.begin(), response.body.end());
    result.files = parse_catalog_download_script(script);
    if (result.files.empty()) result.error = make_error("catalog download metadata", ERROR_NOT_FOUND);
    return result;
}

std::vector<CatalogFile> parse_catalog_download_script(std::string_view script_view) {
    std::vector<CatalogFile> files;
    const std::string script(script_view);
    for (std::size_t index = 0;; ++index) {
        const auto url = assignment(script, index, "url");
        if (!url) break;
        CatalogFile file;
        file.url = *url;
        file.file_name = assignment(script, index, "fileName").value_or("content-" + std::to_string(index));
        file.digest_sha1_base64 = assignment(script, index, "digest").value_or("");
        file.sha256_base64 = assignment(script, index, "sha256").value_or("");
        files.push_back(std::move(file));
    }
    return files;
}

std::string sha256_file_base64(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD written = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    const auto close_algorithm = std::unique_ptr<void, decltype([](void* value) {
        if (value) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(value), 0);
    })>(algorithm, {});
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0) < 0) return {};
    object.resize(object_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0) return {};
    const auto destroy_hash = std::unique_ptr<void, decltype([](void* value) {
        if (value) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(value));
    })>(hash, {});

    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count), 0) < 0) return {};
    }
    if (!input.eof() || BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};

    DWORD chars = 0;
    if (!CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars)) return {};
    std::string result(chars, '\0');
    if (!CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &chars)) return {};
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

CatalogDownloadResult download_catalog_file(
    const CatalogFile& file,
    const std::filesystem::path& destination,
    bool accept_computed_hash) {
    CatalogDownloadResult result;
    result.path = destination;
    if (std::filesystem::exists(destination)) {
        result.error = make_error("destination already exists", ERROR_FILE_EXISTS);
        return result;
    }
    const auto response = http_request(file.url, L"GET");
    if (response.error) {
        result.error = response.error;
        return result;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error) {
        result.error = make_error("create destination directory", filesystem_error.value());
        return result;
    }
    std::ofstream output(destination, std::ios::binary | std::ios::out);
    if (!output) {
        result.error = make_error("create destination file", GetLastError());
        return result;
    }
    output.write(reinterpret_cast<const char*>(response.body.data()),
                 static_cast<std::streamsize>(response.body.size()));
    output.close();
    if (!output) {
        result.error = make_error("write destination file", ERROR_WRITE_FAULT);
        return result;
    }
    result.size_bytes = response.body.size();
    result.sha256_base64 = sha256_file_base64(destination);
    result.hash_matches = file.sha256_base64.empty()
        ? accept_computed_hash && !result.sha256_base64.empty()
        : result.sha256_base64 == file.sha256_base64;
    if (!result.hash_matches) {
        std::filesystem::remove(destination, filesystem_error);
        result.error = make_error("SHA-256 verification", ERROR_CRC);
    }
    return result;
}

}  // namespace open_driver
