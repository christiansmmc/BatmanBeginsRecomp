#include "check.h"
#include "common/movie_skip.h"

namespace {

void TestHoldingForASecondEndsTheMovie() {
    MovieSkip skip;
    CHECK(!skip.Update(true, 0.5));  // held for a short time
    CHECK(skip.Progress() == 0.5f);
    CHECK(skip.Update(true, 0.5));   // reached 1 s
    CHECK(skip.Progress() == 1.0f);
}

void TestLettingGoStartsOver() {
    MovieSkip released;
    released.Update(true, 0.9);
    released.Update(false, 0.1);
    CHECK(released.Progress() == 0.0f);
    CHECK(!released.Update(true, 0.5));
}

void TestAButtonAlreadyDownCountsOnlyAfterItIsLetGo() {
    MovieSkip carried(/*held_at_start=*/true);
    CHECK(!carried.Update(true, 5.0));
    CHECK(carried.Progress() == 0.0f);
    carried.Update(false, 0.1);
    CHECK(!carried.Update(true, 0.5));
    CHECK(carried.Update(true, 0.5));
}

void TestNotHeldNeverEnds() {
    MovieSkip idle;
    for (int i = 0; i < 100; i++) CHECK(!idle.Update(false, 1.0));
    CHECK(idle.Progress() == 0.0f);
}

}  // namespace

int main() {
    TestHoldingForASecondEndsTheMovie();
    TestLettingGoStartsOver();
    TestAButtonAlreadyDownCountsOnlyAfterItIsLetGo();
    TestNotHeldNeverEnds();
    return g_failures;
}
