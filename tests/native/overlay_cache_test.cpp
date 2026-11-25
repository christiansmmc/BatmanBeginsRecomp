#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "gpu/overlay_cache.h"

using gpu::OverlayCache;

namespace {

constexpr uint64_t kFont = 0xe17fcca2edd00edbull;
constexpr uint64_t kHud = 0x3020a8ad19ca2b71ull;
constexpr uint32_t kConsole = 1;  // key_prompts' revision while the console's pictures show
constexpr uint32_t kKeys = 0x1234;
constexpr uint32_t kOtherKeys = 0x5678;

bool Contains(const std::vector<uint64_t>& keys, uint64_t key) { return std::find(keys.begin(), keys.end(), key) != keys.end(); }

// a picture serves only at the revision it was painted at
void TestServesItsRevision() {
    OverlayCache<std::string> cache;
    cache.Latch(kKeys);
    cache.Put(kFont, kKeys, "keys");
    CHECK(cache.Has(kFont));
    CHECK(!cache.Has(kHud));
    const std::string* got = cache.Hand(kFont);
    CHECK(got && *got == "keys");
    cache.Latch(kConsole);
    CHECK(!cache.Has(kFont));
    CHECK(cache.Hand(kFont) == nullptr);
}

// the keyboard, the controller and the keyboard again with the same keys: the picture comes back without painting, and the
// one handed out under the old revision is stale once
void TestSwitchBackPaintsNothing() {
    OverlayCache<std::string> cache;
    cache.Latch(kKeys);
    cache.Put(kFont, kKeys, "keys");
    CHECK(cache.Hand(kFont));
    const uint32_t before = cache.Generation();

    CHECK(cache.Latch(kConsole));
    CHECK(cache.Generation() != before);
    std::vector<uint64_t> stale = cache.TakeStale();
    CHECK(stale.size() == 1 && stale[0] == kFont);
    CHECK(cache.TakeStale().empty());

    CHECK(cache.Latch(kKeys));
    CHECK(cache.Has(kFont));                // nothing to paint
    CHECK(cache.TakeStale().empty());       // nothing was handed out under the console's revision
    CHECK(*cache.Hand(kFont) == "keys");
}

// only the overlay's keys are ever stale: a texture pack image never goes through the cache, so a repaint cannot drop it
void TestStaleIsOnlyWhatWasHandedOut() {
    OverlayCache<std::string> cache;
    cache.Latch(kKeys);
    cache.Put(kFont, kKeys, "font");
    cache.Put(kHud, kKeys, "hud");
    CHECK(cache.Hand(kFont));  // the HUD sheet was painted but never drawn
    cache.Latch(kOtherKeys);
    const std::vector<uint64_t> stale = cache.TakeStale();
    CHECK(stale.size() == 1);
    CHECK(Contains(stale, kFont));
    CHECK(!Contains(stale, kHud));
}

// the same revision again changes nothing
void TestSameRevision() {
    OverlayCache<std::string> cache;
    CHECK(cache.Latch(kKeys));
    cache.Put(kFont, kKeys, "keys");
    CHECK(cache.Hand(kFont));
    const uint32_t generation = cache.Generation();
    CHECK(!cache.Latch(kKeys));
    CHECK(cache.Generation() == generation);
    CHECK(cache.TakeStale().empty());
}

// a picture painted for a revision not latched yet serves once it is
void TestPaintedAhead() {
    OverlayCache<std::string> cache;
    cache.Latch(kConsole);
    cache.Put(kFont, kKeys, "keys");
    CHECK(!cache.Has(kFont));
    cache.Latch(kKeys);
    CHECK(cache.Has(kFont));
}

// the pack listed again drops every GPU copy: nothing old is left to let go of
void TestForget() {
    OverlayCache<std::string> cache;
    cache.Latch(kKeys);
    cache.Put(kFont, kKeys, "keys");
    CHECK(cache.Hand(kFont));
    cache.Forget();
    cache.Latch(kOtherKeys);
    CHECK(cache.TakeStale().empty());
}

// a repaint for new keys replaces the kept picture
void TestRepaint() {
    OverlayCache<std::string> cache;
    cache.Latch(kKeys);
    cache.Put(kFont, kKeys, "old");
    cache.Latch(kOtherKeys);
    cache.Put(kFont, kOtherKeys, "new");
    CHECK(*cache.Hand(kFont) == "new");
    cache.Latch(kKeys);
    CHECK(!cache.Has(kFont));  // only the last picture is kept
}

}  // namespace

int main() {
    TestServesItsRevision();
    TestSwitchBackPaintsNothing();
    TestStaleIsOnlyWhatWasHandedOut();
    TestSameRevision();
    TestPaintedAhead();
    TestForget();
    TestRepaint();
    return g_failures;
}
