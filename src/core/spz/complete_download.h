// SPDX-License-Identifier: GPL-3.0-or-later
// The complete download (SPEC-144), part `download-all`: when it is offered (SPEC-145), the check of the
// archive before anything is unpacked (SPEC-149), the room it needs (SPEC-146), and the job itself, with
// its marker file and what happens to it at the next start.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "spz/executor.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/notice_gate.h"
#include "spz/settings.h"
#include "spz/system.h"

namespace spz {

// SPEC-145's numbers, SPEC-150's limit for the archive, and SPEC-149's limits.
const std::uint64_t kUnpackMemoryMib = 450;
const std::uint64_t kDownloadRoomBytes = 700ull * 1024 * 1024;
const std::uint64_t kArchiveLimitBytes = 512ull * 1024 * 1024;
const std::uint64_t kArchiveMaxEntries = 200000;
const std::uint64_t kArchiveMaxBytes = 2ull * 1024 * 1024 * 1024;

// --- SPEC-145: when it is offered -------------------------------------------------------------------

struct DownloadOffer {
    bool offered = false;                        // the row "Download everything" is shown
    std::string text;                            // "", or the identifier of the explanation under the list:
                                                 // MSG_NEED_7ZIP, MSG_NEED_MEMORY, MSG_NEED_ROOM or MSG_NEED_WRITE
    std::map<std::string, std::string> about;    // MSG_NEED_MEMORY: "n", the MiB free. MSG_NEED_ROOM: "n" needed, "m" free
    // What each condition found, also for the device's results file (SPEC-241).
    std::uint64_t memory_mib = 0;                // available memory plus free swap
    bool unpacker = false;
    std::uint64_t free_mib = 0;
    bool can_write = false;                      // the folder holding the collection root
};

// The table of SPEC-145, top to bottom; the first condition that does not hold gives the text. With
// HVSC not set up, or its release not known, the row is absent and there is no text. Asks the seams, so
// it is called on a worker.
DownloadOffer download_offer(const HvscStatus& status, Files* files, Unpacker* unpacker, Memory* memory,
                             const std::string& root);

// --- SPEC-149: the archive is checked before anything is unpacked -----------------------------------

// Reads what `7z l -slt` prints into entries: each block's Path, Size and Attributes. A folder has "D"
// in its attributes; a link or a special file is known by the first letter of the Unix mode there ("l";
// "b", "c", "p", "s"). False if the text is not such a listing, or an entry's size is not a number.
// This is the reader the real Unpacker uses, and the fuzz target of SPEC-205.
bool read_7z_listing(const std::string& text, std::vector<ArchiveEntry>* out);

enum class ArchiveFault {
    None,
    AbsolutePath,       // a path that starts with "/"
    EmptyPart,          // "a//b", or a path that ends in "/"
    DotPart,            // a part that is "." or ".."
    Backslash,
    Link,
    Special,
    OutsideTopFolder,   // not `C64Music` itself or under it
    TooManyEntries,     // more than 200,000
    TooLarge,           // sizes that add up to more than 2 GiB
};
// Why an archive with these entries is refused; None if it is not.
ArchiveFault check_archive(const std::vector<ArchiveEntry>& entries);

// --- SPEC-146: room, once the archive has been listed -----------------------------------------------

// The archive's size plus the sizes of its entries, each rounded up to the block size, plus a tenth of
// that sum, plus 64 MiB.
std::uint64_t room_for_unpacking(std::uint64_t archive_bytes, const std::vector<ArchiveEntry>& entries,
                                 std::uint64_t block_size);

// --- SPEC-144: the job -----------------------------------------------------------------------------

// The marker: the file install.state in the cache folder, naming the step the complete download is in.
// Reading gives 0 if there is none. Writing goes through SPEC-132; writing step 0 removes it.
int read_install_marker(Files* files, const std::string& cache);
bool write_install_marker(Files* files, const std::string& cache, int step);

// The player's side of step 2. The player belongs to another part of `core`; this is all the job needs
// of it. Called on the UI thread.
class PlaySwitch {
public:
    virtual ~PlaySwitch() = default;
    // Stops the player. Until playing_on(), everything that would load a tune gives the notice
    // MSG_UNPACKING instead (SPEC-060).
    virtual void playing_off() = 0;
    virtual void playing_on() = 0;
};

// Told, on the UI thread, how the job ended.
class DownloadListener {
public:
    virtual ~DownloadListener() = default;
    // Step 8 has ended: the listing, listing.bin in the cache, is new and is to be opened again, and the
    // index is built again (SPEC-161). The notice MSG_DOWNLOAD_DONE has been raised.
    virtual void download_done() = 0;
    // It failed or was stopped in this step. The notice MSG_DOWNLOAD_FAILED has been raised.
    virtual void download_failed(int step) = 0;
};

struct CompleteDownloadParts {
    Files* files = nullptr;
    HttpClient* http = nullptr;
    Unpacker* unpacker = nullptr;
    Memory* memory = nullptr;
    Executor* long_worker = nullptr;
    Executor* ui = nullptr;
    NoticeGate* gate = nullptr;
    Jobs* jobs = nullptr;
    NoticeSink* notices = nullptr;
    const Settings* settings = nullptr;    // `url_archive`
    std::string root;                      // the collection root; HVSC is set up there
    std::string cache;                     // listing.bin, install.state, listing.old and the archive are here
    PlaySwitch* play = nullptr;
    DownloadListener* listener = nullptr;  // may be null
};

class CompleteDownload {
public:
    explicit CompleteDownload(const CompleteDownloadParts& parts);
    ~CompleteDownload();
    CompleteDownload(const CompleteDownload&) = delete;
    CompleteDownload& operator=(const CompleteDownload&) = delete;

