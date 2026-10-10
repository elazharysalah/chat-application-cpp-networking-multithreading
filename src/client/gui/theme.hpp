#pragma once

#include <imgui.h>

#include <string_view>

namespace ui {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* caption = nullptr;
    ImFont* title = nullptr;
};

extern Fonts fonts;
extern float scale;  // DPI scale factor; every hard-coded size goes through px()

inline float px(float v) { return v * scale; }

namespace col {

constexpr ImU32 rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

inline constexpr ImU32 appBg = rgba(22, 24, 32);
inline constexpr ImU32 sidebar = rgba(16, 18, 25);
inline constexpr ImU32 panel = rgba(30, 33, 44);
inline constexpr ImU32 elevated = rgba(36, 39, 52);
inline constexpr ImU32 border = rgba(46, 50, 66);
inline constexpr ImU32 hover = rgba(255, 255, 255, 12);

inline constexpr ImU32 text = rgba(232, 234, 242);
inline constexpr ImU32 textDim = rgba(150, 156, 176);
inline constexpr ImU32 textFaint = rgba(104, 110, 130);
inline constexpr ImU32 white = rgba(255, 255, 255);

inline constexpr ImU32 accent = rgba(99, 102, 241);
inline constexpr ImU32 accentHover = rgba(124, 127, 250);
inline constexpr ImU32 accentActive = rgba(84, 86, 222);
inline constexpr ImU32 accentSoft = rgba(99, 102, 241, 48);
inline constexpr ImU32 accentText = rgba(165, 168, 255);

inline constexpr ImU32 bubble = rgba(38, 41, 55);
inline constexpr ImU32 bubbleHover = rgba(45, 49, 65);

inline constexpr ImU32 online = rgba(34, 197, 94);
inline constexpr ImU32 warning = rgba(245, 158, 11);
inline constexpr ImU32 warningSoft = rgba(245, 158, 11, 36);
inline constexpr ImU32 danger = rgba(239, 68, 68);
inline constexpr ImU32 dangerSoft = rgba(239, 68, 68, 38);
inline constexpr ImU32 dangerText = rgba(252, 165, 165);

}  // namespace col

// Loads fonts (Segoe UI) and applies the dark theme. Call once after ImGui::CreateContext().
void setup(float dpiScale);

ImU32 avatarColor(std::string_view name);
ImU32 withAlpha(ImU32 color, float alpha);

}  // namespace ui
