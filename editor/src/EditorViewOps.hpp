#pragma once

#include <array>

#include <glm/glm.hpp>

#include "EditorData.hpp"

const char *viewTypeName(EditorViewType type);
int viewTypeToComboIndex(EditorViewType type);
EditorViewType comboIndexToViewType(int index);
void syncViewLabels(std::array<EditorView, 4> &views);
void centerFocusForView(glm::vec3 &focus, EditorViewType viewType, const glm::vec3 &target);
glm::vec3 orthoPointFromScreen(const EditorView &view, const glm::vec3 &focus, const glm::vec2 &mousePos);
void panFocusInOrthoView(glm::vec3 &focus, const EditorView &view, const glm::vec2 &mouseDelta);
void panFocusInPerspective(glm::vec3 &focus, const EditorView &view, const glm::vec2 &mouseDelta);
void setupViewLayout(std::array<EditorView, 4> &views,
                     int screenWidth,
                     int screenHeight,
                     int sidebarWidth,
                     int assetPanelHeight,
                     EditorLayoutMode layoutMode,
                     int topInset,
                     int margin,
                     int gap);
void updateCameras(std::array<EditorView, 4> &views, const glm::vec3 &focus);
