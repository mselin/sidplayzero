// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/curl_http.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "platform/guarded.h"
#include "spz/text.h"
#include "spz/version.h"

#ifndef SPZ_TEST_HOOKS
#define SPZ_TEST_HOOKS 0
#endif

namespace spz::platform {

namespace {

const long kConnectSeconds = 15;
const long kSlowBytesPerSecond = 1024;
const long kSlowSeconds = 30;
const long kMostRedirects = 5;

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// http://127.0.0.1, then a port of digits or nothing, then a path or nothing: the address's host is
// 127.0.0.1 and nothing else. "http://127.0.0.1:x@other.host/" is refused: whatever comes before an `@`
// is a user name, and the host is what follows it.
bool is_loopback_http(const std::string& url) {
    const char prefix[] = "http://127.0.0.1";
    if (!text::starts_with(url, prefix)) return false;
    std::size_t at = sizeof prefix - 1;
    if (at < url.size() && url[at] == ':') {
        ++at;
        while (at < url.size() && url[at] >= '0' && url[at] <= '9') ++at;
    }
    return at == url.size() || url[at] == '/';
}

// A header's name or value: spaces and tabs at the start, and those and the line's end at the end, dropped.
std::string trim(const std::string& text) { return text::trim_start(text::trim_end(text, " \t\r\n")); }

std::uint64_t number(const std::string& text) {
    std::uint64_t value = 0;
    bool any = false;
    for (char c : text) {
        if (c < '0' || c > '9') break;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
        any = true;
    }
    return any ? value : 0;
}

// A Content-Range of a 206: "bytes FIRST-LAST/TOTAL", TOTAL a number or "*". False for anything else.
bool read_content_range(const std::string& value, std::uint64_t* first, std::uint64_t* last, std::uint64_t* total) {
    const std::string unit = "bytes ";
    if (text::ascii_lower(value.substr(0, unit.size())) != unit) return false;
    const std::string range = trim(value.substr(unit.size()));
    const std::size_t dash = range.find('-');
    const std::size_t slash = range.find('/');
    if (dash == std::string::npos || slash == std::string::npos || dash == 0 || slash < dash + 2) return false;
    const auto digits = [](const std::string& text) {
        return !text.empty() && text.size() <= 19 && text.find_first_not_of("0123456789") == std::string::npos;
    };
    const std::string a = range.substr(0, dash), b = range.substr(dash + 1, slash - dash - 1), c = range.substr(slash + 1);
    if (!digits(a) || !digits(b) || !(c == "*" || digits(c))) return false;
    *first = number(a);
    *last = number(b);
    *total = c == "*" ? 0 : number(c);
    return *first <= *last && (*total == 0 || *last < *total);
}

// What one request gathers while it runs.
struct Transfer {
    const HttpRequest* request = nullptr;
    const HttpClient::Progress* progress = nullptr;
    const std::atomic<bool>* stopping = nullptr;
    CURL* curl = nullptr;
    int fd = -1;
    bool opened = false;
    bool resumed = false;
    std::uint64_t written = 0;
    HttpFailure write_failure = HttpFailure::None;
    std::string etag, last_modified;
    std::uint64_t content_length = 0, range_total = 0;
    std::uint64_t range_first = 0, range_last = 0;
    bool range_given = false;      // a Content-Range this answer gave, read as SPEC-150's check needs
    bool bad_answer = false;       // a 206 that is not the rest of the file asked for: nothing of it is kept
    bool cancelled = false;

    std::uint64_t offset() const { return resumed ? request->resume_from : 0; }

    static std::size_t header(char* data, std::size_t size, std::size_t count, void* user) {
        auto* self = static_cast<Transfer*>(user);
        const std::string line(data, size * count);
        if (text::starts_with(line, "HTTP/")) {   // a new answer, after a redirect: forget the last one's
            self->etag.clear();
            self->last_modified.clear();
            self->content_length = 0;
            self->range_total = 0;
            self->range_given = false;
            return size * count;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) return size * count;
        const std::string name = text::ascii_lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (name == "etag") {
            self->etag = value;
        } else if (name == "last-modified") {
            self->last_modified = value;
        } else if (name == "content-length") {
            self->content_length = number(value);
        } else if (name == "content-range") {
            self->range_given = read_content_range(value, &self->range_first, &self->range_last, &self->range_total);
        }
        return size * count;
    }

