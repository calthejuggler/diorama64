add_executable(sm64ar_harness main.mm)
target_compile_options(sm64ar_harness PRIVATE -fobjc-arc)
target_link_libraries(sm64ar_harness PRIVATE ar_harness sm64_adapter "-framework AppKit" "-framework MetalKit" "-framework QuartzCore")
set(SM64AR_HARNESS_DIR ${CMAKE_BINARY_DIR}/harness)
file(MAKE_DIRECTORY ${SM64AR_HARNESS_DIR}/bin)
# macOS resource lookup is <exe dir>/../Resources (lang/, mods/, ...), like the tests.
file(CREATE_LINK ${PROJECT_SOURCE_DIR}/build/_src/sm64/build/us_pc ${SM64AR_HARNESS_DIR}/Resources SYMBOLIC)
set_target_properties(sm64ar_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SM64AR_HARNESS_DIR}/bin)
