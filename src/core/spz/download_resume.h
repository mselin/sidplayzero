// SPDX-License-Identifier: GPL-3.0-or-later
// A download that can carry on where it was cut (SPEC-147). It is used for the complete archive: a
// partial file is kept in the cache as NAME.part, with NAME.part.meta beside it holding the server's
// ETag and Last-Modified (SPEC-132), and the next attempt asks for the rest.
#pragma once

#include <cstdint>
#include <string>

#include "spz/files.h"
#include "spz/http.h"

namespace spz {

// Downloads `url` to FOLDER/NAME.part. Called on a worker; returns when the request has ended.
//
// If FOLDER/NAME.part and its .meta are there, and the server gave both an ETag and a Last-Modified when
// the partial file was written, the request asks for the rest: `resume_from` is the partial file's size,
// with those two values as its condition. In every other case, and when the server answers without
// honouring the range, the file is written again from its first byte. After each attempt that got some
// bytes and was cut or stopped, the partial file and a .meta for it are left for the next attempt, and
// nothing else. `progress` is HttpClient's, with `done` counted from the start of the whole file;
// returning false stops the download.
//
// Returns the result of the request: with `failure` None the whole file is at FOLDER/NAME.part, and
// its .meta is still there; the caller deletes both when it has used the file.
HttpResult download_resumable(Files* files, HttpClient* http, const std::string& url, const std::string& folder,
                              const std::string& name, std::uint64_t limit_bytes,
                              const HttpClient::Progress& progress);

// Deletes FOLDER/NAME.part and its .meta, if they are there.
void discard_partial(Files* files, const std::string& folder, const std::string& name);

}  // namespace spz
