#include "App.h"
#include "../Safety.h"

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

// ---- Palette (dark, neon) ----------------------------------------------------------
// Near-black with a faint teal tint and one neon accent for everything that is on,
// selected or active. Green, amber and red are kept for status only (connection
// pill, log levels, warnings). Text on a solid accent fill uses kAccentInk.
ImVec4 Hex(unsigned rgb, float alpha = 1.0f) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}
const ImVec4 kBg = Hex(0x06090D);
const ImVec4 kPanel = Hex(0x0B1117);
const ImVec4 kFrame = Hex(0x111B23);
const ImVec4 kFrameHover = Hex(0x16242E);
const ImVec4 kFrameActive = Hex(0x1D303C);
const ImVec4 kBorder = Hex(0x1B2C36);
const ImVec4 kText = Hex(0xDDF6F8);
const ImVec4 kTextDim = Hex(0x6E8C96);
const ImVec4 kAccent = Hex(0x67F7FF);
const ImVec4 kAccentHover = Hex(0xA4FBFF);
const ImVec4 kAccentDim = Hex(0x67F7FF, 0.30f);
const ImVec4 kAccentTint = Hex(0x67F7FF, 0.12f);  // background of a selected button
const ImVec4 kAccentInk = Hex(0x041418);
const ImVec4 kGood = Hex(0x4DFF9A);
const ImVec4 kWarn = Hex(0xFFB547);
const ImVec4 kBad = Hex(0xFF4D6D);

ImFont* g_headingFont = nullptr;

const UvcInfo& U(const CameraState& s, UvcCtl c) { return s.uvc[static_cast<size_t>(c)]; }

// ---- Layout grid ------------------------------------------------------------------
// Every control row uses the same columns so things line up across sections:
//   label | control (stretches) | value (right-aligned) | trailing (Auto switch)
float LabelWidth() { return ImGui::GetFontSize() * 8.0f; }
float CompactLabelWidth() { return ImGui::GetFontSize() * 3.0f; }
float ValueWidth() { return ImGui::GetFontSize() * 3.2f; }
// Room after a number for its unit, so digits line up whether or not a unit follows.
float UnitSlot() { return ImGui::CalcTextSize("\xC2\xB0/s").x; }
float Gap() { return ImGui::GetStyle().ItemSpacing.x; }
float SwitchHeight() { return std::round(ImGui::GetFontSize() * 1.05f); }
float SwitchWidth() { return std::round(SwitchHeight() * 1.8f); }
float TrailWidth() { return SwitchWidth() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Auto").x; }

// Width of each of n buttons that share the rest of the row equally.
float EqualWidth(int n) {
    return std::floor((ImGui::GetContentRegionAvail().x - Gap() * static_cast<float>(n - 1)) / static_cast<float>(n));
}

void Tooltip(const char* text) {
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void HelpMarker(const char* text) {
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextDisabled("(?)");
    Tooltip(text);
}

// A heading in the bold face and accent colour over a thin line that fades out.
void SectionTitle(const char* text) {
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.25f));
    if (g_headingFont) ImGui::PushFont(g_headingFont, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (g_headingFont) ImGui::PopFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = std::max(1.0f, std::round(ImGui::GetStyle().FontScaleDpi));
    const ImU32 on = ImGui::GetColorU32(kAccentDim), off = ImGui::GetColorU32(Hex(0x67F7FF, 0.0f));
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(p.x, p.y - ImGui::GetStyle().ItemSpacing.y * 0.5f),
                                                        ImVec2(p.x + w, p.y - ImGui::GetStyle().ItemSpacing.y * 0.5f + h),
                                                        on, off, off, on);
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.25f));
}

// Starts a row: the label in a column labelWidth wide (with an optional help
// marker), leaving the cursor where the control goes. A label too long for its
// column pushes the control along rather than overlapping it.
void RowLabel(const char* label, float labelWidth, const char* help = nullptr) {
    const float x0 = ImGui::GetCursorScreenPos().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) HelpMarker(help);
    ImGui::SameLine(0, 0);
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(std::max(cur.x + Gap(), x0 + labelWidth), cur.y));
}
void RowLabel(const char* label, const char* help = nullptr) { RowLabel(label, LabelWidth(), help); }

// A value shown beside its slider: the number right-aligned in the value column,
// then its unit (if any) in the unit slot TrackWidth left, so every number ends at
// the same x.
void ValueText(const char* number, const char* unit = "") {
    ImGui::SameLine(0, Gap());
    const float x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + std::max(0.0f, ValueWidth() - ImGui::CalcTextSize(number).x));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(number);
    if (*unit) {
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(unit);
    }
}

// Width left for a slider track once the value column (with its unit slot) and the
// optional trailing column are reserved.
float TrackWidth(bool trailColumn, float unitSlot = 0.0f) {
    return std::max(ImGui::GetFontSize() * 3.0f, ImGui::GetContentRegionAvail().x - Gap() - ValueWidth() - unitSlot -
                                                     (trailColumn ? Gap() + TrailWidth() : 0.0f));
}

