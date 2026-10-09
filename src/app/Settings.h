// Settings: a small key=value file in %APPDATA%\osbot-oss-windows\settings.ini.
// Nothing is stored in the registry and nothing leaves the computer.
#pragma once

#include <array>
#include <string>

struct Preset {
    bool saved = false;
    long pan = 0, tilt = 0, zoom = 0;
};

enum class MoveMethod { GimbalSpeed = 0, UvcSteps = 1 };

struct Settings {
    bool previewEnabled = true;
    int antiFlicker = 1;    // -1 leave alone, 0 off, 1 = 50 Hz (NZ mains), 2 = 60 Hz
    int variant = 0;        // VariantSetting: 0 auto, 1 Tiny 2, 2 Lite
    int moveMethod = 0;     // MoveMethod
    float moveSpeed = 30.0f;  // degrees per second for hold-to-move
    bool invertPan = false;
    bool invertTilt = false;
    bool showDeveloper = false;
    std::array<Preset, 3> presets{};
    std::wstring devicePath;  // last camera used
    int windowW = 1280, windowH = 800;

    void Load();
    void Save() const;

    static std::wstring Directory();  // created on demand; empty disables persistence
};
