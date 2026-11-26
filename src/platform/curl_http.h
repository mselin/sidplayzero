// SPDX-License-Identifier: GPL-3.0-or-later
// The real HttpClient (SPEC-006, SPEC-021): the system's libcurl, with the rules of SPEC-150.
//
// Every request is HTTPS, the server's certificate and name checked against the system's certificates.
// Redirects are followed at most five times, and only to HTTPS. A request is abandoned if it cannot connect
// within 15 s, or runs slower than 1 KiB/s for 30 s. A body past the request's limit fails it as too large.
// The request names the app and its version as its user agent and sends nothing else about anybody: no
// cookies. A body is written to the request's file only when the answer is a success (200, or 206 for a
// range); a file that is not HTTPS is refused before anything is sent. A 206 is kept only when its
// Content-Range starts at the byte the request asked for, and the request fails, keeping what it wrote,
// unless the file then ends where Content-Range said and that is the whole file's size.
//
// A test build (SIDPLAYZERO_TEST_HOOKS), and only a test build, may also be told to accept
// http://127.0.0.1 and a certificate authority named by the test, beside the system's. In any other
// build those two rules are ignored.
//
// fetch() may be called on several threads at once; each call is one request.
#pragma once

#include <atomic>
#include <string>

#include "spz/http.h"

namespace spz::platform {

struct HttpRules {
    bool loopback_http = false;    // also accept http://127.0.0.1 (a test build only)
    std::string extra_authority;   // a PEM file of a certificate authority to trust too (a test build only)
};

class CurlHttpClient : public HttpClient {
public:
    explicit CurlHttpClient(HttpRules rules = HttpRules());
    ~CurlHttpClient() override;

    HttpResult fetch(const HttpRequest& request, const Progress& progress) override;

    // Makes every request in flight, and every later one, fail as cancelled within about a second: for the
    // app's exit (SPEC-201).
    void stop_all();

    // "SidPlayZero/0.1.0": the user agent every request names (SPEC-150).
    static std::string user_agent();
    // Whether this build may take up HttpRules' two test rules at all.
    static bool test_rules_allowed();

private:
    HttpRules rules_;
    std::atomic<bool> stopping_{false};
};

}  // namespace spz::platform
