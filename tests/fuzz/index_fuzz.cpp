// test: SPEC-205 fuzz: the reader of the search index, index.bin
//
// The bytes are given to read_index() as an index file (SPEC-160). Whatever they are, it must end, must
// not read outside them, and must give either a refusal or records that are in order of path and
// whose paths are collection paths (SPEC-154).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "spz/collection.h"
#include "spz/index.h"
#include "spz/paths.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const spz::Bytes file(data, data + size);
    spz::IndexHeader header;
    std::vector<spz::IndexRecord> records;
    if (!spz::read_index(file, &header, &records)) return 0;
    if (header.format != spz::Index::kFormat) std::abort();
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (!spz::is_collection_path(records[i].path)) std::abort();
        if (i > 0 && !spz::path_comes_before(records[i - 1].path, records[i].path)) std::abort();
        if (records[i].search_text != spz::search_text(records[i].path, records[i].title, records[i].author,
                                                       records[i].released)) {
            std::abort();
        }
    }
    return 0;
}
