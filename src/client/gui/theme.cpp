#include "theme.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace ui {

Fonts fonts;
float scale = 1.0f;

namespace {

ImVector<ImWchar> g_glyphRanges;  // must outlive the font atlas build

const ImWchar* glyphRanges() {
    if (g_glyphRanges.empty()) {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(atlas->GetGlyphRangesDefault());
        builder.AddRanges(atlas->GetGlyphRangesCyrillic());
        builder.AddRanges(atlas->GetGlyphRangesGreek());
        static const ImWchar extra[] = {
            0x0100, 0x024F,  // Latin Extended-A/B
            0x2000, 0x206F,  // General Punctuation (… • – —)
            0x20A0, 0x20CF,  // Currency symbols
            0x2190, 0x21FF,  // Arrows
            0,
        };
        builder.AddRanges(extra);
        builder.BuildRanges(&g_glyphRanges);
    }
    return g_glyphRanges.Data;
}

std::string systemFontPath(const char* file) {
    char dir[MAX_PATH] = {};
    const UINT n = GetWindowsDirectoryA(dir, MAX_PATH);
    return (n > 0 && n < MAX_PATH ? std::string(dir, n) : std::string("C:\\Windows")) + "\\Fonts\\" + file;
}

ImFont* loadFont(const char* file, float size) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;

    const std::string path = systemFontPath(file);
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return atlas->AddFontFromFileTTF(path.c_str(), size * scale, &config, glyphRanges());

    config.SizePixels = size * scale;
    return atlas->AddFontDefault(&config);
}

ImVec4 v4(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }

void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(12, 10);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 10;
    style.GrabMinSize = 10;

    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;

    style.WindowRounding = 0;
    style.ChildRounding = 12;
    style.FrameRounding = 10;
    style.PopupRounding = 10;
    style.ScrollbarRounding = 10;
    style.GrabRounding = 10;
    style.ScaleAllSizes(scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = v4(col::text);
    c[ImGuiCol_TextDisabled] = v4(col::textFaint);
    c[ImGuiCol_WindowBg] = v4(col::appBg);
    c[ImGuiCol_ChildBg] = v4(col::rgba(0, 0, 0, 0));
    c[ImGuiCol_PopupBg] = v4(col::panel);
    c[ImGuiCol_Border] = v4(col::border);
    c[ImGuiCol_BorderShadow] = v4(col::rgba(0, 0, 0, 0));
    c[ImGuiCol_FrameBg] = v4(col::elevated);
    c[ImGuiCol_FrameBgHovered] = v4(col::rgba(42, 46, 61));
    c[ImGuiCol_FrameBgActive] = v4(col::rgba(46, 50, 67));
    c[ImGuiCol_Button] = v4(col::accent);
    c[ImGuiCol_ButtonHovered] = v4(col::accentHover);
    c[ImGuiCol_ButtonActive] = v4(col::accentActive);
    c[ImGuiCol_Header] = v4(col::accentSoft);
    c[ImGuiCol_HeaderHovered] = v4(col::rgba(99, 102, 241, 80));
    c[ImGuiCol_HeaderActive] = v4(col::rgba(99, 102, 241, 110));
    c[ImGuiCol_Separator] = v4(col::border);
    c[ImGuiCol_ScrollbarBg] = v4(col::rgba(0, 0, 0, 0));
    c[ImGuiCol_ScrollbarGrab] = v4(col::rgba(58, 62, 80));
    c[ImGuiCol_ScrollbarGrabHovered] = v4(col::rgba(74, 79, 100));
    c[ImGuiCol_ScrollbarGrabActive] = v4(col::rgba(90, 95, 118));
    c[ImGuiCol_TextSelectedBg] = v4(col::rgba(99, 102, 241, 110));
    c[ImGuiCol_CheckMark] = v4(col::accent);
}

}  // namespace

void setup(float dpiScale) {
    scale = dpiScale > 0.0f ? dpiScale : 1.0f;
    fonts.regular = loadFont("segoeui.ttf", 17.0f);  // first font = ImGui default
    fonts.bold = loadFont("seguisb.ttf", 17.0f);
    fonts.caption = loadFont("segoeui.ttf", 14.0f);
    fonts.title = loadFont("seguisb.ttf", 28.0f);
    applyStyle();
}

ImU32 avatarColor(std::string_view name) {
    static constexpr ImU32 palette[] = {
        col::rgba(239, 68, 68),  col::rgba(249, 115, 22), col::rgba(234, 179, 8),  col::rgba(34, 197, 94),
        col::rgba(20, 184, 166), col::rgba(59, 130, 246), col::rgba(139, 92, 246), col::rgba(236, 72, 153),
    };
    uint32_t hash = 2166136261u;  // FNV-1a, case-insensitive so "Alice" and "alice" match
    for (char ch : name) {
        hash ^= static_cast<uint32_t>(std::tolower(static_cast<unsigned char>(ch)));
        hash *= 16777619u;
    }
    return palette[hash % (sizeof palette / sizeof palette[0])];
}

ImU32 withAlpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

}  // namespace ui
