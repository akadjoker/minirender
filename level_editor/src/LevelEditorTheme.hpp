#pragma once

enum class LevelEditorTheme
{
    Dark,
    Light,
    Classic,
    Studio
};

const char* levelEditorThemeName(LevelEditorTheme theme);
void applyLevelEditorTheme(LevelEditorTheme theme);
