#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Pixmap;

struct M8MipLevel
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;
};

class M8Image
{
public:
    bool loadFromFile(const std::string &path, std::string *error = nullptr);
    void clear();

    bool isValid() const { return !levels_.empty(); }
    int mipCount() const { return static_cast<int>(levels_.size()); }
    const std::string &name() const { return name_; }
    const std::string &animationName() const { return animationName_; }
    const std::vector<uint8_t> &palette() const { return palette_; }
    const M8MipLevel *mipLevel(int index) const;

    Pixmap *createPixmap(int mipLevel = 0) const;

private:
    std::string name_;
    std::string animationName_;
    std::vector<uint8_t> palette_;
    std::vector<M8MipLevel> levels_;
};
