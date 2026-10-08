#include "App.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_internal.h"
#include "Log.h"
#include "version.h"

using obsbot::AiMode;
using obsbot::Fov;

namespace {

// ---- Palette (dark) -------------------------------------------------------------
const ImVec4 kBg = ImVec4(0.059f, 0.067f, 0.082f, 1.0f);         // #0F1115
const ImVec4 kPanel = ImVec4(0.086f, 0.102f, 0.125f, 1.0f);      // #161A20
const ImVec4 kFrame = ImVec4(0.137f, 0.157f, 0.196f, 1.0f);      // #232832
const ImVec4 kFrameHover = ImVec4(0.169f, 0.196f, 0.243f, 1.0f); // #2B323E
const ImVec4 kFrameActive = ImVec4(0.204f, 0.239f, 0.294f, 1.0f); // #343D4B
const ImVec4 kBorder = ImVec4(0.180f, 0.204f, 0.243f, 1.0f);     // #2E343E
const ImVec4 kText = ImVec4(0.902f, 0.910f, 0.922f, 1.0f);       // #E6E8EB
const ImVec4 kTextDim = ImVec4(0.545f, 0.576f, 0.631f, 1.0f);    // #8B93A1
const ImVec4 kAccent = ImVec4(0.310f, 0.549f, 1.0f, 1.0f);       // #4F8CFF
const ImVec4 kAccentHover = ImVec4(0.416f, 0.620f, 1.0f, 1.0f);
const ImVec4 kAccentDim = ImVec4(0.310f, 0.549f, 1.0f, 0.35f);
const ImVec4 kGood = ImVec4(0.298f, 0.788f, 0.486f, 1.0f);
const ImVec4 kWarn = ImVec4(0.961f, 0.722f, 0.259f, 1.0f);
const ImVec4 kBad = ImVec4(0.937f, 0.353f, 0.353f, 1.0f);

const UvcInfo& U(const CameraState& s, UvcCtl c) { return s.uvc[static_cast<size_t>(c)]; }

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SectionTitle(const char* text) {
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// A button that looks "on" when selected.
bool ToggleButton(const char* label, bool selected, const ImVec2& size = ImVec2(0, 0)) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool pressed = ImGui::Button(label, size);
    if (selected) ImGui::PopStyleColor(4);
    return pressed;
}

// Small rounded badge: a coloured dot and label on a tinted background.
void StatusPill(const char* text, const ImVec4& colour) {
    const ImVec2 pad(ImGui::GetFontSize() * 0.6f, ImGui::GetStyle().FramePadding.y);
    const float dot = ImGui::GetFontSize() * 0.22f;
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 size(pad.x * 2 + dot * 2 + ImGui::GetStyle().ItemInnerSpacing.x + textSize.x,
                      textSize.y + pad.y * 2);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                      ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.15f)), size.y * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + pad.x + dot, p.y + size.y * 0.5f), dot, ImGui::GetColorU32(colour));
    dl->AddText(ImVec2(p.x + pad.x + dot * 2 + ImGui::GetStyle().ItemInnerSpacing.x, p.y + pad.y),
                ImGui::GetColorU32(colour), text);
    ImGui::Dummy(size);
}

// Square button with a drawn arrow. Returns true while held down.
bool ArrowPadButton(const char* id, ImGuiDir dir, float size) {
    ImGui::Button(id, ImVec2(size, size));
    const bool held = ImGui::IsItemActive();
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    float r = size * 0.18f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(held ? ImVec4(1, 1, 1, 1) : kText);
    switch (dir) {
    case ImGuiDir_Up: dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.6f), ImVec2(c.x - r, c.y + r * 0.6f), col); break;
    case ImGuiDir_Down: dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r), col); break;
    case ImGuiDir_Left: dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r * 0.6f, c.y + r), col); break;
    case ImGuiDir_Right: dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x - r * 0.6f, c.y - r), col); break;
    default: break;
    }
    return held;
}

