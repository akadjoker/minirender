#pragma once
#include <vector>
#include "Config.hpp"
#include "Math.hpp"
#include "Color.hpp"
 

class Pixmap
{
public:
    Pixmap();
    ~Pixmap();
    Pixmap(int w, int h, int components);
    Pixmap(int w, int h, int components, unsigned char *data);
    Pixmap(const Pixmap &image, const IntRect &crop);
    Pixmap(const Pixmap &other) = delete;
    Pixmap &operator=(const Pixmap &other) = delete;

    // Pixel operations
    void SetPixel(u32 x, u32 y, u8 r, u8 g, u8 b, u8 a);
    void SetPixel(u32 x, u32 y, u32 rgba);
    u32 GetPixel(u32 x, u32 y) const;
    Color GetPixelColor(u32 x, u32 y) const;

    Pixmap* ConvertToRGBA() const;
    void Tint(u8 r, u8 g, u8 b);

    Pixmap* Resize(int newWidth, int newHeight) const;
     Pixmap* Crop(const IntRect &rect) const;
    Pixmap* Crop(int x, int y, int w, int h) const;
    Pixmap* CropExtended(const IntRect &rect, bool fillTransparent = true) const;
  

    // Fill operations
    void Fill(u8 r, u8 g, u8 b, u8 a);
    void Fill(u32 rgba);
    void Clear();

    // File operations
    bool Save(const char *file_name);
    bool Load(const char *file_name);
    bool LoadFromMemory(const unsigned char *buffer, unsigned int bytesRead);

    // Transform operations
    void FlipVertical();
    void FlipHorizontal();

 
    void DrawLine(int x1, int y1, int x2, int y2, const Color &color);
    void DrawRect(int x, int y, int w, int h, const Color &color, bool fill = false);
    void DrawCircle(int cx, int cy, int radius, const Color &color, bool fill = false);
    void DrawPixmap(const Pixmap &source, int x, int y);
    void DrawPixmap(const Pixmap &source, int x, int y, const IntRect &srcRect);

 
    void CopyRegion(const Pixmap &source, const IntRect &srcRect, int dstX, int dstY);

 
    void ReplaceColor(const Color &from, const Color &to, float threshold = 0.0f);
    void SetColorKey(const Color &key, float threshold = 0.0f);

   
    Pixmap *ApplyBlur(int radius) const;
    Pixmap *ApplyGaussianBlur(int radius) const;
    Pixmap *ApplySharpen() const;
    Pixmap *ApplyEdgeDetection() const;
    Pixmap *ApplyEmboss() const;

    bool IsValid() const { return pixels != nullptr; }
    int GetSize() const { return width * height * components; }
    bool HasAlpha() const { return components == 2 || components == 4; }

    unsigned char *pixels;
    int components;
    int width;
    int height;
};