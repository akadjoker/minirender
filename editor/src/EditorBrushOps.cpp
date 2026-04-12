#include "EditorBrushOps.hpp"

#include <algorithm>

#include <glm/glm.hpp>

std::string defaultBrushName(int index)
{
    return "Brush " + std::to_string(index + 1);
}

const std::string &brushDisplayName(const BrushVolume &brush, std::string &fallback, int index)
{
    if (!brush.name.empty())
        return brush.name;
    fallback = defaultBrushName(index);
    return fallback;
}

bool selectionContains(const std::vector<int> &selection, int index)
{
    for (int selected : selection)
    {
        if (selected == index)
            return true;
    }
    return false;
}

void selectionAddUnique(std::vector<int> &selection, int index)
{
    if (!selectionContains(selection, index))
        selection.push_back(index);
}

void selectionRemove(std::vector<int> &selection, int index)
{
    for (auto it = selection.begin(); it != selection.end(); ++it)
    {
        if (*it == index)
        {
            selection.erase(it);
            return;
        }
    }
}

std::vector<int> buildSortedUniqueValidSelection(const std::vector<int> &selection, int brushCount)
{
    std::vector<int> result;
    result.reserve(selection.size());
    for (int index : selection)
    {
        if (index >= 0 && index < brushCount)
            result.push_back(index);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<int> cloneSelectedBrushes(std::vector<BrushVolume> &brushes,
                                      const std::vector<int> &selection,
                                      float offset)
{
    const std::vector<int> source = buildSortedUniqueValidSelection(selection, (int)brushes.size());
    const glm::vec3 delta(offset, 0.0f, offset);

    std::vector<int> newSelection;
    newSelection.reserve(source.size());
    for (int index : source)
    {
        BrushVolume copy = brushes[(size_t)index];
        copy.mins += delta;
        copy.maxs += delta;
        if (!copy.name.empty())
            copy.name += " Copy";
        else
            copy.name = defaultBrushName((int)brushes.size());
        brushes.push_back(copy);
        newSelection.push_back((int)brushes.size() - 1);
    }
    return newSelection;
}

std::vector<BrushVolume> copySelectedBrushes(const std::vector<BrushVolume> &brushes,
                                             const std::vector<int> &selection)
{
    const std::vector<int> source = buildSortedUniqueValidSelection(selection, (int)brushes.size());
    std::vector<BrushVolume> clipboard;
    clipboard.reserve(source.size());
    for (int index : source)
        clipboard.push_back(brushes[(size_t)index]);
    return clipboard;
}

std::vector<int> pasteBrushClipboard(std::vector<BrushVolume> &brushes,
                                     const std::vector<BrushVolume> &clipboard,
                                     float offset)
{
    if (clipboard.empty())
        return {};

    const glm::vec3 delta(offset, 0.0f, offset);
    std::vector<int> newSelection;
    newSelection.reserve(clipboard.size());
    for (const BrushVolume &source : clipboard)
    {
        BrushVolume copy = source;
        copy.mins += delta;
        copy.maxs += delta;
        if (!copy.name.empty())
            copy.name += " Copy";
        else
            copy.name = defaultBrushName((int)brushes.size());
        brushes.push_back(copy);
        newSelection.push_back((int)brushes.size() - 1);
    }
    return newSelection;
}
