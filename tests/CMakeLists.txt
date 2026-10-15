file(GLOB PANE_UNIT_TESTS CONFIGURE_DEPENDS unit/*.cpp io/*.cpp)
add_executable(pane_tests ${PANE_UNIT_TESTS})
target_link_libraries(pane_tests PRIVATE pane_io pane_tools pane_effects Catch2::Catch2WithMain)
target_compile_definitions(pane_tests PRIVATE PANE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

file(GLOB PANE_UI_TESTS CONFIGURE_DEPENDS ui/*.cpp)
if(PANE_UI_TESTS)
  add_executable(pane_ui_tests ${PANE_UI_TESTS})
  target_link_libraries(pane_ui_tests PRIVATE pane_ui Qt6::Test)
  add_test(NAME ui_smoke COMMAND pane_ui_tests)
  set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT
                       "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=0;HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home")
endif()

include(Catch)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test-home)
catch_discover_tests(pane_tests PROPERTIES ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home")

add_executable(pane_bench bench/bench_brush.cpp)
target_link_libraries(pane_bench PRIVATE pane_render)

add_executable(pane_bench_effects bench/bench_effects.cpp)
target_link_libraries(pane_bench_effects PRIVATE pane_effects)

add_executable(pane_bench_ops bench/bench_ops.cpp)
target_link_libraries(pane_bench_ops PRIVATE pane_ui)
