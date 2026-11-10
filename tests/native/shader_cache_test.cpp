#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "gpu/shader_cache.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kVersion = 7;

const std::vector<uint8_t> kFirst{1, 2, 3, 4, 5, 6, 7, 8, 9};
const std::vector<uint8_t> kSecond{200, 201, 202};
constexpr uint64_t kFirstKey = 0x1111222233334444ull;
constexpr uint64_t kSecondKey = 0x5555666677778888ull;

fs::path NewFolder(const char* name) {
    fs::path folder = fs::temp_directory_path() / "batman_shader_cache_test" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

std::vector<uint8_t> ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void WriteAll(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

// a file with both records, as a run leaves it
std::vector<uint8_t> FilledFile(const fs::path& path) {
    {
        gpu::ShaderCache cache(path, kVersion);
        cache.Add(kFirstKey, kFirst.data(), kFirst.size());
        cache.Add(kSecondKey, kSecond.data(), kSecond.size());
    }
    return ReadAll(path);
}

bool Holds(const gpu::ShaderCache& cache, uint64_t key, const std::vector<uint8_t>& bytes) {
    const std::vector<uint8_t>* found = cache.Find(key);
    return found && *found == bytes;
}

// whatever came back is what was stored: a damaged file may lose records, never hand out wrong code
bool OnlyGoodRecords(const gpu::ShaderCache& cache) {
    const std::vector<uint8_t>* first = cache.Find(kFirstKey);
    const std::vector<uint8_t>* second = cache.Find(kSecondKey);
    return (!first || *first == kFirst) && (!second || *second == kSecond);
}

void TestWhatWasAddedComesBackAfterARestart() {
    fs::path path = NewFolder("restart") / "shaders.cache";
    FilledFile(path);
    gpu::ShaderCache cache(path, kVersion);
    CHECK(cache.Count() == 2);
    CHECK(Holds(cache, kFirstKey, kFirst));
    CHECK(Holds(cache, kSecondKey, kSecond));
    CHECK(!cache.Find(0x42));
}

void TestWhatWasAddedIsFoundBeforeARestart() {
    fs::path path = NewFolder("same_run") / "shaders.cache";
    gpu::ShaderCache cache(path, kVersion);
    cache.Add(kFirstKey, kFirst.data(), kFirst.size());
    CHECK(Holds(cache, kFirstKey, kFirst));
}

void TestAMissingFileIsAnEmptyCache() {
    fs::path path = NewFolder("missing") / "shaders.cache";
    gpu::ShaderCache cache(path, kVersion);
    CHECK(cache.Count() == 0);
    CHECK(!cache.Find(kFirstKey));
    cache.Add(kFirstKey, kFirst.data(), kFirst.size());
    gpu::ShaderCache again(path, kVersion);
    CHECK(Holds(again, kFirstKey, kFirst));
}

// the game killed while writing leaves a file cut anywhere
void TestAFileCutAtAnyLengthKeepsItsWholeRecords() {
    fs::path path = NewFolder("cut") / "shaders.cache";
    std::vector<uint8_t> whole = FilledFile(path);
    for (size_t length = 0; length < whole.size(); length++) {
        WriteAll(path, {whole.begin(), whole.begin() + std::ptrdiff_t(length)});
        {
            gpu::ShaderCache cache(path, kVersion);
            CHECK(OnlyGoodRecords(cache));
            CHECK(!cache.Find(kSecondKey));  // the last record is the one that lost bytes
            cache.Add(kSecondKey, kSecond.data(), kSecond.size());
        }
        // what is added after the damage is not lost behind it
        gpu::ShaderCache again(path, kVersion);
        CHECK(Holds(again, kSecondKey, kSecond));
        CHECK(OnlyGoodRecords(again));
    }
}

void TestABadByteAnywhereNeverGivesWrongCode() {
    fs::path path = NewFolder("flip") / "shaders.cache";
    std::vector<uint8_t> whole = FilledFile(path);
    for (size_t at = 0; at < whole.size(); at++) {
        for (uint8_t mask : {uint8_t(0x01), uint8_t(0x80), uint8_t(0xFF)}) {
            std::vector<uint8_t> damaged = whole;
            damaged[at] ^= mask;
            WriteAll(path, damaged);
            {
                gpu::ShaderCache cache(path, kVersion);
                CHECK(OnlyGoodRecords(cache));
                CHECK(cache.Count() < 2);
                cache.Add(kSecondKey, kSecond.data(), kSecond.size());
            }
            gpu::ShaderCache again(path, kVersion);
            CHECK(Holds(again, kSecondKey, kSecond));
        }
    }
}

void TestARecordClaimingAHugeSizeIsRejected() {
    fs::path path = NewFolder("huge") / "shaders.cache";
    std::vector<uint8_t> whole = FilledFile(path);
    // the first record's size follows the 16 byte header and its 8 byte key
    for (size_t i = 24; i < 28; i++) whole[i] = 0xFF;
    WriteAll(path, whole);
    gpu::ShaderCache cache(path, kVersion);
    CHECK(cache.Count() == 0);
}

void TestAnotherFileOrVersionIsIgnored() {
    fs::path folder = NewFolder("other");
    fs::path path = folder / "shaders.cache";
    WriteAll(path, std::vector<uint8_t>(5000, 0x5A));
    {
        gpu::ShaderCache cache(path, kVersion);
        CHECK(cache.Count() == 0);
        cache.Add(kFirstKey, kFirst.data(), kFirst.size());
    }
    {
        gpu::ShaderCache cache(path, kVersion);
        CHECK(Holds(cache, kFirstKey, kFirst));
    }
    {
        gpu::ShaderCache cache(path, kVersion + 1);
        CHECK(cache.Count() == 0);
        cache.Add(kSecondKey, kSecond.data(), kSecond.size());
    }
    gpu::ShaderCache cache(path, kVersion + 1);
    CHECK(Holds(cache, kSecondKey, kSecond));
    CHECK(!cache.Find(kFirstKey));
}

void TestAFolderThatCannotBeWrittenStillWorksForTheRun() {
    fs::path path = NewFolder("no_folder") / "not_there" / "shaders.cache";
    gpu::ShaderCache cache(path, kVersion);
    cache.Add(kFirstKey, kFirst.data(), kFirst.size());
    CHECK(Holds(cache, kFirstKey, kFirst));
    CHECK(!fs::exists(path));
}

void TestAFolderWithSpacesAndAccents() {
    fs::path folder = NewFolder("accents") / L"Café Gâmes Ünïcode";
    fs::create_directories(folder);
    fs::path path = folder / "shaders.cache";
    FilledFile(path);
    gpu::ShaderCache cache(path, kVersion);
    CHECK(Holds(cache, kFirstKey, kFirst));
}

void TestTheKeyTakesSourceEntryAndProfile() {
    uint64_t key = gpu::ShaderKey("float4 main() : SV_Target { return 0; }", "main", "ps_5_0");
    CHECK(key == gpu::ShaderKey("float4 main() : SV_Target { return 0; }", "main", "ps_5_0"));
    CHECK(key != gpu::ShaderKey("float4 main() : SV_Target { return 1; }", "main", "ps_5_0"));
    CHECK(key != gpu::ShaderKey("float4 main() : SV_Target { return 0; }", "other", "ps_5_0"));
    CHECK(key != gpu::ShaderKey("float4 main() : SV_Target { return 0; }", "main", "ps_4_0"));
}

}  // namespace

int main() {
    TestWhatWasAddedComesBackAfterARestart();
    TestWhatWasAddedIsFoundBeforeARestart();
    TestAMissingFileIsAnEmptyCache();
    TestAFileCutAtAnyLengthKeepsItsWholeRecords();
    TestABadByteAnywhereNeverGivesWrongCode();
    TestARecordClaimingAHugeSizeIsRejected();
    TestAnotherFileOrVersionIsIgnored();
    TestAFolderThatCannotBeWrittenStillWorksForTheRun();
    TestAFolderWithSpacesAndAccents();
    TestTheKeyTakesSourceEntryAndProfile();
    return g_failures;
}
