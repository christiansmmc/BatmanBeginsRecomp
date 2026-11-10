#include <cstdint>
#include <vector>

#include "check.h"
#include "xdk/vertex_input.h"

namespace {

constexpr uint32_t kFloat3 = 0x32, kColor = 0x40, kNone = 0x02;

// 16 D3DVERTEXSHADERINPUT of 16 bytes: stream, offset, format, tessellation
std::vector<uint32_t> EmptyTable() {
    std::vector<uint32_t> table(64, 0);
    for (int i = 0; i < 16; i++) table[size_t(i) * 4 + 2] = kNone;
    return table;
}

void SetAttribute(std::vector<uint32_t>& table, int reg, uint32_t stream, uint32_t offset, uint32_t format) {
    table[size_t(reg) * 4] = stream;
    table[size_t(reg) * 4 + 1] = offset;
    table[size_t(reg) * 4 + 2] = format;
}

void TestAttributesAddTheStreamOffset() {
    auto table = EmptyTable();
    SetAttribute(table, 0, 0, 0, kFloat3);
    SetAttribute(table, 3, 1, 12, kColor);
    // D3DSTREAM_INPUT of 12 bytes: vertex buffer, stride, offset
    const uint32_t streams[] = {0x80001000, 24, 0, 0x80002000, 32, 8};
    auto layout = d3d::ParseVertexInput(table.data(), streams);
    CHECK(layout.inputs[0].stream == 0);
    CHECK(layout.inputs[0].offset == 0);
    CHECK(layout.inputs[0].type == kFloat3);
    CHECK(layout.inputs[3].stream == 1);
    CHECK(layout.inputs[3].offset == 20);  // 12 in the vertex + 8 into the stream
    CHECK(layout.inputs[3].type == kColor);
    CHECK(layout.streams[0].vb == 0x80001000);
    CHECK(layout.streams[0].stride == 24);
    CHECK(layout.streams[1].vb == 0x80002000);
    CHECK(layout.streams[1].stride == 32);
    CHECK(layout.streams[2].vb == 0);
}

void TestUnusedRegistersHaveNoStream() {
    auto table = EmptyTable();
    const uint32_t streams[] = {0x80001000, 24, 0};
    auto layout = d3d::ParseVertexInput(table.data(), streams);
    for (const auto& input : layout.inputs) CHECK(input.stream == -1);
}

// the game skips empty streams when it counts them (0x250340), and the library reads the array by index, not by count
void TestStreamIndexIsNotLimitedByTheCount() {
    auto table = EmptyTable();
    SetAttribute(table, 1, 2, 4, kFloat3);
    const uint32_t streams[] = {0x80001000, 24, 0, 0, 0, 0, 0x80003000, 16, 8};
    auto layout = d3d::ParseVertexInput(table.data(), streams);
    CHECK(layout.inputs[1].stream == 2);
    CHECK(layout.inputs[1].offset == 12);
    CHECK(layout.streams[2].vb == 0x80003000);
    CHECK(layout.streams[2].stride == 16);
}

void TestStreamIndexBeyondTheDeviceIsIgnored() {
    auto table = EmptyTable();
    SetAttribute(table, 1, 16, 0, kFloat3);
    const uint32_t streams[] = {0x80001000, 24, 0};
    auto layout = d3d::ParseVertexInput(table.data(), streams);
    CHECK(layout.inputs[1].stream == -1);
}

void TestOnlyTheLowByteOfTheFormatIsTheType() {
    auto table = EmptyTable();
    SetAttribute(table, 0, 0, 0, 0x1832);
    const uint32_t streams[] = {0x80001000, 24, 0};
    CHECK(d3d::ParseVertexInput(table.data(), streams).inputs[0].type == kFloat3);
}

}  // namespace

int main() {
    TestAttributesAddTheStreamOffset();
    TestUnusedRegistersHaveNoStream();
    TestStreamIndexIsNotLimitedByTheCount();
    TestStreamIndexBeyondTheDeviceIsIgnored();
    TestOnlyTheLowByteOfTheFormatIsTheType();
    return g_failures;
}
