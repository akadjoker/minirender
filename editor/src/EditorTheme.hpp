#pragma once

enum class EditorTheme
{
    Dark,
    Light,
    Classic,
    Studio
};

const char *themeName(EditorTheme theme);
void applyEditorTheme(EditorTheme theme);