const char* AntiFlickerName(long v) {
    switch (v) {
    case kPowerLineDisabled: return "Off";
    case kPowerLine50Hz: return "50 Hz";
    case kPowerLine60Hz: return "60 Hz";
    default: return "Auto";
    }
}

std::string WindowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (fn && fn(&v) == 0) {
            char buf[64];
            snprintf(buf, sizeof buf, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
            return buf;
        }
    }
    return "Windows (unknown version)";
}

std::string NarrowPath(const std::wstring& w) {
    std::string s;
    for (wchar_t ch : w) s += (ch < 128) ? static_cast<char>(ch) : '?';
    return s;
}

}  // namespace

// ---- Theme -----------------------------------------------------------------------------

void App::ApplyTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.GrabMinSize = 12.0f;
    style.ScrollbarSize = 12.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_PopupBg] = kPanel;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_FrameBg] = kFrame;
    c[ImGuiCol_FrameBgHovered] = kFrameHover;
    c[ImGuiCol_FrameBgActive] = kFrameActive;
    c[ImGuiCol_TitleBg] = kBg;
    c[ImGuiCol_TitleBgActive] = kBg;
    c[ImGuiCol_MenuBarBg] = kPanel;
    c[ImGuiCol_ScrollbarBg] = kPanel;
    c[ImGuiCol_ScrollbarGrab] = kFrameHover;
    c[ImGuiCol_ScrollbarGrabHovered] = kFrameActive;
    c[ImGuiCol_ScrollbarGrabActive] = kAccentDim;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_Button] = kFrame;
    c[ImGuiCol_ButtonHovered] = kFrameHover;
    c[ImGuiCol_ButtonActive] = kFrameActive;
    c[ImGuiCol_Header] = kFrame;
    c[ImGuiCol_HeaderHovered] = kFrameHover;
    c[ImGuiCol_HeaderActive] = kFrameActive;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_Tab] = kPanel;
    c[ImGuiCol_TabHovered] = kFrameHover;
    c[ImGuiCol_TabSelected] = kFrame;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = kPanel;
    c[ImGuiCol_TabDimmedSelected] = kFrame;
    c[ImGuiCol_TextSelectedBg] = kAccentDim;
    c[ImGuiCol_NavCursor] = kAccent;

    style.FontSizeBase = 17.0f;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

// ---- Lifecycle --------------------------------------------------------------------------

void App::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx) {
    hwnd_ = hwnd;
    device_ = device;
    ctx_ = ctx;
    worker_.SetVariantSetting(static_cast<VariantSetting>(settings_.variant));
    worker_.SetAntiFlickerOnConnect(settings_.antiFlicker);
    worker_.SelectDevice(settings_.devicePath);
    worker_.Start();
}

void App::Shutdown() {
    preview_.Stop();
    worker_.Stop();
    settings_.Save();
}

// ---- Frame -----------------------------------------------------------------------------

void App::ManagePreview(const CameraState& s) {
    const bool want = settings_.previewEnabled && s.connected;
    if (!want) {
        if (preview_.Running()) preview_.Stop();
        previewAttemptPath_.clear();
        return;
    }
    if (preview_.Running() && preview_.DevicePath() == s.device.path) return;
    if (!preview_.Running() && previewAttemptPath_ == s.device.path) return;  // failed: wait for Retry
    previewAttemptPath_ = s.device.path;
    preview_.Start(s.device.path);
}

