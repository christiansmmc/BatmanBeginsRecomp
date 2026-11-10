// The movie file names in the paths the game opens, for the test probe in src/game/movie_probe.cpp.
#pragma once
#include <string>
#include <string_view>

inline std::string_view MovieFileName(std::string_view path) {
    size_t slash = path.find_last_of('\\');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// the same folder, another file
inline std::string RedirectMoviePath(std::string_view path, std::string_view file) {
    std::string redirected(path.substr(0, path.size() - MovieFileName(path).size()));
    redirected += file;
    return redirected;
}
