#pragma once

#include "Animation.hpp"
#include "Mesh.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class IqmLoader
{
public:
    struct FrameGroupClip
    {
        std::string name;
        int firstFrame = 0;
        int numFrames = 0;
        float fps = 24.0f;
        bool loop = true;
    };

    // Load an IQM file directly  into an AnimatedMesh.
    // If outAnimations != nullptr, animations are created and returned there.
    // Ownership of returned Animation* is transferred to caller.
    bool load(const std::string &iqmPath,
              const std::string &textureDir,
              AnimatedMesh *outMesh,
              std::vector<Animation *> *outAnimations = nullptr) const;

    // Optional helpers exposed for tooling/debug.
    static bool parseFrameGroups(const std::string &path,
                                 std::vector<FrameGroupClip> &outClips);
    static bool parseSkinFile(const std::string &path,
                              std::unordered_map<std::string, std::string> &outMap);
};

