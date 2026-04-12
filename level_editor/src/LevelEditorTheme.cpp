#include "LevelEditorTheme.hpp"

#include "imgui.h"

const char* levelEditorThemeName(LevelEditorTheme theme)
{
    switch (theme)
    {
    case LevelEditorTheme::Dark: return "Dark";
    case LevelEditorTheme::Studio: return "Studio";
    }
    return "Dark";
}

void applyLevelEditorTheme(LevelEditorTheme theme)
{
    ImGuiStyle& style = ImGui::GetStyle();
    switch (theme)
    {
    case LevelEditorTheme::Dark:
        ImGui::StyleColorsDark();
        break;
    case LevelEditorTheme::Studio:
        ImGui::StyleColorsDark();
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.GrabRounding = 5.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.16f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.28f, 0.41f, 0.85f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.39f, 0.54f, 0.90f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.34f, 0.49f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.27f, 0.39f, 0.90f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.38f, 0.53f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.21f, 0.33f, 0.48f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.25f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.29f, 0.37f, 1.00f);
        break;
    }
}
