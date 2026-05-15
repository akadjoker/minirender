#pragma once

#include <string>
#include <glm/glm.hpp>

enum class SpritePreviewViewMode
{
    Front,
    Side,
    Top,
    Custom
};

enum class SpriteFrontDirection
{
    Front,
    Back
};

enum class SpriteSideDirection
{
    Left,
    Right
};

enum class SpriteTopDirection
{
    Top,
    Bottom
};

struct SpriteProject
{
    std::string projectName = "untitled_sprite_project";
    std::string modelPath = "assets/models/character.h3d";
    std::string outputPath = "bin/sprites";
    std::string animationName;

    int frameStart = 0;
    int frameEnd = 24;
    float currentFrame = 0.0f;
    float animationFps = 12.0f;
    bool animationLoop = true;
    bool animationPlaying = true;

    // Transform channel filters
    bool usePositionChannel = true;
    bool useRotationChannel = true;
    bool useScaleChannel = true;

    int spriteWidth = 128;
    int spriteHeight = 128;

    float customPreviewYaw = 20.0f;
    float customPreviewPitch = -10.0f;
    float customPreviewZoom = 1.0f;
    float orthoPreviewZoom = 1.0f;
    SpriteFrontDirection frontDirection = SpriteFrontDirection::Front;
    SpriteSideDirection sideDirection = SpriteSideDirection::Right;
    SpriteTopDirection topDirection = SpriteTopDirection::Top;

    // Background and model transform
    glm::vec4 clearColor = glm::vec4(0.09f, 0.10f, 0.12f, 1.0f);
    std::string backgroundImagePath = "";
    glm::vec3 modelPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 modelRotation = glm::vec3(0.0f, 0.0f, 0.0f);  // X, Y, Z rotation in degrees
    glm::vec3 modelScale = glm::vec3(1.0f, 1.0f, 1.0f);     // Scale per axis

    bool viewFront = true;
    bool viewSide = true;
    bool viewTop = false;
    bool viewCustom = true;

    bool transparentBackground = true;
    bool exportAtlas = true;
    bool exportFrames = true;
    bool exportJson = true;
    bool trimOutput = false;
    std::string exportPrefix = "";
};