// A soft neon halo around a shape: a few fading accent outlines.
void Glow(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding, float strength = 1.0f) {
    const float step = std::max(1.0f, std::round(ImGui::GetFontSize() * 0.08f));
    for (int i = 1; i <= 3; ++i) {
        const float e = step * static_cast<float>(i);
        dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e),
                    ImGui::GetColorU32(Hex(0x67F7FF, 0.20f * strength / static_cast<float>(i))), rounding + e, step);
    }
}

// A slim slider: a thin rounded track, accent fill and a round grab. The value is
// drawn by the caller beside it (ValueText), never inside the track.
bool SliderTrack(const char* id, ImGuiDataType type, void* value, const void* min, const void* max,
                 const char* format, float width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID gid = window->GetID(id);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + ImGui::GetFrameHeight()));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, gid)) return false;

    const bool hovered = ImGui::ItemHoverable(bb, gid, g.LastItemData.ItemFlags);
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None, gid);
    if (clicked || g.NavActivateId == gid) {
        if (clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft, gid);
        ImGui::SetActiveID(gid, window);
        ImGui::SetFocusID(gid, window);
        ImGui::FocusWindow(window);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
    }

    ImRect grab;
    const bool changed = ImGui::SliderBehavior(bb, gid, type, value, min, max, format,
                                               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput, &grab);
    if (changed) ImGui::MarkItemEdited(gid);

    const bool active = g.ActiveId == gid;
    const float cy = std::round((bb.Min.y + bb.Max.y) * 0.5f);
    const float track = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.24f));
    const float radius = style.GrabMinSize * 0.62f * (hovered || active ? 1.15f : 1.0f);
    const float x0 = bb.Min.x + 2.0f + style.GrabMinSize * 0.5f;
    const float x1 = bb.Max.x - 2.0f - style.GrabMinSize * 0.5f;
    const float gx = grab.Max.x > grab.Min.x ? std::clamp((grab.Min.x + grab.Max.x) * 0.5f, x0, x1) : x0;

    ImDrawList* dl = window->DrawList;
    ImGui::RenderNavCursor(bb, gid);
    dl->AddRectFilled(ImVec2(x0, cy - track * 0.5f), ImVec2(x1, cy + track * 0.5f),
                      ImGui::GetColorU32(hovered || active ? kFrameActive : kFrameHover), track * 0.5f);
    dl->AddRectFilled(ImVec2(x0, cy - track * 0.5f), ImVec2(gx, cy + track * 0.5f),
                      ImGui::GetColorU32(active ? kAccentHover : kAccent), track * 0.5f);
    if (hovered || active) Glow(dl, ImVec2(gx - radius, cy - radius), ImVec2(gx + radius, cy + radius), radius);
    dl->AddCircleFilled(ImVec2(gx, cy), radius, ImGui::GetColorU32(kBg));
    dl->AddCircle(ImVec2(gx, cy), radius, ImGui::GetColorU32(active ? kAccentHover : kAccent), 0,
                  std::max(2.0f, std::round(ImGui::GetFontSize() * 0.12f)));
    return changed;
}

// An on/off switch with its label to the right. Returns true when flipped.
bool ToggleSwitch(const char* label, bool* v) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float textW = label == end ? 0.0f : ImGui::CalcTextSize(label, end).x;
    const float sh = SwitchHeight(), sw = SwitchWidth();
    const float h = ImGui::GetFrameHeight();
    const float w = sw + (textW > 0 ? ImGui::GetStyle().ItemInnerSpacing.x + textW : 0.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    if (pressed) *v = !*v;
    const bool hovered = ImGui::IsItemHovered();
    ImGui::RenderNavCursor(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), ImGui::GetItemID());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = std::round(p.y + (h - sh) * 0.5f);
    const ImVec2 a(p.x, top), b(p.x + sw, top + sh);
    const float rounding = std::round(sh * 0.3f);
    if (*v) {
        Glow(dl, a, b, rounding);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(hovered ? kAccentHover : kAccent), rounding);
    } else {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(hovered ? kFrameActive : kFrameHover), rounding);
        dl->AddRect(a, b, ImGui::GetColorU32(hovered ? kAccentDim : kBorder), rounding);
    }
    // A squared-off knob.
    const float inset = std::max(2.0f, std::round(sh * 0.16f));
    const float k = sh - inset * 2;
    const float kx = *v ? b.x - inset - k : a.x + inset;
    dl->AddRectFilled(ImVec2(kx, top + inset), ImVec2(kx + k, top + inset + k),
                      ImGui::GetColorU32(*v ? kAccentInk : kTextDim), std::max(1.0f, rounding - inset));
    if (textW > 0)
        dl->AddText(ImVec2(b.x + ImGui::GetStyle().ItemInnerSpacing.x, p.y + ImGui::GetStyle().FramePadding.y),
                    ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return pressed;
}

