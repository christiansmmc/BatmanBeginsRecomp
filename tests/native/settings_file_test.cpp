#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/settings_file.h"
#include "check.h"

namespace fs = std::filesystem;
using app::ReadSettingsFile;
using app::SettingsFile;

namespace {

fs::path g_dir;

fs::path Write(const std::string& text) {
    fs::path file = g_dir / "settings.toml";
    std::ofstream(file, std::ios::binary) << text;
    return file;
}

std::string Bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

void Fresh() {
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);
}

void AMissingFileIsAFirstRun() {
    Fresh();
    auto read = ReadSettingsFile(g_dir / "settings.toml");
    CHECK(read.state == SettingsFile::Missing);
    CHECK(read.problem.empty());
    CHECK(!fs::exists(g_dir / "settings.toml.bad"));
}

void ABrokenFileIsKeptAndSaysWhere() {
    Fresh();
    const std::string text = "width = 1280\nheight = = 720\nvsync = true\n";
    fs::path file = Write(text);
    auto read = ReadSettingsFile(file);
    CHECK(read.state == SettingsFile::Broken);
    CHECK(read.problem.find("line 2") != std::string::npos);
    CHECK(read.table.empty());
    CHECK(Bytes(g_dir / "settings.toml.bad") == text);
    CHECK(Bytes(file) == text);  // the file itself stays until the options screen writes it
}

void TheKeyAPlayerAddsTwiceIsBroken() {  // a player who adds a line by hand
    Fresh();
    auto read = ReadSettingsFile(Write("experimental_play_fps = 60\nexperimental_play_fps = 30\n"));
    CHECK(read.state == SettingsFile::Broken);
    CHECK(read.problem.find("line 2") != std::string::npos);
    CHECK(fs::exists(g_dir / "settings.toml.bad"));
}

void ANewerBrokenFileReplacesTheCopy() {
    Fresh();
    ReadSettingsFile(Write("a = = 1\n"));
    ReadSettingsFile(Write("b = = 2\n"));
    CHECK(Bytes(g_dir / "settings.toml.bad") == "b = = 2\n");
}

void AValidFileIsRead() {
    Fresh();
    auto read = ReadSettingsFile(Write("display_mode = \"windowed\"\nwidth = 1280\nvsync = false\n"));
    CHECK(read.state == SettingsFile::Valid);
    CHECK(read.problem.empty());
    CHECK(read.table["display_mode"].value_or(std::string()) == "windowed");
    CHECK(read.table["width"].value_or(0) == 1280);
    CHECK(read.table["vsync"].value_or(true) == false);
    CHECK(!fs::exists(g_dir / "settings.toml.bad"));
}

void AnEmptyFileIsValid() {
    Fresh();
    auto read = ReadSettingsFile(Write(""));
    CHECK(read.state == SettingsFile::Valid);
    CHECK(read.table.empty());
}

}  // namespace

int main() {
    // a folder with an accent: a player's folder may have one, and toml++ takes a plain string as UTF-8
    g_dir = fs::temp_directory_path() / L"bb settings ç test";
    AMissingFileIsAFirstRun();
    ABrokenFileIsKeptAndSaysWhere();
    TheKeyAPlayerAddsTwiceIsBroken();
    ANewerBrokenFileReplacesTheCopy();
    AValidFileIsRead();
    AnEmptyFileIsValid();
    fs::remove_all(g_dir);
    return g_failures;
}