void App::Frame() {
    const CameraState s = worker_.Snapshot();
    if (s.connected && s.device.path != settings_.devicePath) settings_.devicePath = s.device.path;
    ManagePreview(s);
    holding_ = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    DrawHeader(s);

    const float panelW = std::min(430.0f * ImGui::GetStyle().FontScaleDpi, ImGui::GetContentRegionAvail().x * 0.55f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float previewW = avail.x - panelW - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##preview", ImVec2(previewW, avail.y), ImGuiChildFlags_Borders);
    DrawPreview(s, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##panel", ImVec2(panelW, avail.y), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Control")) { DrawControlTab(s); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Image")) { DrawImageTab(s); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Camera")) { DrawCameraTab(s); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Log")) { DrawLogTab(s); ImGui::EndTabItem(); }
        if (settings_.showDeveloper && ImGui::BeginTabItem("Developer")) {
            DrawDeveloperTab(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();
}

void App::DrawHeader(const CameraState& s) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("osbot-oss");
    ImGui::SameLine();
    const bool asleep = s.status.valid && s.status.asleep;
    const ImVec4 colour = !s.connected ? kBad : asleep ? kWarn : kGood;
    StatusPill(!s.connected ? "Not connected" : asleep ? "Asleep" : "Connected", colour);
    if (s.connected) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", obsbot::ModelName(s.device.model));
    }

    if (s.connected) {
        const char* label = s.status.valid && s.status.asleep ? "Wake camera" : "Sleep camera";
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        if (s.available.size() > 1) w += 260.0f * ImGui::GetStyle().FontScaleDpi + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
        if (s.available.size() > 1) {
            ImGui::SetNextItemWidth(260.0f * ImGui::GetStyle().FontScaleDpi);
            if (ImGui::BeginCombo("##device", s.device.name.c_str())) {
                for (const auto& d : s.available) {
                    bool sel = d.path == s.device.path;
                    std::string label2 = d.name + "##" + NarrowPath(d.path);
                    if (ImGui::Selectable(label2.c_str(), sel)) {
                        settings_.devicePath = d.path;
                        worker_.SelectDevice(d.path);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!s.xu);
        if (ImGui::Button(label)) worker_.SetSleep(!(s.status.valid && s.status.asleep));
        ImGui::EndDisabled();
    }
    ImGui::Spacing();
}

void App::DrawPreview(const CameraState& s, float width, float height) {
    int texW = 0, texH = 0;
    ID3D11ShaderResourceView* srv = preview_.Running() ? preview_.Update(device_, ctx_, &texW, &texH) : nullptr;
    previewActive_ = preview_.Running();

    auto centredText = [&](const char* text, const ImVec4& colour) {
        const float wrap = width * 0.8f;
        ImVec2 sz = ImGui::CalcTextSize(text, nullptr, false, wrap);
        ImGui::SetCursorPos(ImVec2((width - sz.x) * 0.5f + ImGui::GetStyle().WindowPadding.x,
                                   (height - sz.y) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    if (srv && texW > 0 && texH > 0) {
        const float scale = std::min(width / texW, height / texH);
        const ImVec2 size(texW * scale, texH * scale);
        const ImVec2 start = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(start.x + (width - size.x) * 0.5f, start.y + (height - size.y) * 0.5f));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv))), size);
        ImGui::SetCursorPos(ImVec2(start.x + 4, start.y + 2));
        ImGui::TextDisabled("%s", preview_.Format().c_str());
        return;
    }

    if (!s.connected) {
        centredText("Waiting for an OBSBOT camera...", kTextDim);
    } else if (!settings_.previewEnabled) {
        centredText("Preview is off. Turn it on from the Camera tab.", kTextDim);
    } else if (std::string err = preview_.Error(); !err.empty()) {
        centredText(err.c_str(), kWarn);
        const char* retry = "Retry preview";
        const float bw = ImGui::CalcTextSize(retry).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX((width - bw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(retry)) previewAttemptPath_.clear();
    } else {
        centredText("Starting preview...", kTextDim);
    }
}

// ---- Control tab -------------------------------------------------------------------------

void App::DrawControlTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    DrawAiTracking(s);
    DrawGimbal(s);
    DrawPresets(s);
    ImGui::EndDisabled();
}

void App::DrawAiTracking(const CameraState& s) {
    SectionTitle("AI TRACKING");
    ImGui::BeginDisabled(!s.xu);
    const AiMode current = s.status.aiModeKnown ? s.status.aiMode : AiMode::Count;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float bw = (ImGui::GetContentRegionAvail().x - spacing * 2) / 3.0f;

    if (ToggleButton("Off", current == AiMode::Off, ImVec2(bw, 0))) worker_.SetAiMode(AiMode::Off);
    ImGui::SameLine(0, spacing * 2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Speed");
    ImGui::SameLine();
    if (ImGui::RadioButton("Standard", !s.status.sportTracking)) worker_.SetSportTracking(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Sport", s.status.sportTracking)) worker_.SetSportTracking(true);

    const AiMode modes[] = {AiMode::Normal, AiMode::UpperBody, AiMode::CloseUp,    AiMode::Headless, AiMode::LowerBody,
                            AiMode::Group,  AiMode::Hand,      AiMode::Whiteboard, AiMode::Desk};
    for (int i = 0; i < IM_ARRAYSIZE(modes); ++i) {
        const AiMode m = modes[i];
        if (i % 3) ImGui::SameLine();
        const char* label = m == AiMode::Normal ? "Normal" : obsbot::AiModeName(m);
        if (ToggleButton(label, m == current, ImVec2(bw, 0))) worker_.SetAiMode(m);
        if (m == AiMode::Desk) ImGui::SetItemTooltip("Desk mode may not work on every firmware version.");
    }
    if (s.status.valid && !s.status.aiModeKnown)
        ImGui::TextDisabled("Camera reports an unknown mode (%u, %u).", s.status.aiM, s.status.aiN);
    ImGui::EndDisabled();
    if (!s.xu && s.connected) ImGui::TextDisabled("AI controls need the OBSBOT extension unit (see Log).");
}

void App::DrawGimbal(const CameraState& s) {
    SectionTitle("GIMBAL AND ZOOM");
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float b = 46.0f * dpi;
    const float sp = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    const ImVec2 origin = ImGui::GetCursorPos();
    const float padW = b * 3 + sp * 2;

    // 3x3 pad: arrows around a Centre button.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
    ImGui::SetCursorPos(ImVec2(origin.x + b + sp, origin.y));
    const bool up = ArrowPadButton("##up", ImGuiDir_Up, b);
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + b + sp));
    const bool left = ArrowPadButton("##left", ImGuiDir_Left, b);
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.xu);
    if (ImGui::Button("##centre", ImVec2(b, b))) worker_.Recentre();
    {
        // Target symbol: a ring with a dot.
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddCircle(c, b * 0.2f, col, 0, 2.0f * dpi);
        dl->AddCircleFilled(c, b * 0.07f, col);
    }
    ImGui::SetItemTooltip("Centre the camera");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool right = ArrowPadButton("##right", ImGuiDir_Right, b);
    ImGui::SetCursorPos(ImVec2(origin.x + b + sp, origin.y + (b + sp) * 2));
    const bool down = ArrowPadButton("##down", ImGuiDir_Down, b);
    ImGui::PopStyleVar();

    // Options to the right of the pad.
    const float optX = origin.x + padW + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPos(ImVec2(optX, origin.y));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("Movement");
    int method = settings_.moveMethod;
    if (ImGui::RadioButton("Smooth (vendor)", &method, static_cast<int>(MoveMethod::GimbalSpeed)) ||
        ImGui::RadioButton("Steps (UVC)", &method, static_cast<int>(MoveMethod::UvcSteps)))
        settings_.moveMethod = method;
    ImGui::SetItemTooltip("If one method doesn't move your camera, try the other.");
    if (settings_.moveMethod == static_cast<int>(MoveMethod::GimbalSpeed))
        ImGui::SliderFloat("##speed", &settings_.moveSpeed, 5.0f, 90.0f, "Speed %.0f\xC2\xB0/s");
    ImGui::Checkbox("Invert left/right", &settings_.invertPan);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const float groupBottom = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
    ImGui::SetCursorPos(ImVec2(origin.x, std::max(origin.y + (b + sp) * 3, groupBottom + ImGui::GetStyle().ItemSpacing.y)));

    // Hold-to-move.
    const int dx = (right ? 1 : 0) - (left ? 1 : 0);
    const int dy = (up ? 1 : 0) - (down ? 1 : 0);
    if (dx || dy) {
        holding_ = true;
        const int sx = settings_.invertPan ? -dx : dx;
        const int sy = settings_.invertTilt ? -dy : dy;
        if (settings_.moveMethod == static_cast<int>(MoveMethod::GimbalSpeed)) {
            // Positive yaw turns the camera towards its own left, which is the
            // viewer's right in an unmirrored preview.
            worker_.HoldGimbalVelocity(sy * settings_.moveSpeed, sx * settings_.moveSpeed);
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastStep_ > std::chrono::milliseconds(150)) {
                lastStep_ = now;
                auto step = [&](UvcCtl c, int dir) {
                    const UvcInfo& u = U(s, c);
                    if (!dir || !u.supported) return;
                    // 64-bit maths: driver-reported ranges can be close to the limits of long.
                    const long long range = static_cast<long long>(u.max) - u.min;
                    const long long delta = std::max<long long>(u.step, range / 40);
                    const long long target = static_cast<long long>(u.value) + dir * delta;
                    worker_.SetUvc(c, static_cast<long>(std::clamp<long long>(target, u.min, u.max)), false);
                };
                step(UvcCtl::Pan, sx);
                step(UvcCtl::Tilt, sy);
            }
        }
    }

    // Absolute sliders (UVC). Large ranges are arc-seconds: show degrees.
    auto angleSlider = [&](UvcCtl c, const char* label) {
        const UvcInfo& u = U(s, c);
        if (!u.supported) {
            ImGui::TextDisabled("%s: not available", label);
            return;
        }
        const float perDeg = (u.max - u.min) > 3600 ? 3600.0f : 1.0f;
        float deg = u.value / perDeg;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Pan  ").x - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::SliderFloat(label, &deg, u.min / perDeg, u.max / perDeg, "%.0f\xC2\xB0"))
            worker_.SetUvc(c, std::clamp(static_cast<long>(std::lround(deg * perDeg)), u.min, u.max), false);
    };
    angleSlider(UvcCtl::Pan, "Pan");
    angleSlider(UvcCtl::Tilt, "Tilt");
    UvcSlider(s, UvcCtl::Zoom, "Zoom");
}

void App::DrawPresets(const CameraState& s) {
    SectionTitle("PRESETS");
    const bool usable = U(s, UvcCtl::Pan).supported && U(s, UvcCtl::Tilt).supported;
    ImGui::BeginDisabled(!usable);
    const float bw = ImGui::CalcTextSize("Update").x + ImGui::GetStyle().FramePadding.x * 2;
    for (size_t i = 0; i < settings_.presets.size(); ++i) {
        Preset& p = settings_.presets[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Preset %zu", i + 1);
        ImGui::SameLine(ImGui::CalcTextSize("Preset 3").x + ImGui::GetStyle().ItemSpacing.x * 3);
        ImGui::BeginDisabled(!p.saved);
        if (ImGui::Button("Go", ImVec2(bw, 0))) worker_.GotoPosition(p.pan, p.tilt, p.zoom);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(p.saved ? "Update" : "Save", ImVec2(bw, 0))) {
            p.saved = true;
            p.pan = U(s, UvcCtl::Pan).value;
            p.tilt = U(s, UvcCtl::Tilt).value;
            p.zoom = U(s, UvcCtl::Zoom).value;
            settings_.Save();
            LOG_INFO("Saved preset %zu (pan %ld, tilt %ld, zoom %ld).", i + 1, p.pan, p.tilt, p.zoom);
        }
        if (p.saved) {
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(bw, 0))) {
                p = Preset{};
                settings_.Save();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (s.connected && !usable) ImGui::TextDisabled("Presets need UVC pan/tilt support, which this camera doesn't report.");
}

// ---- Image tab -------------------------------------------------------------------------------

void App::UvcSlider(const CameraState& s, UvcCtl c, const char* label) {
    const UvcInfo& u = U(s, c);
    ImGui::PushID(static_cast<int>(c));
    if (!u.supported) {
        ImGui::TextDisabled("%s: not available", label);
        ImGui::PopID();
        return;
    }
    const float labelW = ImGui::GetFontSize() * 7.5f;
    const float autoW = u.canAuto ? ImGui::CalcTextSize("Auto").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2 : 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelW);
    ImGui::BeginDisabled(u.isAuto);
    int v = static_cast<int>(u.value);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - autoW);
    if (ImGui::SliderInt("##v", &v, static_cast<int>(u.min), static_cast<int>(u.max)))
        worker_.SetUvc(c, v, false);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) worker_.SetUvc(c, u.def, false);
    ImGui::SetItemTooltip("Double-click to reset (default %ld).", u.def);
    ImGui::EndDisabled();
    if (u.canAuto) {
        ImGui::SameLine();
        bool a = u.isAuto;
        if (ImGui::Checkbox("Auto", &a)) worker_.SetUvc(c, u.value, a);
    }
    ImGui::PopID();
}