// Joined, equal-width choices with the selected one in the accent colour.
// Returns the index that was clicked, or -1.
int Segmented(const char* id, const char* const* labels, int count, int selected,
              const char* const* tooltips = nullptr) {
    ImGui::PushID(id);
    const float width = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), ImGui::GetColorU32(kFrame), rounding);
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), ImGui::GetColorU32(kBorder), rounding);

    int clicked = -1;
    const float inset = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.15f));
    for (int i = 0; i < count; ++i) {
        const float xa = std::round(p.x + width * static_cast<float>(i) / static_cast<float>(count));
        const float xb = std::round(p.x + width * static_cast<float>(i + 1) / static_cast<float>(count));
        ImGui::SetCursorScreenPos(ImVec2(xa, p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(xb - xa, h))) clicked = i;
        ImGui::PopID();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::RenderNavCursor(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), ImGui::GetItemID());
        if (tooltips && tooltips[i]) Tooltip(tooltips[i]);
        const ImVec2 a(xa + inset, p.y + inset), b(xb - inset, p.y + h - inset);
        const float r = std::max(0.0f, rounding - inset * 0.5f);
        if (i == selected) {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(kAccentTint), r);
            dl->AddRect(a, b, ImGui::GetColorU32(hovered ? kAccentHover : kAccent), r);
        } else if (hovered) {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(kFrameHover), r);
        }
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->PushClipRect(ImVec2(xa, p.y), ImVec2(xb, p.y + h), true);
        dl->AddText(ImVec2(std::round((xa + xb - ts.x) * 0.5f), std::round(p.y + (h - ts.y) * 0.5f)),
                    ImGui::GetColorU32(i == selected ? kAccent : kText), labels[i]);
        dl->PopClipRect();
    }
    // Leave the cursor on the next line, as a normal full-width item would.
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::PopID();
    return clicked;
}

// A button that lights up (neon outline, accent text and a soft glow) when selected.
bool ToggleButton(const char* label, bool selected, const ImVec2& size = ImVec2(0, 0)) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccentTint);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0x67F7FF, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    }
    bool pressed = ImGui::Button(label, size);
    if (selected) {
        ImGui::PopStyleColor(5);
        Glow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
             ImGui::GetStyle().FrameRounding, 0.8f);
    }
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
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                      ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.10f)), rounding);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.45f)),
                rounding);
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
    ImU32 col = ImGui::GetColorU32(held ? kAccentHover : kText);
    switch (dir) {
    case ImGuiDir_Up: dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.6f), ImVec2(c.x - r, c.y + r * 0.6f), col); break;
    case ImGuiDir_Down: dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r), col); break;
    case ImGuiDir_Left: dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r * 0.6f, c.y + r), col); break;
    case ImGuiDir_Right: dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x - r * 0.6f, c.y - r), col); break;
    default: break;
    }
    return held;
}

// HUD-style corner brackets around a rectangle.
void CornerBrackets(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec4& colour) {
    const float len = std::min({ImGui::GetFontSize() * 1.2f, (b.x - a.x) * 0.25f, (b.y - a.y) * 0.25f});
    const float t = std::max(1.0f, std::round(ImGui::GetStyle().FontScaleDpi * 1.5f));
    const ImU32 c = ImGui::GetColorU32(colour);
    const ImVec2 corners[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
    for (const ImVec2& p : corners) {
        const float sx = p.x == a.x ? 1.0f : -1.0f, sy = p.y == a.y ? 1.0f : -1.0f;
        dl->AddLine(p, ImVec2(p.x + sx * len, p.y), c, t);
        dl->AddLine(p, ImVec2(p.x, p.y + sy * len), c, t);
    }
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

    // Small radii for a crisper, machined look.
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabBarOverlineSize = 2.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.GrabMinSize = 12.0f;
    style.ScrollbarSize = 12.0f;
    style.SeparatorTextBorderSize = 1.0f;

    // Set every colour from the palette, so none of Dear ImGui's stock blue shows.
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_PopupBg] = kPanel;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kFrame;
    c[ImGuiCol_FrameBgHovered] = kFrameHover;
    c[ImGuiCol_FrameBgActive] = kFrameActive;
    c[ImGuiCol_TitleBg] = kBg;
    c[ImGuiCol_TitleBgActive] = kBg;
    c[ImGuiCol_TitleBgCollapsed] = kBg;
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
    c[ImGuiCol_SeparatorHovered] = kFrameActive;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = kFrame;
    c[ImGuiCol_ResizeGripHovered] = kFrameActive;
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_InputTextCursor] = kText;
    c[ImGuiCol_Tab] = kPanel;
    c[ImGuiCol_TabHovered] = kFrameHover;
    c[ImGuiCol_TabSelected] = kFrame;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = kPanel;
    c[ImGuiCol_TabDimmedSelected] = kFrame;
    c[ImGuiCol_TabDimmedSelectedOverline] = kBorder;
    c[ImGuiCol_PlotLines] = kTextDim;
    c[ImGuiCol_PlotLinesHovered] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccentHover;
    c[ImGuiCol_TableHeaderBg] = kFrame;
    c[ImGuiCol_TableBorderStrong] = kBorder;
    c[ImGuiCol_TableBorderLight] = kBorder;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextLink] = kAccent;
    c[ImGuiCol_TextSelectedBg] = kAccentDim;
    c[ImGuiCol_TreeLines] = kBorder;
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_DragDropTargetBg] = kAccentDim;
    c[ImGuiCol_UnsavedMarker] = kText;
    c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = kAccent;
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

    // Whole pixels at this DPI, so hinted glyphs land exactly on the pixel grid.
    style.FontSizeBase = std::round(18.0f * dpiScale) / dpiScale;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

