// The overlay's pictures (texturedumper.h, Overlay) by texture key, each with the revision it was painted at. A picture serves
// while its revision is the current one and is kept when the revision moves on, so a revision that comes back (the keyboard
// after the controller, with the same keys) paints nothing. The keys handed out under a revision become stale when it
// changes: their GPU copies are let go of, and only theirs, never the texture pack's. The render thread's.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gpu {

template <class Image>
class OverlayCache {
public:
    // the overlay's revision as Update reads it; true when it changed
    bool Latch(uint32_t revision) {
        if (revision == revision_) return false;
        revision_ = revision;
        generation_++;
        stale_.insert(stale_.end(), handed_out_.begin(), handed_out_.end());
        handed_out_.clear();
        return true;
    }
    uint32_t Revision() const { return revision_; }
    // moves on with each change of revision
    uint32_t Generation() const { return generation_; }

    // a picture painted at the current revision
    bool Has(uint64_t key) const {
        auto it = made_.find(key);
        return it != made_.end() && it->second.first == revision_;
    }
    // `revision` is the one paint saw, which may be ahead of the latched one: the picture serves once it is latched
    void Put(uint64_t key, uint32_t revision, Image image) {
        made_.insert_or_assign(key, std::pair<uint32_t, Image>{revision, std::move(image)});
    }
    // the picture for the current revision, noted as handed out; null when there is none
    const Image* Hand(uint64_t key) {
        if (!Has(key)) return nullptr;
        handed_out_.insert(key);
        return &made_.find(key)->second.second;
    }

    // every GPU copy went (the pack was listed again): none of the old ones is left to let go of
    void Forget() {
        handed_out_.clear();
        stale_.clear();
    }
    // the keys whose GPU copies are older than the revision, once
    std::vector<uint64_t> TakeStale() { return std::exchange(stale_, {}); }

private:
    uint32_t revision_ = 0;
    uint32_t generation_ = 1;
    std::unordered_map<uint64_t, std::pair<uint32_t, Image>> made_;
    std::unordered_set<uint64_t> handed_out_;
    std::vector<uint64_t> stale_;
};

}  // namespace gpu
