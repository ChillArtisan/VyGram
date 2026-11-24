import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Telegram/SourceFiles/window/themes/window_theme.cpp"
TEMP = Path(r"D:\AyuGramBuild\temp\terminal_migration")
VCVARS = Path(r"E:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat")

PRELUDE = r'''
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>
using QByteArray = std::string;
std::u16string operator""_q(const char16_t *text, size_t size) {
    return {text, size};
}
struct State {
    bool unlocked = true, night = false, terminal = false;
    bool applyOk = true, toggleOk = true, storeOk = true;
    bool systemDark = true, systemAccent = true;
    std::string marker;
    int reads = 0, stores = 0, apply = 0, toggle = 0, keep = 0;
    int radius = -1, corners = -1, protect = 0, writes = 0, logs = 0;
    bool tail = false, single = false;
    std::u16string font;
    std::vector<std::string> events;
} s;
std::optional<int> FreshProfileTheme = 7;
#define LOG(message) (++s.logs)
namespace AyuDatabase {
bool isUnlocked() { return s.unlocked; }
std::string loadPrivateSetting(const char *key) {
    if (std::string(key) != "terminal.design.version") throw std::runtime_error("read key");
    ++s.reads;
    return s.marker;
}
bool storePrivateSetting(const char *key, const std::string &value) {
    if (std::string(key) != "terminal.design.version") throw std::runtime_error("store key");
    ++s.stores;
    s.events.push_back("marker");
    if (s.storeOk) s.marker = value;
    return s.storeOk;
}
}
namespace Core {
struct Settings {
    void setSystemDarkModeEnabled(bool value) { s.systemDark = value; }
    void setSystemAccentColorEnabled(bool value) { s.systemAccent = value; }
};
struct Application { Settings value; Settings &settings() { return value; } } app;
Application &App() { return app; }
}
namespace AyuSettings {
struct Settings {
    void setMessageBubbleRadius(int value) { s.radius = value; }
    void setRemoveMessageTail(bool value) { s.tail = value; }
    void setAvatarCorners(int value) { s.corners = value; }
    void setSingleCornerRadius(bool value) { s.single = value; }
    void setMonoFont(std::u16string value) { s.font = value; }
} settings;
Settings &getInstance() { return settings; }
}
namespace AyuInfra {
void applyProtectedUiSettings() { ++s.protect; s.events.push_back("protected"); }
}
namespace Local {
void writeSettings() { ++s.writes; s.events.push_back("settings"); }
void sync() { s.events.push_back("sync"); }
}
bool IsNightMode() { return s.night; }
std::string TerminalThemePath() { return "terminal"; }
std::string ColdSteelThemePath() { return "steel"; }
bool IsColdSteelTheme() { return false; }
bool Apply(const std::string &path) {
    if (path != "terminal") throw std::runtime_error("apply path");
    ++s.apply;
    if (s.applyOk) s.terminal = true;
    return s.applyOk;
}
void ToggleNightMode(const std::string &path) {
    if (path != "terminal") throw std::runtime_error("toggle path");
    ++s.toggle;
    if (s.toggleOk) s.terminal = true;
}
bool IsTerminalTheme() { return s.terminal; }
void KeepApplied() { ++s.keep; s.night = true; s.events.push_back("keep"); }
'''

