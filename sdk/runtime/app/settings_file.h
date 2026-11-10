// Reading the settings file, apart from settings.cpp, which needs the game and its folder.
#pragma once
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include <toml++/toml.hpp>

namespace app {

enum class SettingsFile { Missing, Broken, Valid };

struct SettingsRead {
    SettingsFile state = SettingsFile::Missing;
    toml::table table;    // only a valid file has one
    std::string problem;  // a broken file: what is wrong and on which line
};

// A missing file is a first run, with nothing to say. A broken one goes to <file>.bad before anything can write over it: the
// next change on the options screen writes the whole file again, with the defaults in place of what could not be read.
inline SettingsRead ReadSettingsFile(const std::filesystem::path& file) {
    SettingsRead read;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) && !ec) return read;  // toml++ raises the same error for a missing file
    try {
        read.table = toml::parse_file(file.u8string());  // a plain string it takes as UTF-8, and string() is the system's code page
        read.state = SettingsFile::Valid;
        return read;
    } catch (const toml::parse_error& e) {
        read.problem = std::string(e.description()) + ", line " + std::to_string(e.source().begin.line);
    } catch (const std::exception& e) {
        read.problem = e.what();
    }
    read.state = SettingsFile::Broken;
    std::filesystem::path bad = file;
    bad += ".bad";
    std::filesystem::copy_file(file, bad, std::filesystem::copy_options::overwrite_existing, ec);
    read.problem += ec ? ", and it could not be copied to " + bad.filename().string() : ", kept as " + bad.filename().string();
    return read;
}

}  // namespace app
