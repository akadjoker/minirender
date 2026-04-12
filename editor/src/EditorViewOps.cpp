#include "EditorViewOps.hpp"

#include <cmath>

#include <glm/gtx/norm.hpp>

const char *viewTypeName(EditorViewType type)
{
    switch (type)
    {
    case EditorViewType::Top: return "Top";
    case EditorViewType::Bottom: return "Bottom";
    case EditorViewType::Front: return "Front";
    case EditorViewType::Back: return "Back";
    case EditorViewType::Left: return "Left";
    case EditorViewType::Right: return "Right";
    case EditorViewType::Perspective: return "3D";
    }
    return "View";
}

int viewTypeToComboIndex(EditorViewType type)
{
    switch (type)
    {
    case EditorViewType::Top: return 0;
    case EditorViewType::Bottom: return 1;
    case EditorViewType::Front: return 2;
    case EditorViewType::Back: return 3;
    case EditorViewType::Left: return 4;
    case EditorViewType::Right: return 5;
    case EditorViewType::Perspective: return 6;
    }
    return 0;
}

EditorViewType comboIndexToViewType(int index)
{
    switch (index)
    {
    case 0: return EditorViewType::Top;
    case 1: return EditorViewType::Bottom;
    case 2: return EditorViewType::Front;
    case 3: return EditorViewType::Back;
    case 4: return EditorViewType::Left;
    case 5: return EditorViewType::Right;
    case 6: return EditorViewType::Perspective;
    default: break;
    }
    return EditorViewType::Top;
}

void syncViewLabels(std::array<EditorView, 4> &views)
{
    for (EditorView &view : views)
        view.label = viewTypeName(view.type);
}

void centerFocusForView(glm::vec3 &focus, EditorViewType viewType, const glm::vec3 &target)
{
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        focus.x = target.x;
        focus.z = target.z;
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        focus.x = target.x;
        focus.y = target.y;
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        focus.z = target.z;
        focus.y = target.y;
        break;
    case EditorViewType::Perspective:
        focus = target;
        break;
    }
}

glm::vec3 orthoPointFromScreen(const EditorView &view, const glm::vec3 &focus, const glm::vec2 &mousePos)
{
    const float aspect = (view.rect.h > 0) ? ((float)view.rect.w / (float)view.rect.h) : 1.0f;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;

    const float localX = ((mousePos.x - (float)view.rect.x) / (float)view.rect.w) * 2.0f - 1.0f;
    const float localY = 1.0f - ((mousePos.y - (float)view.rect.y) / (float)view.rect.h) * 2.0f;

    switch (view.type)
    {
    case EditorViewType::Top:
        return glm::vec3(focus.x + localX * halfW, focus.y, focus.z - localY * halfH);
    case EditorViewType::Bottom:
        return glm::vec3(focus.x + localX * halfW, focus.y, focus.z + localY * halfH);
    case EditorViewType::Front:
        return glm::vec3(focus.x + localX * halfW, focus.y + localY * halfH, focus.z);
    case EditorViewType::Back:
        return glm::vec3(focus.x - localX * halfW, focus.y + localY * halfH, focus.z);
    case EditorViewType::Left:
        return glm::vec3(focus.x, focus.y + localY * halfH, focus.z + localX * halfW);
    case EditorViewType::Right:
        return glm::vec3(focus.x, focus.y + localY * halfH, focus.z - localX * halfW);
    case EditorViewType::Perspective:
        break;
    }

    return focus;
}

void panFocusInOrthoView(glm::vec3 &focus, const EditorView &view, const glm::vec2 &mouseDelta)
{
    if (view.rect.w <= 0 || view.rect.h <= 0)
        return;

    const float aspect = (float)view.rect.w / (float)view.rect.h;
    const float halfH = view.orthoSize;
    const float halfW = halfH * aspect;
    const float worldPerPixelX = (2.0f * halfW) / (float)view.rect.w;
    const float worldPerPixelY = (2.0f * halfH) / (float)view.rect.h;

    switch (view.type)
    {
    case EditorViewType::Top:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.z += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Bottom:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.z -= mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Front:
        focus.x -= mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Back:
        focus.x += mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Left:
        focus.z -= mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Right:
        focus.z += mouseDelta.x * worldPerPixelX;
        focus.y += mouseDelta.y * worldPerPixelY;
        break;
    case EditorViewType::Perspective:
        break;
    }
}

