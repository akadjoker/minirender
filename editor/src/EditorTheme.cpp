#include "EditorTheme.hpp"

#include "imgui.h"

const char *themeName(EditorTheme theme)
{
    switch (theme)
    {
    case EditorTheme::Dark: return "Dark";
    case EditorTheme::Light: return "Light";
    case EditorTheme::Classic: return "Classic";
    case EditorTheme::Studio: return "Studio";
    }
    return "Dark";
}

void applyEditorTheme(EditorTheme theme)
{
    ImGuiStyle &style = ImGui::GetStyle();
    switch (theme)
    {
    case EditorTheme::Dark:
        ImGui::StyleColorsDark();
        break;
    case EditorTheme::Light:
        ImGui::StyleColorsLight();
        break;
    case EditorTheme::Classic:
        ImGui::StyleColorsClassic();
        break;
    case EditorTheme::Studio:
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
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.15f, 0.98f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.23f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.29f, 0.41f, 0.85f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.40f, 0.56f, 0.85f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.35f, 0.51f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.28f, 0.39f, 0.90f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.39f, 0.53f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.30f, 0.46f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.28f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.33f, 0.44f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.20f, 0.27f, 0.95f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.34f, 0.48f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.31f, 0.43f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.78f, 1.00f, 1.00f);
        break;
    }
}
