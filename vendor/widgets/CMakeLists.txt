project(bugui_widgets)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Library type: STATIC by default; override from parent if needed ───────────
if(NOT DEFINED BUGUI_LIB_TYPE)
    set(BUGUI_LIB_TYPE STATIC)
endif()

# stb implementations (truetype, rect_pack) live in core's stb_impl.cpp.
# bugui_widgets only needs the headers — they are provided via vendor/stb.

# ── Migrated sources (add back one by one as each file is ported) ─────────────
set(SOURCES
    src/BuGUI.cpp
    src/Widget.cpp
    src/WidgetApp.cpp
    src/FileSystem.cpp
    src/BuImage.cpp
    src/IconAtlas.cpp
    src/BasicWidgets.cpp
    src/LayoutWidgets.cpp
    src/InputWidgets.cpp
    src/ScrollWidgets.cpp # needs SDL + widget type registration
    src/TextInputWidgets.cpp
    src/ComboBox.cpp
    src/MenuWidgets.cpp
    src/DialogWidgets.cpp
    src/TreePropertyColorWidgets.cpp
    src/DataWidgets.cpp
    src/ViewWidgets.cpp
    src/GizmoWidgets.cpp
    src/ChartWidgets.cpp
    src/NodeEditor.cpp
    src/Timeline.cpp
    src/AudioWidgets.cpp
    src/ThumbnailGrid.cpp
    src/DirCache.cpp
    src/FileDialog.cpp
    src/ConsoleWidget.cpp
    src/DockPanel.cpp
    src/WidgetSerializer.cpp
    src/AssetBrowser.cpp
    src/ToolBar.cpp
    src/CodeEditor.cpp
    src/AutomotiveWidgets.cpp
    src/Animation.cpp
    src/ItemModel.cpp
    src/AppWidgets.cpp
    # src/InputWidgets.cpp
    # src/LayoutWidgets.cpp
    # src/TextEdit.cpp
    # src/MenuWidgets.cpp
    # src/ScrollWidgets.cpp
    # src/ViewWidgets.cpp
    # src/Dialogs.cpp
    # src/FileDialog.cpp
    # src/DockPanel.cpp
    # src/DataGrid.cpp
    # src/TreeGrid.cpp
    # src/TreePropertyColor.cpp
    # src/ListWidgets.cpp (if exists)
    # src/TextWidgets.cpp (if exists)
    # ... specialty widgets below
    # src/Gizmo2D.cpp
    # src/Gizmo3D.cpp
    # src/NodeEditor.cpp
    # src/Timeline.cpp
    # src/CurveEditor.cpp
    # src/PianoRoll.cpp
    # src/Knob.cpp
    # src/VUMeter.cpp
    # src/WaveformView.cpp
    # src/SpectrumAnalyzer.cpp
    # src/HistogramWidget.cpp
    # src/PlotWidget.cpp
    # src/ADSRWidget.cpp
    # src/GradientEditor.cpp
    # src/AssetBrowser.cpp
    # src/ConsoleWidget.cpp
    # src/ViewportWidget.cpp
    # src/UIApp.cpp
)

add_library(bugui_widgets ${BUGUI_LIB_TYPE} ${SOURCES})

set_target_properties(bugui_widgets PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(bugui_widgets PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/vendor/stb
)

# stb implementations resolved at link time via core (stb_impl.cpp)

target_precompile_headers(bugui_widgets PRIVATE src/pch.hpp)

# Consumers that call target_link_libraries(... bugui_widgets) automatically
# inherit this PCH, avoiding repeated parsing of the heavy widget headers.
# Override with target_precompile_headers(myapp REUSE_FROM bugui_widgets) for
# maximum sharing when compile flags match.
target_precompile_headers(bugui_widgets INTERFACE
    include/WidgetApp.hpp
    include/BuGUI_light.hpp
)
