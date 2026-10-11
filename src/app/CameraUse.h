// CameraUse: whether another app is using a camera right now, read from the
// records Windows keeps for its camera privacy indicator. Read-only registry
// access: no camera is opened, nothing is written and nothing leaves the computer.
#pragma once

#include <string>

struct CameraUse {
    bool inUse = false;
    std::string appName;  // UTF-8, e.g. "Zoom"; empty when not in use
};

// Skips this app's own use (the preview). Takes well under a millisecond.
CameraUse QueryCameraUse();
