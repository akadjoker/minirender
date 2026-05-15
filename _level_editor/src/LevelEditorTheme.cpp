#include "LevelEditorTheme.hpp"

#include "imgui.h"

const char* levelEditorThemeName(LevelEditorTheme theme)
{
    switch (theme)
    {
    case LevelEditorTheme::Dark: return "Dark";
    case LevelEditorTheme::Light: return "Light";
    case LevelEditorTheme::Classic: return "Classic";
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
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.98f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        break;
    case LevelEditorTheme::Light:
        ImGui::StyleColorsLight();
        break;
    case LevelEditorTheme::Classic:
        ImGui::StyleColorsClassic();
        break;
    case LevelEditorTheme::Studio:
        ImGui::StyleColorsDark();
        style.WindowRounding = 7.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 5.0f;
        style.FrameBorderSize = 1.0f;
        style.WindowBorderSize = 1.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.98f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.24f, 0.33f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.20f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.17f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.25f, 0.35f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.13f, 0.16f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.15f, 0.95f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.20f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.15f, 0.17f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.65f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.35f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.45f, 0.62f, 1.00f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);
        break;
    }
}