    static std::size_t body(char* data, std::size_t size, std::size_t count, void* user) {
        auto* self = static_cast<Transfer*>(user);
        const std::size_t bytes = size * count;
        long status = 0;
        curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status != 200 && status != 206) return bytes;   // a page about an error is not the file
        if (!self->opened) {
            // A 206 is kept only as the rest of the file from the byte asked for: to a request that asked
            // for no range, or starting anywhere else, it would put the wrong bytes in the file.
            if (status == 206 && (self->request->resume_from == 0 || !self->range_given ||
                                  self->range_first != self->request->resume_from)) {
                self->bad_answer = true;
                return 0;
            }
            self->resumed = status == 206 && self->request->resume_from > 0;
            const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (self->resumed ? O_APPEND : O_TRUNC);
            self->fd = ::open(self->request->to_path.c_str(), flags, 0644);
            self->opened = true;
            if (self->fd < 0) {
                self->write_failure = (errno == ENOSPC || errno == EDQUOT) ? HttpFailure::CardFull
                                                                         : HttpFailure::CannotWrite;
                return 0;
            }
        }
        if (self->fd < 0) return 0;
        std::size_t take = bytes;
        const std::uint64_t limit = self->request->limit_bytes;
        bool over = false;
        if (limit > 0 && self->offset() + self->written + take > limit) {
            take = static_cast<std::size_t>(limit - std::min<std::uint64_t>(limit, self->offset() + self->written));
            over = true;
        }
        std::size_t done = 0;
        while (done < take) {
            const ssize_t put = ::write(self->fd, data + done, take - done);
            if (put < 0) {
                if (errno == EINTR) continue;
                self->write_failure = (errno == ENOSPC || errno == EDQUOT) ? HttpFailure::CardFull
                                                                         : HttpFailure::CannotWrite;
                return 0;
            }
            done += static_cast<std::size_t>(put);
        }
        self->written += take;
        if (over) {
            self->write_failure = HttpFailure::TooLarge;
            return 0;
        }
        return bytes;
    }

    // What libcurl calls: C frames, which nothing may be thrown through (A8). A callback that throws stops
    // the transfer, which then fails.
    static std::size_t guarded_header(char* data, std::size_t size, std::size_t count, void* user) noexcept {
        return run_guarded_or<std::size_t>("a header of a download", 0, [&] { return header(data, size, count, user); });
    }
    static std::size_t guarded_body(char* data, std::size_t size, std::size_t count, void* user) noexcept {
        return run_guarded_or<std::size_t>("a download's data", 0, [&] { return body(data, size, count, user); });
    }
    static int guarded_info(void* user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) noexcept {
        return run_guarded_or<int>("a download's progress", 1, [&] { return info(user, a, b, c, d); });
    }

    static int info(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        auto* self = static_cast<Transfer*>(user);
        if (self->stopping->load()) {
            self->cancelled = true;
            return 1;
        }
        if (*self->progress && self->opened) {
            // The whole file's size: from Content-Range for the rest of a file, else from Content-Length.
            const std::uint64_t total = self->resumed ? self->range_total : self->content_length;
            if (!(*self->progress)(self->offset() + self->written, total)) {
                self->cancelled = true;
                return 1;
            }
        }
        return 0;
    }
};

HttpFailure failure_of(CURLcode code) {
    switch (code) {
        case CURLE_OK: return HttpFailure::None;
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        case CURLE_SSL_INVALIDCERTSTATUS:
        case CURLE_USE_SSL_FAILED:
            return HttpFailure::Tls;
        case CURLE_FILESIZE_EXCEEDED: return HttpFailure::TooLarge;
        case CURLE_ABORTED_BY_CALLBACK: return HttpFailure::Cancelled;
        default: return HttpFailure::Network;
    }
}

}  // namespace

