#pragma once

#include "Math.hpp"
#include "TextMap.hpp"

#include <vector>

struct DemoClipPlane
{
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float dist = 0.0f;
};

struct DemoClipBrush
{
    std::vector<DemoClipPlane> planes;
    BoundingBox bounds = {};
};

class MapClipper
{
public:
    void clear();
    void addBrush(const TextMapBrush &brush,
                  bool remapZUpToYUp = true,
                  const glm::vec3 &offset = glm::vec3(0.0f));
    void addEntity(const TextMapEntity &entity,
                   bool remapZUpToYUp = true,
                   const glm::vec3 &offset = glm::vec3(0.0f));
    void appendTransformed(const MapClipper &other, const glm::vec3 &offset);

    int brushCount() const { return static_cast<int>(brushes_.size()); }

    int traceSphere(glm::vec3 &ioPosition,
                    const glm::vec3 &desiredEnd,
                    float radius,
                    bool &outHitGround) const;

    bool dropToFloor(glm::vec3 &ioPosition,
                     float radius,
                     float maxDrop,
                     bool &outHitGround) const;

private:
    struct ClipState
    {
        float dist = 1.0f;
        glm::vec3 end = glm::vec3(0.0f);
        DemoClipPlane plane = {};
        bool hasPlane = false;
        bool startSolid = false;
    };

    std::vector<DemoClipBrush> brushes_;

    std::vector<const DemoClipBrush *> gatherBrushes(const glm::vec3 &start,
                                                     const glm::vec3 &end,
                                                     float radius) const;

    void clipCheckBrushes(const glm::vec3 &start,
                          const glm::vec3 &end,
                          float radius,
                          const std::vector<const DemoClipBrush *> &brushes,
                          ClipState &outState) const;

    int clipMoveSlide(glm::vec3 &ioPosition,
                      const glm::vec3 &desiredEnd,
                      float radius,
                      const std::vector<const DemoClipBrush *> &brushes,
                      bool &outHitGround) const;
};
