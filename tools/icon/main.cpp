// xbe_icon <default.xbe> <out.ico>: the program's icon, from the title image the console's dashboard shows for the game,
// and its save image for the small sizes
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "xbe_icon.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: xbe_icon <default.xbe> <out.ico>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> xbe{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto section = xbe_icon::Section(xbe, "$$XTIMAGE");
    if (!section) {
        std::fprintf(stderr, "xbe_icon: %s has no $$XTIMAGE section\n", argv[1]);
        return 1;
    }
    auto image = xbe_icon::DecodeXpr(*section);
    if (!image) {
        std::fprintf(stderr, "xbe_icon: the title image in %s is not one DXT1 texture\n", argv[1]);
        return 1;
    }
    xbe_icon::ClearWhiteBackground(*image);
    std::optional<xbe_icon::Image> save;
    if (const auto saved = xbe_icon::Section(xbe, "$$XSIMAGE")) save = xbe_icon::DecodeXpr(*saved);
    if (!save) std::fprintf(stderr, "xbe_icon: no save image in %s, the small sizes use the logo too\n", argv[1]);
    const std::vector<uint8_t> ico = xbe_icon::Ico(xbe_icon::IconSizes(*image, save));
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(ico.data()), std::streamsize(ico.size()));
    return out ? 0 : 1;
}
