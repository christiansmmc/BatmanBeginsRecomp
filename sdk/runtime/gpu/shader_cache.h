// Compiled shaders kept in a file beside the program. D3DCompile takes tens of milliseconds per shader, and the game
// asks for its shaders at draw time, so a first sight of one shows as a hitch. Each record carries its own check: a file
// cut short or damaged loses the records from that point on, which then compile again, and is never handed out as code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gpu {

// a shader's identity: everything D3DCompile is given that varies per shader
uint64_t ShaderKey(std::string_view source, std::string_view entry, std::string_view profile);

class ShaderCache {
public:
    // reads what the file holds; a different version or a file that is not a cache counts as empty and is replaced
    ShaderCache(std::filesystem::path file, uint32_t version);

    const std::vector<uint8_t>* Find(uint64_t key) const;
    // kept for this run in any case, and in the file at once, so that a killed run loses at most the record being written
    void Add(uint64_t key, const void* code, size_t size);
    size_t Count() const { return records_.size(); }
    // bytes of the file past its last good record, dropped at start
    uint64_t Dropped() const { return dropped_; }
    bool Writable() const { return out_.is_open() && out_.good(); }

private:
    void Load(uint32_t version);

    std::filesystem::path file_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> records_;
    std::ofstream out_;
    uint64_t dropped_ = 0;
};

}  // namespace gpu
