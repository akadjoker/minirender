#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "Math.hpp"
#include "Color.hpp"

struct Texture;
class RenderBatch;
class Pixmap;

// ========================================
// Enums e Estruturas
// ========================================

enum class TextAlign
{
    LEFT,
    CENTER,
    RIGHT,
    JUSTIFY
};

enum class TextVAlign
{
    TOP,
    MIDDLE,
    BOTTOM
};

struct GlyphInfo
{
    int value;      // Character value (Unicode)
    int offsetX;    // Offset X when drawing
    int offsetY;    // Offset Y when drawing
    int advanceX;   // Advance X position
};

struct TextMetrics
{
    glm::vec2 size;           // Total size
    float lineHeight;    // Height of one line
    int lineCount;       // Number of lines
    float maxLineWidth;  // Width of longest line
    std::vector<float> lineWidths; // Width of each line
};

struct TextStyle
{
    Color color = Color::WHITE;
    float fontSize = 16.0f;
    float spacing = 1.0f;
    float lineSpacing = 0.0f; // Extra spacing between lines (0 = default)
    
    // Effects
    bool enableShadow = false;
    Color shadowColor = Color::BLACK;
    glm::vec2 shadowOffset = glm::vec2(1, 1);
    
    bool enableOutline = false;
    Color outlineColor = Color::BLACK;
    float outlineThickness = 1.0f;
    
    // Alignment
    TextAlign align = TextAlign::LEFT;
    TextVAlign valign = TextVAlign::TOP;
};

// ========================================
// Font Class
// ========================================

class Font
{
public:
    Font();
    ~Font();
    
    // ========================================
    // Loading Methods
    // ========================================
    
    // Load default bitmap font
    bool LoadDefaultFont();
    
    // Load bitmap font from Angel Code format (.fnt + texture)
    bool LoadBMFont(const char* fntPath, const char* texturePath = nullptr);
    
    // Load from pixmap (custom bitmap font)
    bool LoadFromPixmap(const Pixmap& pixmap, int charWidth, int charHeight, 
                        const char* charset = nullptr);
    
    // Load TrueType font (requires stb_truetype)
    bool LoadTTF(const char* ttfPath, float fontSize, int atlasSize = 512);
    bool LoadTTFFromMemory(const unsigned char* data, int dataSize, 
                          float fontSize, int atlasSize = 512);
    
    void Release();
    
    // ========================================
    // Rendering Methods
    // ========================================
    
    // Simple print
    void Print(const char* text, float x, float y);
    void Print(float x, float y, const char* fmt, ...);
    
    // Print with style
    void Print(const char* text, float x, float y, const TextStyle& style);
    
    // Print with alignment and bounds
    void PrintAligned(const char* text, const FloatRect& bounds, 
                     TextAlign align = TextAlign::LEFT,
                     TextVAlign valign = TextVAlign::TOP);
    
    // Print with automatic word wrap
    void PrintWrapped(const char* text, float x, float y, float maxWidth);
    void PrintWrapped(const char* text, const FloatRect& bounds, 
                     const TextStyle& style);
    
    // Print single line with effects
    void PrintWithShadow(const char* text, float x, float y, 
                        const Color& textColor, const Color& shadowColor,
                        const glm::vec2& shadowOffset = glm::vec2(1, 1));
    
    void PrintWithOutline(const char* text, float x, float y,
                         const Color& textColor, const Color& outlineColor,
                         float thickness = 1.0f);
    
    // ========================================
    // Text Measurement
    // ========================================
    
    glm::vec2 GetTextSize(const char* text);
    float GetTextWidth(const char* text);
    float GetTextHeight(const char* text);
    
    // Get detailed metrics
    TextMetrics MeasureText(const char* text, float maxWidth = 0.0f);
    
    // Get character width
    float GetCharWidth(int codepoint);
    
    // Word wrapping helper
    std::vector<std::string> WrapText(const char* text, float maxWidth);
    
    // ========================================
    // Configuration
    // ========================================
    
    void SetBatch(RenderBatch* batch) { this->batch = batch; }
    
    void SetColor(const Color& color) { this->color = color; }
    void SetColor(u8 r, u8 g, u8 b, u8 a = 255);
    
    void SetFontSize(float size) { fontSize = size; }
    void SetSpacing(float spacing) { this->spacing = spacing; }
    void SetLineSpacing(float spacing) { textLineSpacing = spacing; }
    
    void SetClip(int x, int y, int w, int h);
    void EnableClip(bool enable);
    
    // ========================================
    // Getters
    // ========================================
    
    float GetFontSize() const { return fontSize; }
    float GetSpacing() const { return spacing; }
    float GetLineSpacing() const { return textLineSpacing; }
    int GetBaseSize() const { return m_baseSize; }
    int GetGlyphCount() const { return m_glyphCount; }
    Texture* GetTexture() const { return texture; }
    
    bool IsValid() const { return texture != nullptr && m_glyphCount > 0; }
    
private:
    // ========================================
    // Internal Methods
    // ========================================
    
    void drawTextCodepoint(int codepoint, float x, float y);
    void drawTextCodepointWithStyle(int codepoint, float x, float y, 
                                   const TextStyle& style);
    
    int getGlyphIndex(int codepoint);
    
 
    
 
    std::vector<std::string> SplitIntoLines(const char* text);
    float GetLineWidth(const char* line);
    
    // ========================================
    // Member Variables
    // ========================================
    
    Texture* texture;
    RenderBatch* batch;
    
    // Font data
    int m_baseSize;
    int m_glyphCount;
    int m_glyphPadding;
    std::vector<FloatRect> m_recs;
    std::vector<GlyphInfo> m_glyphs;
    
    // Fast lookup cache (codepoint -> glyph index)
    std::unordered_map<int, int> m_glyphCache;
    
    // Rendering state
    Color color;
    float fontSize;
    float spacing;
    float textLineSpacing;
    
    bool enableClip;
    IntRect clip;
    
    // Temporary arrays for quad rendering
    glm::vec2 coords[4];
    glm::vec2 texcoords[4];
};

 