CurlHttpClient::CurlHttpClient(HttpRules rules) : rules_(std::move(rules)) {
    global_init();
    if (!test_rules_allowed()) rules_ = HttpRules();
}

CurlHttpClient::~CurlHttpClient() = default;

void CurlHttpClient::stop_all() { stopping_.store(true); }

std::string CurlHttpClient::user_agent() { return std::string("SidPlayZero/") + app_version(); }

bool CurlHttpClient::test_rules_allowed() { return SPZ_TEST_HOOKS != 0; }

HttpResult CurlHttpClient::fetch(const HttpRequest& request, const Progress& progress) {
    HttpResult result;
    const bool https = text::starts_with(request.url, "https://");
    const bool loopback = rules_.loopback_http && is_loopback_http(request.url);
    if (!https && !loopback) {
        result.failure = HttpFailure::Tls;     // not HTTPS: refused before anything is sent
        return result;
    }
    if (stopping_.load()) {
        result.failure = HttpFailure::Cancelled;
        return result;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.failure = HttpFailure::Network;
        return result;
    }
    Transfer transfer;
    transfer.request = &request;
    transfer.progress = &progress;
    transfer.stopping = &stopping_;
    transfer.curl = curl;

    const std::string agent = user_agent();
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, loopback ? "http" : "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, kMostRedirects);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!rules_.extra_authority.empty()) {
        // The test's authority, beside the system's own.
        curl_easy_setopt(curl, CURLOPT_CAINFO, rules_.extra_authority.c_str());
        curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/ssl/certs");
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectSeconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kSlowBytesPerSecond);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kSlowSeconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
    if (request.limit_bytes > 0) {
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(request.limit_bytes));
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &Transfer::guarded_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Transfer::guarded_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &Transfer::guarded_info);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

    curl_slist* headers = nullptr;
    std::string range;
    if (request.resume_from > 0) {
        range = std::to_string(request.resume_from) + "-";
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        // The rest only of the same file: the server answers with all of it if the file has changed.
        const std::string& condition = !request.if_etag.empty() ? request.if_etag : request.if_last_modified;
        if (!condition.empty()) {
            headers = curl_slist_append(headers, ("If-Range: " + condition).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
    }

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (transfer.fd >= 0) {
        if (::fsync(transfer.fd) != 0 && transfer.write_failure == HttpFailure::None && code == CURLE_OK) {
            transfer.write_failure = HttpFailure::CannotWrite;
        }
        ::close(transfer.fd);
    }

    result.status = static_cast<int>(status);
    result.bytes = transfer.written;
    result.resumed = transfer.resumed;
    result.etag = transfer.etag;
    result.last_modified = transfer.last_modified;
    if (status == 206 && transfer.range_total > 0) {
        result.total = transfer.range_total;
    } else if (status == 200) {
        result.total = transfer.content_length;
    }
    // The rest of a file ends where its Content-Range said, and the file is then the whole of it.
    const bool short_rest = status == 206 && (!transfer.opened ||
                            (transfer.offset() + transfer.written != transfer.range_last + 1 ||
                             (transfer.range_total > 0 && transfer.range_last + 1 != transfer.range_total)));

    if (transfer.write_failure != HttpFailure::None) {
        result.failure = transfer.write_failure;
    } else if (transfer.bad_answer) {
        result.failure = HttpFailure::Network;   // the server did not answer what was asked
    } else if (transfer.cancelled) {
        result.failure = HttpFailure::Cancelled;
    } else if (code != CURLE_OK) {
        result.failure = failure_of(code);
    } else if (status == 404 || status == 410) {
        result.failure = HttpFailure::NotFound;
    } else if (status != 200 && status != 206) {
        result.failure = HttpFailure::Network;
    } else if (short_rest) {
        result.failure = HttpFailure::Network;   // kept as it is: a later request may fetch the rest
    } else if (!transfer.opened) {
        // A success with an empty body: the file is there, and empty.
        const int fd = ::open(request.to_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) {
            result.failure = (errno == ENOSPC || errno == EDQUOT) ? HttpFailure::CardFull : HttpFailure::CannotWrite;
        } else {
            ::close(fd);
        }
    }
    return result;
}

}  // namespace spz::platform
