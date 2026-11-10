#include <cstdint>
#include <vector>

#include "check.h"
#include "xdk/vertex_program.h"

namespace {

using d3d::VertexProgramMemory;

// a program of n instructions of 4 dwords, tagged, whose last instruction has the end bit
std::vector<uint32_t> Program(uint32_t tag, uint32_t instructions) {
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < instructions; i++) {
        words.insert(words.end(), {tag, i, 0, i + 1 == instructions ? VertexProgramMemory::kLastInstruction : 0u});
    }
    return words;
}

void TestProgramEndsAtItsLastInstruction() {
    VertexProgramMemory memory;
    memory.Store(0, Program(1, 7));
    memory.Store(0, Program(2, 3));
    CHECK(memory.ProgramAt(0) == Program(2, 3));
    // what the shorter program did not overwrite is still there
    auto rest = memory.ProgramAt(3);
    CHECK(rest.size() == 4 * VertexProgramMemory::kInstructionDwords);
    CHECK(rest[0] == 1);
}

// SetVertexShader is Load at slot 0 and Select at slot 0. A title may then Load alone at slot 0: the hardware runs what
// is in that memory now (0x21190E in Batman Begins does this for its world shaders).
void TestLoadOverTheRunningProgramChangesWhatRuns() {
    VertexProgramMemory memory;
    CHECK(!memory.Store(0, Program(1, 3)));
    memory.Run(0);
    CHECK(memory.Store(0, Program(2, 51)));
    CHECK(memory.Running());
    CHECK(memory.ProgramAt(memory.Start()) == Program(2, 51));
}

void TestLoadElsewhereLeavesTheRunningProgram() {
    VertexProgramMemory memory;
    memory.Store(0, Program(1, 3));
    memory.Run(0);
    CHECK(!memory.Store(3, Program(2, 4)));
    CHECK(memory.ProgramAt(memory.Start()) == Program(1, 3));
}

void TestLoadThatEndsBeforeTheRunningProgramLeavesIt() {
    VertexProgramMemory memory;
    memory.Store(10, Program(1, 3));
    memory.Run(10);
    CHECK(!memory.Store(8, Program(2, 2)));
    CHECK(memory.Store(8, Program(3, 3)));  // its last instruction falls on the running program's first
}

void TestFixedFunctionRunsNoProgram() {
    VertexProgramMemory memory;
    memory.Store(0, Program(1, 3));
    memory.Run(0);
    memory.Stop();
    CHECK(!memory.Running());
    CHECK(!memory.Store(0, Program(2, 3)));
}

void TestStoreStopsAtTheEndOfTheMemory() {
    VertexProgramMemory memory;
    CHECK(!memory.Store(VertexProgramMemory::kSlots, Program(1, 1)));
    memory.Store(VertexProgramMemory::kSlots - 1, Program(2, 2));
    CHECK(memory.ProgramAt(VertexProgramMemory::kSlots).empty());
    CHECK(memory.ProgramAt(VertexProgramMemory::kSlots - 1).size() == VertexProgramMemory::kInstructionDwords);
}

}  // namespace

int main() {
    TestProgramEndsAtItsLastInstruction();
    TestLoadOverTheRunningProgramChangesWhatRuns();
    TestLoadElsewhereLeavesTheRunningProgram();
    TestLoadThatEndsBeforeTheRunningProgramLeavesIt();
    TestFixedFunctionRunsNoProgram();
    TestStoreStopsAtTheEndOfTheMemory();
    return g_failures;
}