void App::SetHeadingFont(ImFont* font) { g_headingFont = font; }

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
    // A different camera starts again at its default format.
    if (s.device.path != previewFormatDevice_) {
        previewFormatDevice_ = s.device.path;
        previewFormatKey_.clear();
    }
    const bool sameRequest = preview_.DevicePath() == s.device.path && preview_.RequestedFormat() == previewFormatKey_;
    if (preview_.Running() && sameRequest) return;
    if (!preview_.Running() && sameRequest && previewAttemptPath_ == s.device.path) return;  // failed: wait for Retry
    previewAttemptPath_ = s.device.path;
    preview_.Start(s.device.path, previewFormatKey_);
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

    const float panelW = std::min(460.0f * ImGui::GetStyle().FontScaleDpi, ImGui::GetContentRegionAvail().x * 0.55f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float previewW = avail.x - panelW - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##preview", ImVec2(previewW, avail.y), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawPreviewToolbar(s);
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
    if (g_headingFont) ImGui::PushFont(g_headingFont, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("OSBOT-OSS");
    ImGui::PopStyleColor();
    if (g_headingFont) ImGui::PopFont();
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
        const float comboW = 260.0f * ImGui::GetStyle().FontScaleDpi;
        const float buttonW = std::max(ImGui::CalcTextSize("Sleep camera").x, ImGui::CalcTextSize("Wake camera").x) +
                              ImGui::GetStyle().FramePadding.x * 2;
        float w = buttonW;
        if (s.available.size() > 1) w += comboW + Gap();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
        if (s.available.size() > 1) {
            ImGui::SetNextItemWidth(comboW);
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
        if (ImGui::Button(label, ImVec2(buttonW, 0))) worker_.SetSleep(!(s.status.valid && s.status.asleep));
        ImGui::EndDisabled();
    }
    ImGui::Spacing();
}

// The strip above the video: resolution, pixel format and frame rate pickers on
// the left, the preview switch on the right.
void App::DrawPreviewToolbar(const CameraState& s) {
    namespace pf = preview_format;
    const std::vector<PreviewFormat> formats = preview_.Formats();
    const std::string defaultKey = preview_.DefaultFormatKey();
    const PreviewFormat* def = pf::Find(formats, defaultKey);
    // What's showing now: the running format, else the one asked for, else the default.
    const PreviewFormat* cur = pf::Find(formats, preview_.CurrentFormatKey());
    if (!cur) cur = pf::Find(formats, previewFormatKey_.empty() ? defaultKey : previewFormatKey_);
    if (!cur) cur = def;

    if (s.connected && settings_.previewEnabled && cur) {
        const ImGuiComboFlags flags = ImGuiComboFlags_WidthFitPreview | ImGuiComboFlags_HeightLarge;
        auto choose = [&](uint32_t w, uint32_t h, const std::string& fourcc, int fps) {
            const std::string key = pf::Choose(formats, w, h, fourcc, fps, def ? def->fourcc : std::string());
            if (key.empty()) return;
            previewFormatKey_ = key == defaultKey ? std::string() : key;
            previewAttemptPath_.clear();
        };
        auto item = [](std::string text, bool isDefault) {
            if (isDefault) text += "  (default)";
            return text;
        };
        const bool defSize = def && def->width == cur->width && def->height == cur->height;

        if (ImGui::BeginCombo("##size", PreviewFormat::SizeLabel(cur->width, cur->height).c_str(), flags)) {
            for (const auto& [w, h] : pf::Resolutions(formats)) {
                const bool isDefault = def && def->width == w && def->height == h;
                const std::string text = item(PreviewFormat::SizeLabel(w, h), isDefault);
                if (ImGui::Selectable(text.c_str(), w == cur->width && h == cur->height))
                    choose(w, h, cur->fourcc, cur->RoundedFps());
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Preview resolution. Larger sizes use more processor time.");

        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::BeginCombo("##pixels", cur->fourcc.c_str(), flags)) {
            for (const std::string& fourcc : pf::PixelFormats(formats, cur->width, cur->height)) {
                const std::string text = item(fourcc, defSize && def->fourcc == fourcc);
                if (ImGui::Selectable(text.c_str(), fourcc == cur->fourcc))
                    choose(cur->width, cur->height, fourcc, cur->RoundedFps());
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("How the camera sends each frame. NV12 and YUY2 are uncompressed; "
                              "MJPG is compressed and allows higher frame rates at large sizes.");

        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::BeginCombo("##rate", PreviewFormat::FpsLabel(cur->RoundedFps()).c_str(), flags)) {
            for (const int fps : pf::FrameRates(formats, cur->width, cur->height, cur->fourcc)) {
                const bool isDefault = defSize && def->fourcc == cur->fourcc && def->RoundedFps() == fps;
                const std::string text = item(PreviewFormat::FpsLabel(fps), isDefault);
                if (ImGui::Selectable(text.c_str(), fps == cur->RoundedFps()))
                    choose(cur->width, cur->height, cur->fourcc, fps);
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Preview frame rate.");
        ImGui::SameLine();
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Preview");
        ImGui::SameLine();
    }

    const char* label = "Live preview";
    const float w = SwitchWidth() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
    if (ToggleSwitch(label, &settings_.previewEnabled)) settings_.Save();
    Tooltip("Turn this off if you want another app (Teams, Zoom, OBS) to use the camera while this window "
            "is open. Controls keep working without the preview.");
}

void App::DrawPreview(const CameraState& s, float width, float height) {
    int texW = 0, texH = 0;
    ID3D11ShaderResourceView* srv = preview_.Running() ? preview_.Update(device_, ctx_, &texW, &texH) : nullptr;
    previewActive_ = preview_.Running();
    const ImVec2 start = ImGui::GetCursorPos();

    auto centredText = [&](const char* text, const ImVec4& colour) {
        const float wrap = width * 0.8f;
        ImVec2 sz = ImGui::CalcTextSize(text, nullptr, false, wrap);
        ImGui::SetCursorPos(ImVec2(start.x + (width - sz.x) * 0.5f, start.y + (height - sz.y) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    if (srv && texW > 0 && texH > 0) {
        const float scale = std::min(width / texW, height / texH);
        const ImVec2 size(texW * scale, texH * scale);
        ImGui::SetCursorPos(ImVec2(start.x + (width - size.x) * 0.5f, start.y + (height - size.y) * 0.5f));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv))), size);
        CornerBrackets(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), kAccentDim);
        return;
    }
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        CornerBrackets(ImGui::GetWindowDrawList(), a, ImVec2(a.x + width, a.y + height), Hex(0x67F7FF, 0.18f));
    }

    if (!s.connected) {
        centredText("Waiting for an OBSBOT camera...", kTextDim);
    } else if (!settings_.previewEnabled) {
        centredText("Preview is off.", kTextDim);
    } else if (std::string err = preview_.Error(); !err.empty()) {
        centredText(err.c_str(), kWarn);
        const char* retry = "Retry preview";
        const float bw = ImGui::CalcTextSize(retry).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX(start.x + (width - bw) * 0.5f);
        if (ImGui::Button(retry)) {
            // Go back to the default format in case the chosen one was the problem.
            previewFormatKey_.clear();
            previewAttemptPath_.clear();
        }
    } else {
        centredText("Starting preview...", kTextDim);
    }
}

// ---- Control tab -------------------------------------------------------------------------

void App::DrawControlTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    DrawAiTracking(s);
    DrawGimbal(s);
    ImGui::EndDisabled();
}

void App::DrawAiTracking(const CameraState& s) {
    SectionTitle("AI TRACKING");
    ImGui::BeginDisabled(!s.xu);
    const AiMode current = s.status.aiModeKnown ? s.status.aiMode : AiMode::Count;

    if (ToggleButton("Tracking off", current == AiMode::Off, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        worker_.SetAiMode(AiMode::Off);
    const AiMode modes[] = {AiMode::Normal, AiMode::UpperBody, AiMode::CloseUp,    AiMode::Headless, AiMode::LowerBody,
                            AiMode::Group,  AiMode::Hand,      AiMode::Whiteboard, AiMode::Desk};
    const float bw = EqualWidth(3);
    for (int i = 0; i < IM_ARRAYSIZE(modes); ++i) {
        const AiMode m = modes[i];
        if (i % 3) ImGui::SameLine(0, Gap());
        const char* label = m == AiMode::Normal ? "Normal" : obsbot::AiModeName(m);
        if (ToggleButton(label, m == current, ImVec2(bw, 0))) worker_.SetAiMode(m);
        if (m == AiMode::Desk) ImGui::SetItemTooltip("Desk mode may not work on every firmware version.");
    }

    RowLabel("Speed");
    const char* speeds[] = {"Standard", "Sport"};
    const char* speedTips[] = {"Smooth, steady framing.", "Follows fast movement more closely."};
    const int speed = Segmented("##speed", speeds, 2, s.status.sportTracking ? 1 : 0, speedTips);
    if (speed >= 0) worker_.SetSportTracking(speed == 1);

    if (s.status.valid && !s.status.aiModeKnown)
        ImGui::TextDisabled("Camera reports an unknown mode (%u, %u).", s.status.aiM, s.status.aiN);
    ImGui::EndDisabled();
    if (!s.xu && s.connected) ImGui::TextDisabled("AI controls need the OBSBOT extension unit (see Log).");
}

void App::DrawGimbal(const CameraState& s) {
    SectionTitle("GIMBAL AND ZOOM");
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float b = 46.0f * dpi;
    const float sp = Gap() * 0.5f;
    const ImVec2 origin = ImGui::GetCursorPos();
    const float padW = b * 3 + sp * 2;
    const float padH = b * 3 + sp * 2;

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

    // Position sliders beside the pad, centred on it vertically.
    const float rowsH = ImGui::GetFrameHeight() * 3 + ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::SetCursorPos(ImVec2(origin.x + padW + Gap() * 2, origin.y + std::max(0.0f, (padH - rowsH) * 0.5f)));
    ImGui::BeginGroup();
    // Absolute pan/tilt (UVC). Large ranges are arc-seconds: show degrees.
    auto angleSlider = [&](UvcCtl c, const char* label) {
        const UvcInfo& u = U(s, c);
        ImGui::PushID(label);
        RowLabel(label, CompactLabelWidth());
        if (!u.supported) {
            ImGui::TextDisabled("Not available");
            ImGui::PopID();
            return;
        }
        const float perDeg = (static_cast<int64_t>(u.max) - u.min) > 3600 ? 3600.0f : 1.0f;
        float deg = u.value / perDeg;
        const float mn = u.min / perDeg, mx = u.max / perDeg;
        if (SliderTrack("##v", ImGuiDataType_Float, &deg, &mn, &mx, "%.0f", TrackWidth(false, UnitSlot())))
            worker_.SetUvc(c, safety::AngleValue(deg, perDeg, u.min, u.max), false);
        char buf[32];
        snprintf(buf, sizeof buf, "%.0f", deg);
        ValueText(buf, "\xC2\xB0");
        ImGui::PopID();
    };
    angleSlider(UvcCtl::Pan, "Pan");
    angleSlider(UvcCtl::Tilt, "Tilt");
    UvcSlider(s, UvcCtl::Zoom, "Zoom", CompactLabelWidth(), false, UnitSlot());
    ImGui::EndGroup();
    const float groupBottom = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
    ImGui::SetCursorPos(ImVec2(origin.x, std::max(origin.y + padH, groupBottom) + ImGui::GetStyle().ItemSpacing.y));

    // How the arrow pad moves the camera.
    RowLabel("Movement");
    const char* methods[] = {"Smooth (vendor)", "Steps (UVC)"};
    const char* methodTips[] = {"Moves at a steady speed using OBSBOT's own command.",
                                "Moves in small standard webcam steps. Try this if Smooth doesn't move your camera."};
    const int method = Segmented("##method", methods, 2, settings_.moveMethod, methodTips);
    if (method >= 0 && method != settings_.moveMethod) {
        settings_.moveMethod = method;
        settings_.Save();
    }
    const bool smooth = settings_.moveMethod == static_cast<int>(MoveMethod::GimbalSpeed);

    ImGui::BeginDisabled(!smooth);
    RowLabel("Speed");
    const float minSpeed = 5.0f, maxSpeed = 90.0f;
    SliderTrack("##speed", ImGuiDataType_Float, &settings_.moveSpeed, &minSpeed, &maxSpeed, "%.0f",
                TrackWidth(false, UnitSlot()));
    if (!smooth) ImGui::SetItemTooltip("Only used by Smooth movement.");
    settings_.moveSpeed = safety::MoveSpeed(settings_.moveSpeed);
    char speedText[32];
    snprintf(speedText, sizeof speedText, "%.0f", settings_.moveSpeed);
    ValueText(speedText, "\xC2\xB0/s");
    ImGui::EndDisabled();

    RowLabel("Invert");
    if (ToggleSwitch("Horizontal", &settings_.invertHorizontal)) settings_.Save();
    ImGui::SetItemTooltip("Swap the left and right arrows.");
    ImGui::SameLine(0, Gap() * 3);
    if (ToggleSwitch("Vertical", &settings_.invertVertical)) settings_.Save();
    ImGui::SetItemTooltip("Swap the up and down arrows.");

    DrawPresets(s);

    // Hold-to-move. MoveSigns knows which axis each method runs the opposite way
    // on (checked on a Tiny 2 Lite) and applies the invert switches on top.
    const int dx = (right ? 1 : 0) - (left ? 1 : 0);
    const int dy = (up ? 1 : 0) - (down ? 1 : 0);
    if (dx || dy) {
        holding_ = true;
        const safety::MoveSign sign =
            safety::MoveSigns(smooth, dx, dy, settings_.invertHorizontal, settings_.invertVertical);
        if (smooth) {
            worker_.HoldGimbalVelocity(sign.tilt * settings_.moveSpeed, sign.pan * settings_.moveSpeed);
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastStep_ > std::chrono::milliseconds(150)) {
                lastStep_ = now;
                auto step = [&](UvcCtl c, int dir) {
                    const UvcInfo& u = U(s, c);
                    if (!dir || !u.supported) return;
                    worker_.SetUvc(c, safety::StepValue(u.value, u.min, u.max, u.step, dir), false);
                };
                step(UvcCtl::Pan, sign.pan);
                step(UvcCtl::Tilt, sign.tilt);
            }
        }
    }
}

// One row of three presets: click an empty one to save the current position, a
// saved one to go back to it; right-click either to update or clear it.
void App::DrawPresets(const CameraState& s) {
    const bool usable = U(s, UvcCtl::Pan).supported && U(s, UvcCtl::Tilt).supported;
    RowLabel("Presets", "Click an empty preset to save where the camera is pointing. Click a saved one to go "
                        "back there. Right-click a preset to update or clear it.");
    ImGui::BeginDisabled(!usable);
    const float bw = EqualWidth(static_cast<int>(settings_.presets.size()));
    for (size_t i = 0; i < settings_.presets.size(); ++i) {
        Preset& p = settings_.presets[i];
        auto save = [&] {
            p.saved = true;
            p.pan = U(s, UvcCtl::Pan).value;
            p.tilt = U(s, UvcCtl::Tilt).value;
            p.zoom = U(s, UvcCtl::Zoom).value;
            settings_.Save();
            LOG_INFO("Saved preset %zu (pan %ld, tilt %ld, zoom %ld).", i + 1, p.pan, p.tilt, p.zoom);
        };
        ImGui::PushID(static_cast<int>(i));
        if (i) ImGui::SameLine(0, Gap());
        char label[32];
        if (p.saved)
            snprintf(label, sizeof label, "Preset %zu", i + 1);
        else
            snprintf(label, sizeof label, "+ Save %zu", i + 1);
        ImGui::PushStyleColor(ImGuiCol_Text, p.saved ? kText : kTextDim);
        if (ImGui::Button(label, ImVec2(bw, 0))) {
            if (p.saved)
                worker_.GotoPosition(p.pan, p.tilt, p.zoom);
            else
                save();
        }
        ImGui::PopStyleColor();
        if (p.saved) {
            // A small accent tick on the left edge marks a saved preset.
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const float t = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.15f));
            const float inset = std::round((mx.y - mn.y) * 0.3f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x + t, mn.y + inset), ImVec2(mn.x + t * 2, mx.y - inset),
                                                      ImGui::GetColorU32(kAccent), t * 0.5f);
        }
        ImGui::SetItemTooltip(p.saved ? "Go to this position. Right-click to update or clear."
                                      : "Save where the camera is pointing now.");
        if (ImGui::BeginPopupContextItem("##menu")) {
            if (ImGui::MenuItem(p.saved ? "Update to the current position" : "Save the current position")) save();
            if (ImGui::MenuItem("Clear", nullptr, false, p.saved)) {
                p = Preset{};
                settings_.Save();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (s.connected && !usable) ImGui::TextDisabled("Presets need UVC pan/tilt support, which this camera doesn't report.");
}

// ---- Image tab -------------------------------------------------------------------------------

void App::UvcSlider(const CameraState& s, UvcCtl c, const char* label, float labelWidth, bool autoColumn,
                    float unitSlot) {
    const UvcInfo& u = U(s, c);
    ImGui::PushID(static_cast<int>(c));
    RowLabel(label, labelWidth);
    if (!u.supported) {
        ImGui::TextDisabled("Not available");
        ImGui::PopID();
        return;
    }
    ImGui::BeginDisabled(u.isAuto);
    int64_t v = u.value;
    const int64_t min = u.min, max = u.max;
    if (SliderTrack("##v", ImGuiDataType_S64, &v, &min, &max, "%lld", TrackWidth(autoColumn, unitSlot)))
        worker_.SetUvc(c, static_cast<long>(v), false);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) worker_.SetUvc(c, u.def, false);
    ImGui::SetItemTooltip("Double-click to reset (default %ld).", u.def);
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    ValueText(buf);
    ImGui::EndDisabled();
    if (autoColumn && u.canAuto) {
        ImGui::SameLine(0, Gap());
        bool a = u.isAuto;
        if (ToggleSwitch("Auto", &a)) worker_.SetUvc(c, u.value, a);
    }
    ImGui::PopID();
}

void App::DrawImageTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    const float lw = LabelWidth();
    SectionTitle("PICTURE");
    UvcSlider(s, UvcCtl::Brightness, "Brightness", lw, true);
    UvcSlider(s, UvcCtl::Contrast, "Contrast", lw, true);
    UvcSlider(s, UvcCtl::Saturation, "Saturation", lw, true);
    UvcSlider(s, UvcCtl::Sharpness, "Sharpness", lw, true);
    UvcSlider(s, UvcCtl::Hue, "Hue", lw, true);
    UvcSlider(s, UvcCtl::Gamma, "Gamma", lw, true);
    SectionTitle("COLOUR AND LIGHT");
    UvcSlider(s, UvcCtl::WhiteBalance, "White balance", lw, true);
    UvcSlider(s, UvcCtl::Exposure, "Exposure", lw, true);
    UvcSlider(s, UvcCtl::Gain, "Gain", lw, true);
    UvcSlider(s, UvcCtl::BacklightComp, "Backlight", lw, true);
    SectionTitle("FOCUS");
    UvcSlider(s, UvcCtl::Focus, "Focus", lw, true);
    ImGui::Spacing();
    const float bw = EqualWidth(2);
    if (ImGui::Button("Reset to defaults", ImVec2(bw, 0))) worker_.ResetImageDefaults();
    ImGui::SetItemTooltip("Put every picture setting back to the camera's own defaults.");
    ImGui::SameLine(0, Gap());
    if (ImGui::Button("Refresh", ImVec2(bw, 0))) worker_.RefreshAll();
    ImGui::SetItemTooltip("Read every setting from the camera again.");
    ImGui::EndDisabled();
}

