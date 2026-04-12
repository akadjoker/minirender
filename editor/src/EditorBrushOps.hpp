#pragma once

#include <string>
#include <vector>

#include "EditorData.hpp"

std::string defaultBrushName(int index);
const std::string &brushDisplayName(const BrushVolume &brush, std::string &fallback, int index);
bool selectionContains(const std::vector<int> &selection, int index);
void selectionAddUnique(std::vector<int> &selection, int index);
void selectionRemove(std::vector<int> &selection, int index);
std::vector<int> buildSortedUniqueValidSelection(const std::vector<int> &selection, int brushCount);
std::vector<int> cloneSelectedBrushes(std::vector<BrushVolume> &brushes,
                                      const std::vector<int> &selection,
                                      float offset);
std::vector<BrushVolume> copySelectedBrushes(const std::vector<BrushVolume> &brushes,
                                             const std::vector<int> &selection);
std::vector<int> pasteBrushClipboard(std::vector<BrushVolume> &brushes,
                                     const std::vector<BrushVolume> &clipboard,
                                     float offset);
