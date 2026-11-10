#include "gpu/shader_cache.h"

#include <cstring>
#include <iterator>
#include <system_error>

#include "gpu/shaders.h"

namespace gpu {
namespace {

constexpr char kMagic[8] = {'X', 'B', 'E', 'S', 'H', 'A', 'D', 'R'};

struct FileHeader {
    char magic[8];
    uint32_t version;
    uint32_t zero;
};
static_assert(sizeof(FileHeader) == 16);

struct RecordHeader {
    uint64_t key;
    uint32_t size;
    uint32_t zero;
    uint64_t check;
};
static_assert(sizeof(RecordHeader) == 24);

// the key and size are in it too: a bad byte in either must not pass for another record
uint64_t RecordCheck(uint64_t key, const void* code, size_t size) {
    uint64_t seed = Hash(&key, sizeof(key));
    uint64_t length = size;
    seed = Hash(&length, sizeof(length), seed);
    return Hash(code, size, seed);
}

FileHeader MakeHeader(uint32_t version) {
    FileHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = version;
    return header;
}

}  // namespace

uint64_t ShaderKey(std::string_view source, std::string_view entry, std::string_view profile) {
    // one source can hold several entry points
    uint64_t profile_seed = Hash(profile.data(), profile.size());
    uint64_t entry_seed = Hash(entry.data(), entry.size(), profile_seed);
    return Hash(source.data(), source.size(), entry_seed);
}

ShaderCache::ShaderCache(std::filesystem::path file, uint32_t version) : file_(std::move(file)) {
    Load(version);
}

void ShaderCache::Load(uint32_t version) {
    std::vector<char> bytes;
    {
        std::ifstream in(file_, std::ios::binary);
        if (in) bytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    FileHeader expected = MakeHeader(version);
    bool ours = bytes.size() >= sizeof(FileHeader) && std::memcmp(bytes.data(), &expected, sizeof(expected)) == 0;
    size_t good = 0;
    if (ours) {
        good = sizeof(FileHeader);
        while (bytes.size() - good >= sizeof(RecordHeader)) {
            RecordHeader record;
            std::memcpy(&record, bytes.data() + good, sizeof(record));
            size_t body = good + sizeof(record);
            if (record.zero != 0 || record.size > bytes.size() - body) break;
            if (RecordCheck(record.key, bytes.data() + body, record.size) != record.check) break;
            auto* code = reinterpret_cast<const uint8_t*>(bytes.data() + body);
            records_[record.key].assign(code, code + record.size);
            good = body + record.size;
        }
    }
    dropped_ = bytes.size() - good;

    // what follows the damage is dropped, or new records would land behind it and never load
    if (ours && good < bytes.size()) {
        std::error_code ignored;
        std::filesystem::resize_file(file_, good, ignored);
    }
    if (ours) {
        out_.open(file_, std::ios::binary | std::ios::app);
    } else {
        out_.open(file_, std::ios::binary | std::ios::trunc);
        out_.write(reinterpret_cast<const char*>(&expected), sizeof(expected));
        out_.flush();
    }
}

const std::vector<uint8_t>* ShaderCache::Find(uint64_t key) const {
    auto it = records_.find(key);
    return it == records_.end() ? nullptr : &it->second;
}

void ShaderCache::Add(uint64_t key, const void* code, size_t size) {
    auto* bytes = static_cast<const uint8_t*>(code);
    records_[key].assign(bytes, bytes + size);
    if (!Writable()) return;
    RecordHeader record{key, uint32_t(size), 0, RecordCheck(key, code, size)};
    out_.write(reinterpret_cast<const char*>(&record), sizeof(record));
    out_.write(static_cast<const char*>(code), std::streamsize(size));
    out_.flush();
}

}  // namespace gpu
