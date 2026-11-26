// SPDX-License-Identifier: GPL-3.0-or-later
// A download that can carry on where it was cut (SPEC-147).
//
// NAME.part.meta holds two lines: the ETag and the Last-Modified the server gave for the partial file.
#include "spz/download_resume.h"

#include "spz/safe_file.h"
#include "spz/types.h"

namespace spz {

namespace {

const std::size_t kMetaLimit = 64 * 1024;

struct Validators {
    std::string etag;
    std::string last_modified;
};

bool read_meta(Files* files, const std::string& meta, Validators* out) {
    Bytes bytes;
    if (files->read(meta, kMetaLimit, &bytes) != FileStatus::Ok) return false;
    const std::string text(bytes.begin(), bytes.end());
    const std::size_t newline = text.find('\n');
    if (newline == std::string::npos) return false;
    out->etag = text.substr(0, newline);
    std::string rest = text.substr(newline + 1);
    const std::size_t end = rest.find('\n');
    if (end != std::string::npos) rest.resize(end);
    out->last_modified = rest;
    return true;
}

bool safe_value(const std::string& value) {
    for (const char c : value) {
        if (c == '\n' || c == '\r' || c == '\0') return false;
    }
    return true;
}

}  // namespace

HttpResult download_resumable(Files* files, HttpClient* http, const std::string& url, const std::string& folder,
                              const std::string& name, std::uint64_t limit_bytes,
                              const HttpClient::Progress& progress) {
    const std::string part = folder + "/" + name + kPartEnding;
    const std::string meta = folder + "/" + name + kPartMetaEnding;

    HttpRequest request;
    request.url = url;
    request.to_path = part;
    request.limit_bytes = limit_bytes;

    FileInfo info;
    Validators kept;
    if (files->info(part, &info) == FileStatus::Ok && !info.is_folder && info.size > 0 && read_meta(files, meta, &kept) &&
        !kept.etag.empty() && !kept.last_modified.empty()) {
        request.resume_from = info.size;
        request.if_etag = kept.etag;
        request.if_last_modified = kept.last_modified;
    }

    const HttpResult result = http->fetch(request, progress);

    FileInfo after;
    const bool have_bytes = files->info(part, &after) == FileStatus::Ok && !after.is_folder && after.size > 0;
    if (!have_bytes) {
        files->remove(part);
        files->remove(meta);
        return result;
    }
    // The server answered (it gave a status): what it said now describes the file. Without an answer the
    // file is as it was, and so is what its .meta says of it.
    if (result.status != 0) {
        const std::string etag = safe_value(result.etag) ? result.etag : "";
        const std::string modified = safe_value(result.last_modified) ? result.last_modified : "";
        const std::string text = etag + "\n" + modified + "\n";
        if (files->write(meta, Bytes(text.begin(), text.end())) != FileStatus::Ok) files->remove(meta);
    }
    return result;
}

void discard_partial(Files* files, const std::string& folder, const std::string& name) {
    files->remove(folder + "/" + name + kPartEnding);
    files->remove(folder + "/" + name + kPartMetaEnding);
}

}  // namespace spz