// ---- Camera tab ------------------------------------------------------------------------------

void App::DrawCameraTab(const CameraState& s) {
    ImGui::BeginDisabled(!s.connected);
    SectionTitle("PICTURE MODE");
    ImGui::BeginDisabled(!s.xu);
    RowLabel("HDR", "High dynamic range: helps when there's a bright window behind you. May lower the frame rate.");
    bool hdr = s.status.hdr;
    if (ToggleSwitch("##hdr", &hdr)) worker_.SetHdr(hdr);

    RowLabel("Field of view");
    Fov currentFov = Fov::Count;
    if (s.status.valid) obsbot::FovFromStatus(s.status.fovRaw, s.variant, &currentFov);
    const char* fovs[static_cast<int>(Fov::Count)];
    for (int i = 0; i < static_cast<int>(Fov::Count); ++i) fovs[i] = obsbot::FovName(static_cast<Fov>(i));
    const int fov = Segmented("##fov", fovs, static_cast<int>(Fov::Count), static_cast<int>(currentFov));
    if (fov >= 0) worker_.SetFov(static_cast<Fov>(fov));
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    SectionTitle("ANTI-FLICKER");
    RowLabel("Mains power", "Matches the camera to your mains power to stop lights flickering on video. "
                            "New Zealand and Australia use 50 Hz. Applied each time the camera connects.");
    ImGui::BeginDisabled(!s.connected || !U(s, UvcCtl::PowerLine).supported);
    const long powerValues[] = {kPowerLineDisabled, kPowerLine50Hz, kPowerLine60Hz};
    const char* powerNames[] = {AntiFlickerName(powerValues[0]), AntiFlickerName(powerValues[1]),
                                AntiFlickerName(powerValues[2])};
    int powerSel = -1;
    for (int i = 0; i < 3; ++i)
        if (settings_.antiFlicker == powerValues[i]) powerSel = i;
    const int power = Segmented("##power", powerNames, 3, powerSel);
    if (power >= 0) {
        const int v = static_cast<int>(powerValues[power]);
        settings_.antiFlicker = v;
        worker_.SetAntiFlickerOnConnect(v);
        worker_.SetUvc(UvcCtl::PowerLine, v, false);
        settings_.Save();
    }
    ImGui::EndDisabled();
    if (s.connected && U(s, UvcCtl::PowerLine).supported) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelWidth());
        ImGui::TextDisabled("Camera currently reports: %s", AntiFlickerName(U(s, UvcCtl::PowerLine).value));
    }

    SectionTitle("ADVANCED");
    RowLabel("Developer tab", "Adds a Developer tab for testing raw camera commands. You won't need it for everyday use.");
    if (ToggleSwitch("##developer", &settings_.showDeveloper)) settings_.Save();

    SectionTitle("ABOUT");
    ImGui::Text("osbot-oss-windows %s", OSBOT_VERSION_STRING);
    ImGui::TextWrapped("Free and open-source software under the MIT Licence. Unofficial: not made by, "
                       "affiliated with or endorsed by OBSBOT.");
    ImGui::TextWrapped("Private by design: works entirely offline, with no accounts, telemetry or internet access.");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("Uses the Chakra Petch font (SIL Open Font Licence 1.1) and Dear ImGui (MIT). Portions of this "
                       "software are copyright \xC2\xA9 2024 The FreeType Project (www.freetype.org). All rights reserved.");
    ImGui::PopStyleColor();
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
    const float bw = EqualWidth(2);
    if (ImGui::Button("Copy diagnostics", ImVec2(bw, 0))) {
        ImGui::SetClipboardText(Diagnostics(s).c_str());
        LOG_INFO("Diagnostics copied to the clipboard.");
    }
    ImGui::SetItemTooltip("Copies camera details and this log so you can paste them into a bug report.");
    ImGui::SameLine(0, Gap());
    if (ImGui::Button("Clear log", ImVec2(bw, 0))) logx::Clear();

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