    // "Download everything". Step 1 is the notice gate, and the release HVSC is set up with. Then a job
    // of kind Download on the long worker, which keeps the marker at the step it is in:
    //   2  SPEC-145's conditions again, and 700 MiB of room (MSG_NEED_ROOM); the player is switched off
    //   3  `url_archive` with {release} replaced, to the cache, resumable (download_resumable), limit 512 MiB
    //   4  Unpacker::list, check_archive, and room_for_unpacking against the free space (MSG_NEED_ROOM)
    //   5  Unpacker::unpack into `.sidplayzero-staging` beside the root, removed first if it is there
    //   6  no link or special file in the staging folder; its song length file gives a listing
    //   7  listing.bin is copied to listing.old; every file of the staging folder's C64Music is renamed
    //      over its place under the root, DOCUMENTS last; each file listing.old names and the new song
    //      length file does not is removed if its MD5 is still listing.old's. No stopping from here on.
    //   8  the listing is built again; the archive, the staging folder, listing.old and the marker are
    //      deleted; the notice MSG_DOWNLOAD_DONE
    // A failure or a stop in steps 2 to 6 leaves the root as it was, removes the staging folder and the
    // marker, keeps a partial archive, and raises MSG_DOWNLOAD_FAILED with "step". A failure in step 7
    // or 8 raises the same and keeps the marker, the staging folder and listing.old. Either way playing
    // is switched on again. Returns false, and does nothing, if a job is running already.
    bool start();

    // At start (SPEC-200), on the long worker, before anything reads the song length database. A marker
    // naming step 1 to 6: the staging folder and the marker are removed. A marker naming step 7 or 8:
    // those steps are taken up again from where they stopped. No marker: nothing.
    void recover();

    // Whether a download was cut short in step 7 or 8 and has not been finished: the HVSC screen then
    // offers "Finish the download" in place of "Download everything", whatever SPEC-145 says.
    bool unfinished() const;
    // "Finish the download": takes steps 7 and 8 up again, as a job. False if a job is running already
    // or there is nothing to finish.
    bool finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
