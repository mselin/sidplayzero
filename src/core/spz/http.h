// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: downloads (SPEC-021, SPEC-150). `core` uses the network only through this.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace spz {

enum class HttpFailure {
    None,
    NotFound,     // the server has no such file: 404 or 410
    Network,      // no connection, a timeout, or a server error
    Tls,          // the certificate or the name was refused, or the address is not HTTPS
    TooLarge,     // more than the request's limit
    CannotWrite,  // the file could not be written
    CardFull,
    Cancelled,
};

struct HttpRequest {
    std::string url;
    std::string to_path;              // where the body is written
    std::uint64_t limit_bytes = 0;    // stop and fail beyond this (SPEC-150)
    std::uint64_t resume_from = 0;    // ask for the rest from this byte, appending to `to_path`
    std::string if_etag;              // with resume_from: only if the file is still this one
    std::string if_last_modified;
};

struct HttpResult {
    HttpFailure failure = HttpFailure::None;
    int status = 0;                   // the HTTP status, 0 if there was none
    std::uint64_t bytes = 0;          // bytes of body written by this request
    std::uint64_t total = 0;          // the whole file's size if the server said, else 0
    bool resumed = false;             // the server honoured resume_from
    std::string etag;
    std::string last_modified;
};

class HttpClient {
public:
    // Called as the body arrives, with bytes so far and the total if known. Return false to cancel.
    using Progress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

    virtual ~HttpClient() = default;
    virtual HttpResult fetch(const HttpRequest& request, const Progress& progress) = 0;
};

}  // namespace spz
