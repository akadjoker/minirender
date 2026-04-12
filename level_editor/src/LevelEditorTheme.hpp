#pragma once

enum class LevelEditorTheme
{
    Dark,
    Studio
};

const char* levelEditorThemeName(LevelEditorTheme theme);
void applyLevelEditorTheme(LevelEditorTheme theme);