TESTS = r'''
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void reset() { s = State(); FreshProfileTheme = 7; }
void preset() {
    check(s.radius == 2 && s.corners == 2 && s.tail && s.single,
        "preset geometry");
    check(s.font == u"JetBrains Mono", "preset font");
    check(s.protect == 1 && s.writes == 1 && s.keep == 1, "persist preset");
    check(s.marker == "4" && s.stores == 1, "persist marker");
    check(s.night && !s.systemDark && !s.systemAccent && !FreshProfileTheme, "committed state");
    check(s.events == std::vector<std::string>{"keep", "protected", "settings", "sync", "marker"},
        "commit ordering");
}
int run_tests() {
    reset(); s.unlocked = false;
    CommitFreshProfileTheme();
    check(s.reads == 0 && s.stores == 0 && s.apply == 0 && s.toggle == 0,
        "locked profile database and theme untouched");
    check(s.systemDark && s.systemAccent && s.radius == -1 && s.writes == 0 && FreshProfileTheme == 7,
        "locked profile settings and pending theme untouched");
    std::cout << "PASS locked profile\n";
    for (bool night : {false, true}) {
        reset(); s.night = night;
        CommitFreshProfileTheme(); preset();
        check(s.apply == (night ? 1 : 0) && s.toggle == (night ? 0 : 1), "day/night branch");
        std::cout << "PASS existing " << (night ? "night" : "day") << " migration\n";
    }
    for (bool night : {false, true}) {
        reset(); s.marker = "4"; s.night = night;
        CommitFreshProfileTheme();
        check(s.night == night && !s.terminal && s.apply == 0 && s.toggle == 0,
            "marker preserves manual theme");
        check(s.systemDark && s.systemAccent && s.radius == -1 && s.stores == 0 && s.writes == 0,
            "marker preserves manual settings");
        check(!FreshProfileTheme, "marker clears pending bootstrap");
    }
    std::cout << "PASS v2 marker preserves day/night manual choice\n";
    for (bool night : {false, true}) {
        reset(); s.marker = "1"; s.night = night;
        s.radius = 7; s.corners = 9; s.font = u"User Font";
        CommitFreshProfileTheme();
        check(s.night == night && !s.terminal && s.apply == 0 && s.toggle == 0,
            "v1 manual theme remains selected");
        check(s.systemDark && s.systemAccent && s.radius == 7 && s.corners == 9
            && s.font == u"User Font" && !s.tail && !s.single,
            "v1 manual theme preferences untouched");
        check(s.marker == "4" && s.stores == 1 && s.keep == 0 && s.protect == 0
            && s.writes == 0 && s.events == std::vector<std::string>{"marker"},
            "v1 manual theme only advances marker");
    }
    std::cout << "PASS v1 manual theme preserved and marker advanced\n";
    for (bool night : {false, true}) {
        reset(); s.marker = "1"; s.night = night; s.terminal = true;
        s.radius = 7; s.corners = 9; s.font = u"User Font";
        CommitFreshProfileTheme();
        check(s.apply == (night ? 1 : 0) && s.toggle == (night ? 0 : 1)
            && s.keep == 1 && s.marker == "4", "v1 Terminal bundle refreshed");
        check(s.radius == 7 && s.corners == 9 && s.font == u"User Font" && !s.tail
            && !s.single && s.protect == 1, "v1 Terminal keeps user preferences");
        check(s.events == std::vector<std::string>{"keep", "protected", "settings", "sync", "marker"},
            "v1 refresh sync before marker");
    }
    std::cout << "PASS v1 Terminal refresh preserves user preferences\n";
    reset(); s.night = true; s.applyOk = false;
    CommitFreshProfileTheme();
    check(s.stores == 0 && s.marker.empty() && s.keep == 0 && s.writes == 0,
        "Apply failure cannot mark or keep migration");
    check(s.radius == -1 && FreshProfileTheme == 7, "Apply failure leaves preset pending");
    std::cout << "PASS Apply failure leaves marker absent\n";
    reset(); s.marker = "1"; s.night = true; s.terminal = true; s.applyOk = false;
    CommitFreshProfileTheme();
    check(s.marker == "1" && s.stores == 0 && s.keep == 0 && s.writes == 0,
        "v1 Terminal Apply failure does not advance marker");
    std::cout << "PASS failed v1 Terminal refresh retains marker1\n";
    reset(); s.toggleOk = false;
    CommitFreshProfileTheme();
    check(s.stores == 0 && s.keep == 0 && s.writes == 0, "failed day toggle not committed");
    std::cout << "PASS day toggle rejection leaves marker absent\n";
    reset(); s.night = true; s.storeOk = false;
    CommitFreshProfileTheme();
    check(s.stores == 1 && s.marker.empty() && s.logs == 1 && s.writes == 1,
        "marker write failure is logged after preset commit");
    std::cout << "PASS marker write failure logged\n";
    return 0;
}
int main() {
    try { return run_tests(); }
    catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
'''


def main():
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("void CommitFreshProfileTheme() {")
    depth, end = 0, None
    for position in range(text.index("{", start), len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                end = position + 1
                break
    if end is None:
        raise RuntimeError("Cannot extract CommitFreshProfileTheme")
    TEMP.mkdir(parents=True, exist_ok=True)
    (TEMP / "harness.cpp").write_text(PRELUDE + text[start:end] + TESTS, encoding="utf-8")
    command = ('@echo off\ncall "' + str(VCVARS) + '" >nul\n'
               'if errorlevel 1 exit /b 1\n'
               'cl /nologo /std:c++20 /EHsc /utf-8 /W4 harness.cpp /Fe:harness.exe\n'
               'if errorlevel 1 exit /b 1\nharness.exe\n')
    (TEMP / "run.cmd").write_text(command, encoding="utf-8")
    subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(TEMP / "run.cmd")],
                   cwd=TEMP, check=True)


if __name__ == "__main__":
    main()