void App::DrawImageTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    SectionTitle("PICTURE");
    UvcSlider(s, UvcCtl::Brightness, "Brightness");
    UvcSlider(s, UvcCtl::Contrast, "Contrast");
    UvcSlider(s, UvcCtl::Saturation, "Saturation");
    UvcSlider(s, UvcCtl::Sharpness, "Sharpness");
    UvcSlider(s, UvcCtl::Hue, "Hue");
    UvcSlider(s, UvcCtl::Gamma, "Gamma");
    SectionTitle("COLOUR AND LIGHT");
    UvcSlider(s, UvcCtl::WhiteBalance, "White balance");
    UvcSlider(s, UvcCtl::Exposure, "Exposure");
    UvcSlider(s, UvcCtl::Gain, "Gain");
    UvcSlider(s, UvcCtl::BacklightComp, "Backlight");
    SectionTitle("FOCUS");
    UvcSlider(s, UvcCtl::Focus, "Focus");
    ImGui::Spacing();
    if (ImGui::Button("Reset image to camera defaults")) worker_.ResetImageDefaults();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) worker_.RefreshAll();
    ImGui::EndDisabled();
}

// ---- Camera tab ------------------------------------------------------------------------------

void App::DrawCameraTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    SectionTitle("PICTURE MODE");
    ImGui::BeginDisabled(!s.xu);
    bool hdr = s.status.hdr;
    if (ImGui::Checkbox("HDR", &hdr)) worker_.SetHdr(hdr);
    HelpMarker("High dynamic range: helps when there's a bright window behind you. May lower the frame rate.");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Field of view");
    Fov currentFov = Fov::Count;
    if (s.status.valid) obsbot::FovFromStatus(s.status.fovRaw, s.variant, &currentFov);
    for (int i = 0; i < static_cast<int>(Fov::Count); ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(obsbot::FovName(static_cast<Fov>(i)), currentFov == static_cast<Fov>(i)))
            worker_.SetFov(static_cast<Fov>(i));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    SectionTitle("ANTI-FLICKER");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mains power");
    HelpMarker("Matches the camera to your mains power to stop lights flickering on video. "
               "New Zealand and Australia use 50 Hz. Applied each time the camera connects.");
    ImGui::BeginDisabled(!s.connected || !U(s, UvcCtl::PowerLine).supported);
    for (int v : {kPowerLineDisabled, kPowerLine50Hz, kPowerLine60Hz}) {
        ImGui::SameLine();
        if (ImGui::RadioButton(AntiFlickerName(v), settings_.antiFlicker == v)) {
            settings_.antiFlicker = v;
            worker_.SetAntiFlickerOnConnect(v);
            worker_.SetUvc(UvcCtl::PowerLine, v, false);
            settings_.Save();
        }
    }
    ImGui::EndDisabled();
    if (s.connected && U(s, UvcCtl::PowerLine).supported)
        ImGui::TextDisabled("Camera currently reports: %s", AntiFlickerName(U(s, UvcCtl::PowerLine).value));

    SectionTitle("PREVIEW");
    if (ImGui::Checkbox("Show live preview", &settings_.previewEnabled)) settings_.Save();
    HelpMarker("Turn this off if you want another app (Teams, Zoom, OBS) to use the camera while this window "
               "is open. Controls keep working without the preview.");

    SectionTitle("ADVANCED");
    if (ImGui::Checkbox("Show developer tools", &settings_.showDeveloper)) settings_.Save();
    HelpMarker("Adds a Developer tab for testing raw camera commands. You won't need it for everyday use.");

    SectionTitle("ABOUT");
    ImGui::Text("osbot-oss-windows %s", OSBOT_VERSION_STRING);
    ImGui::TextWrapped("Free and open-source software under the MIT Licence. Unofficial: not made by, "
                       "affiliated with or endorsed by OBSBOT.");
    ImGui::TextWrapped("Private by design: works entirely offline, with no accounts, telemetry or internet access.");
    if (ImGui::Button("Open settings folder"))
        ShellExecuteW(nullptr, L"open", Settings::Directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- Log tab --------------------------------------------------------------------------------

std::string App::Diagnostics(const CameraState& s) {
    std::string d;
    char line[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(line, sizeof line, "osbot-oss-windows %s diagnostics, %u/%02u/%04u %02u:%02u\r\n", OSBOT_VERSION_STRING,
             st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    d += line;
    d += WindowsVersion() + "\r\n";
    if (!s.connected) {
        d += "Camera: not connected\r\n";
    } else {
        snprintf(line, sizeof line, "Camera: %s (%s), VID %04X PID %04X\r\nPath: %s\r\n", s.device.name.c_str(),
                 obsbot::ModelName(s.device.model), s.device.vid, s.device.pid, NarrowPath(s.device.path).c_str());
        d += line;
        snprintf(line, sizeof line, "Extension unit: %s (node %lu), protocol variant %s\r\n", s.xu ? "found" : "NOT found",
                 s.xuNode, s.variant == obsbot::Variant::Lite ? "Lite" : "Tiny 2");
        d += line;
        static const char* names[] = {"Pan", "Tilt", "Zoom", "Exposure", "Focus", "Brightness", "Contrast", "Hue",
                                      "Saturation", "Sharpness", "Gamma", "WhiteBalance", "BacklightComp", "Gain",
                                      "PowerLine"};
        for (size_t i = 0; i < s.uvc.size(); ++i) {
            const UvcInfo& u = s.uvc[i];
            if (!u.supported) {
                snprintf(line, sizeof line, "  %-13s unsupported\r\n", names[i]);
            } else {
                snprintf(line, sizeof line, "  %-13s %ld..%ld step %ld def %ld value %ld%s%s\r\n", names[i], u.min,
                         u.max, u.step, u.def, u.value, u.canAuto ? " canAuto" : "", u.isAuto ? " AUTO" : "");
            }
            d += line;
        }
        d += "Status block: " + obsbot::ToHex(s.rawStatus.data(), s.rawStatus.size()) + "\r\n";
    }
    d += "Preview: " + (preview_.Running() ? preview_.Format() : std::string("not running")) + "\r\n";
    if (!preview_.Error().empty()) d += "Preview error: " + preview_.Error() + "\r\n";
    d += "\r\n--- Log ---\r\n" + logx::AllAsText();
    return d;
}

void App::DrawLogTab(const CameraState& s) {
    if (ImGui::Button("Copy diagnostics")) {
        ImGui::SetClipboardText(Diagnostics(s).c_str());
        LOG_INFO("Diagnostics copied to the clipboard.");
    }
    ImGui::SetItemTooltip("Copies camera details and this log so you can paste them into a bug report.");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) logx::Clear();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const auto entries = logx::Snapshot();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entries.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& e = entries[static_cast<size_t>(i)];
            ImGui::TextDisabled("%s", e.time.c_str());
            ImGui::SameLine();
            const ImVec4 col = e.level == logx::Level::Error ? kBad : e.level == logx::Level::Warn ? kWarn : kText;
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// ---- Developer tab -------------------------------------------------------------------------------

void App::DrawDeveloperTab(const CameraState& s) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextUnformatted("For testing protocol commands. Unknown commands can confuse the camera; "
                           "unplug it and plug it back in to recover.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    SectionTitle("PROTOCOL VARIANT");
    const char* variants[] = {"Automatic (by model)", "Tiny 2 codes", "Tiny 2 Lite codes"};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##variant", &settings_.variant, variants, IM_ARRAYSIZE(variants))) {
        worker_.SetVariantSetting(static_cast<VariantSetting>(settings_.variant));
        settings_.Save();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("In use: %s codes. Affects field of view and sleep commands.",
                        s.variant == obsbot::Variant::Lite ? "Tiny 2 Lite" : "Tiny 2");
    ImGui::PopTextWrapPos();
    ImGui::Checkbox("Invert up/down", &settings_.invertTilt);

    ImGui::BeginDisabled(!s.connected || !s.xu);
    SectionTitle("STATUS BLOCK (SELECTOR 6)");
    if (s.status.valid) {
        ImGui::Text("Asleep %s, device status %u, HDR %s, FOV byte %u, zoom %u%%", s.status.asleep ? "yes" : "no",
                    s.status.deviceStatus, s.status.hdr ? "on" : "off", s.status.fovRaw, s.status.zoomPercent);
        ImGui::Text("AI bytes (%u, %u) = %s, tracking %s", s.status.aiM, s.status.aiN,
                    s.status.aiModeKnown ? obsbot::AiModeName(s.status.aiMode) : "unknown",
                    s.status.sportTracking ? "sport" : "standard");
    } else {
        ImGui::TextDisabled("No status yet.");
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", obsbot::ToHex(s.rawStatus.data(), s.rawStatus.size()).c_str());
    ImGui::PopTextWrapPos();

    SectionTitle("RAW XU TRANSFER");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::InputInt("Selector", &devSelector_);
    devSelector_ = std::clamp(devSelector_, 1, 31);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hex", "Bytes in hex, e.g. 16 02 02 01", devHex_, sizeof devHex_);
    if (ImGui::Button("SET")) {
        std::vector<uint8_t> bytes;
        if (obsbot::ParseHex(devHex_, &bytes))
            worker_.DevSetRaw(static_cast<uint32_t>(devSelector_), bytes);
        else
            LOG_WARN("[dev] Couldn't read those hex bytes.");
    }
    ImGui::SameLine();
    if (ImGui::Button("GET")) worker_.DevGetRaw(static_cast<uint32_t>(devSelector_));

    SectionTitle("FRAMED COMMAND (SELECTOR 2)");
    const char* receivers[] = {"Camera (0x02)", "Gimbal (0x03)", "AI (0x04)"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    ImGui::Combo("Receiver", &devReceiver_, receivers, IM_ARRAYSIZE(receivers));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    ImGui::InputTextWithHint("Command", "e.g. A0C2", devCmd_, sizeof devCmd_);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##payload", "Payload hex (empty = GET)", devPayload_, sizeof devPayload_);
    if (ImGui::Button("Send framed")) {
        std::vector<uint8_t> payload;
        char* end = nullptr;
        const unsigned long command = strtoul(devCmd_, &end, 16);
        while (*end == ' ') ++end;
        const bool commandOk = end != devCmd_ && *end == '\0' && devCmd_[0] != '-' && command <= 0xFFFF;
        if (!obsbot::ParseHex(devPayload_, &payload) || !commandOk) {
            LOG_WARN("[dev] Check the command and payload hex.");
        } else {
            const obsbot::Receiver r = devReceiver_ == 0   ? obsbot::Receiver::Camera
                                       : devReceiver_ == 1 ? obsbot::Receiver::Gimbal
                                                           : obsbot::Receiver::Ai;
            worker_.DevFramed(r, static_cast<uint16_t>(command), payload);
        }
    }
    ImGui::EndDisabled();

    SectionTitle("LAST RESULT");
    std::string out = s.devOutput;
    ImGui::InputTextMultiline("##out", out.data(), out.size() + 1, ImVec2(-1, ImGui::GetTextLineHeight() * 6),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
}
