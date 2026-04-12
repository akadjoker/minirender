#pragma once

enum class SpriteTheme
{
    Dark,
    Light,
    Classic,
    Studio
};

const char* spriteThemeName(SpriteTheme theme);
void applySpriteTheme(SpriteTheme theme);
