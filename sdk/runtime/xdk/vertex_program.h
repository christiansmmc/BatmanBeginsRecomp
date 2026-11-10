// The console's vertex program memory. Load writes instructions at a slot and Select starts the program at a slot, so
// a Load over the program that runs changes what the next draw runs, with no Select: SetVertexShader is Load and Select
// at slot 0, and a title may then only Load at slot 0 (0x21190E in Batman Begins).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace d3d {

class VertexProgramMemory {
public:
    static constexpr uint32_t kSlots = 136;
    static constexpr uint32_t kInstructionDwords = 4;
    static constexpr uint32_t kLastInstruction = 1;  // bit 0 of an instruction's fourth dword

    // true when it wrote over the program that runs
    bool Store(uint32_t slot, const std::vector<uint32_t>& words) {
        if (slot >= kSlots) return false;
        size_t room = memory_.size() - slot * kInstructionDwords;
        size_t count = std::min(words.size(), room);
        size_t last = slot + count / kInstructionDwords;  // one past the written instructions
        bool over = running_ && slot < start_ + ProgramAt(start_).size() / kInstructionDwords && last > start_;
        std::copy_n(words.begin(), count, memory_.begin() + slot * kInstructionDwords);
        return over;
    }

    // the instructions from slot to the last one
    std::vector<uint32_t> ProgramAt(uint32_t slot) const {
        std::vector<uint32_t> program;
        for (; slot < kSlots; slot++) {
            const uint32_t* insn = &memory_[slot * kInstructionDwords];
            program.insert(program.end(), insn, insn + kInstructionDwords);
            if (insn[3] & kLastInstruction) break;
        }
        return program;
    }

    void Run(uint32_t slot) {
        start_ = slot;
        running_ = true;
    }
    // the fixed function pipeline, or a shader without a program
    void Stop() { running_ = false; }
    bool Running() const { return running_; }
    uint32_t Start() const { return start_; }

private:
    std::array<uint32_t, kSlots * kInstructionDwords> memory_{};
    uint32_t start_ = 0;
    bool running_ = false;
};

}  // namespace d3d