void panFocusInPerspective(glm::vec3 &focus, const EditorView &view, const glm::vec2 &mouseDelta)
{
    const float yaw = glm::radians(view.perspectiveYaw);
    const float pitch = glm::radians(view.perspectivePitch);
    const glm::vec3 offset(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw));

    const glm::vec3 forward = glm::normalize(-offset);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (glm::length2(right) < 1e-8f)
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const float panScale = glm::max(view.perspectiveDistance * 0.0015f, 0.01f);
    focus += (-right * mouseDelta.x + up * mouseDelta.y) * panScale;
}

void setupViewLayout(std::array<EditorView, 4> &views,
                     int screenWidth,
                     int screenHeight,
                     int sidebarWidth,
                     int assetPanelHeight,
                     EditorLayoutMode layoutMode,
                     int topInset,
                     int margin,
                     int gap)
{
    const int layoutX = sidebarWidth + margin;
    const int layoutY = topInset + margin;
    const int layoutW = screenWidth - layoutX - margin;
    const int layoutH = screenHeight - layoutY - margin - assetPanelHeight - gap;
    for (EditorView &view : views)
        view.rect = {};

    if (layoutMode == EditorLayoutMode::TwoViews)
    {
        const int halfW = (layoutW - gap) / 2;
        views[0].rect = {layoutX, layoutY, halfW, layoutH};
        views[1].rect = {layoutX + halfW + gap, layoutY, halfW, layoutH};
        return;
    }

    if (layoutMode == EditorLayoutMode::ThreeViews)
    {
        const int leftW = (layoutW - gap) * 3 / 5;
        const int rightW = layoutW - gap - leftW;
        const int halfH = (layoutH - gap) / 2;

        views[0].rect = {layoutX, layoutY, leftW, layoutH};
        views[1].rect = {layoutX + leftW + gap, layoutY, rightW, halfH};
        views[2].rect = {layoutX + leftW + gap, layoutY + halfH + gap, rightW, halfH};
        return;
    }

    const int halfW = (layoutW - gap) / 2;
    const int halfH = (layoutH - gap) / 2;
    views[0].rect = {layoutX, layoutY, halfW, halfH};
    views[1].rect = {layoutX + halfW + gap, layoutY, halfW, halfH};
    views[2].rect = {layoutX, layoutY + halfH + gap, halfW, halfH};
    views[3].rect = {layoutX + halfW + gap, layoutY + halfH + gap, halfW, halfH};
}

void updateCameras(std::array<EditorView, 4> &views, const glm::vec3 &focus)
{
    constexpr float orthoDistance = 1024.0f;

    for (EditorView &view : views)
    {
        view.camera.setViewport(0, 0, view.rect.w, view.rect.h);
        view.camera.setViewPlanes(0.1f, 8192.0f);

        if (view.type == EditorViewType::Perspective)
        {
            view.focus = focus;
            view.camera.setProjectionType(ProjectionType::Perspective);
            view.camera.setFov(60.0f);

            const float yaw = glm::radians(view.perspectiveYaw);
            const float pitch = glm::radians(view.perspectivePitch);
            const glm::vec3 offset(
                std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw));
            view.camera.setPosition(focus + offset * view.perspectiveDistance);
            view.camera.lookAt(focus, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            const glm::vec3 viewFocus = view.focus;
            view.camera.setProjectionType(ProjectionType::Orthographic);
            view.camera.orthoSize = view.orthoSize;

            switch (view.type)
            {
            case EditorViewType::Top:
                view.camera.setPosition(viewFocus + glm::vec3(0.0f, orthoDistance, 0.0f));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 0.0f, -1.0f));
                break;
            case EditorViewType::Bottom:
                view.camera.setPosition(viewFocus + glm::vec3(0.0f, -orthoDistance, 0.0f));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 0.0f, 1.0f));
                break;
            case EditorViewType::Front:
                view.camera.setPosition(viewFocus + glm::vec3(0.0f, 0.0f, orthoDistance));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Back:
                view.camera.setPosition(viewFocus + glm::vec3(0.0f, 0.0f, -orthoDistance));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Left:
                view.camera.setPosition(viewFocus + glm::vec3(-orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Right:
                view.camera.setPosition(viewFocus + glm::vec3(orthoDistance, 0.0f, 0.0f));
                view.camera.lookAt(viewFocus, glm::vec3(0.0f, 1.0f, 0.0f));
                break;
            case EditorViewType::Perspective:
                break;
            }
        }

        view.camera.updateMatrices();
    }
}
