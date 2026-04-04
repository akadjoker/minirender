#pragma once

#include "Animation.hpp"
#include "Mesh.hpp"

#include <string>
#include <vector>

class B3dLoader
{
public:
    // Load Blitz3D .b3d directlyinto AnimatedMesh.
    // If outAnimations != nullptr, animations are created and returned there.
    // Ownership of Animation* is transferred to caller.
    bool load(const std::string &b3dPath,
              const std::string &textureDir,
              AnimatedMesh *outMesh,
              std::vector<Animation *> *outAnimations = nullptr) const;
};

