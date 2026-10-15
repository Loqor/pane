file(GLOB PANE_CORE_SRC CONFIGURE_DEPENDS core/*.cpp core/*.h)
add_library(pane_core STATIC ${PANE_CORE_SRC})
target_include_directories(pane_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pane_core PUBLIC Qt6::Core Qt6::Gui PkgConfig::LZ4)

file(GLOB PANE_RENDER_SRC CONFIGURE_DEPENDS render/*.cpp render/*.h)
add_library(pane_render STATIC ${PANE_RENDER_SRC})
target_link_libraries(pane_render PUBLIC pane_core Qt6::Concurrent)

file(GLOB PANE_IO_SRC CONFIGURE_DEPENDS io/*.cpp io/*.h)
add_library(pane_io STATIC ${PANE_IO_SRC})
target_link_libraries(pane_io PUBLIC pane_core PkgConfig::PNG PkgConfig::JPEG PkgConfig::ZLIB PkgConfig::WEBP PkgConfig::AVIF
                      PkgConfig::HEIF PkgConfig::TIFF ${GIF_LIBRARY})
target_include_directories(pane_io PRIVATE ${GIF_INCLUDE_DIR})

file(GLOB PANE_TOOLS_SRC CONFIGURE_DEPENDS tools/*.cpp tools/*.h)
add_library(pane_tools STATIC ${PANE_TOOLS_SRC})
target_link_libraries(pane_tools PUBLIC pane_render)

file(GLOB PANE_EFFECTS_SRC CONFIGURE_DEPENDS effects/*.cpp effects/*.h)
add_library(pane_effects STATIC ${PANE_EFFECTS_SRC})
target_link_libraries(pane_effects PUBLIC pane_render)

file(GLOB_RECURSE PANE_UI_SRC CONFIGURE_DEPENDS ui/*.cpp ui/*.h app/*.cpp app/*.h)
list(FILTER PANE_UI_SRC EXCLUDE REGEX "app/main\\.cpp$")
add_library(pane_ui STATIC ${PANE_UI_SRC} ${CMAKE_SOURCE_DIR}/resources/pane.qrc)
target_link_libraries(pane_ui PUBLIC pane_tools pane_effects pane_io Qt6::Widgets Qt6::PrintSupport)

add_executable(pane app/main.cpp)
target_link_libraries(pane PRIVATE pane_ui)
install(TARGETS pane RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/resources/pane.desktop DESTINATION share/applications)
