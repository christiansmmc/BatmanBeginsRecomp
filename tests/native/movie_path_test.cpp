#include <string>

#include "check.h"
#include "common/movie_path.h"

namespace {

void TestFileNameIsWhatFollowsTheLastBackslash() {
    CHECK(MovieFileName(R"(D:\Batman\Binary\_bin_xb\PSS_US\EA_US.xmv)") == "EA_US.xmv");
    CHECK(MovieFileName("EA_US.xmv") == "EA_US.xmv");
}

void TestRedirectKeepsTheFolderAndChangesTheFile() {
    CHECK(RedirectMoviePath(R"(D:\Batman\PSS_US\EA_US.xmv)", "int1_us.xmv") == R"(D:\Batman\PSS_US\int1_us.xmv)");
    CHECK(RedirectMoviePath("EA_US.xmv", "tt_us.xmv") == "tt_us.xmv");
}

}  // namespace

int main() {
    TestFileNameIsWhatFollowsTheLastBackslash();
    TestRedirectKeepsTheFolderAndChangesTheFile();
    return g_failures;
}
