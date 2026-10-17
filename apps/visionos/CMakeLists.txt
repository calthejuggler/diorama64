set(SM64AR_TEAM_ID "" CACHE STRING "Apple development team ID for a device build (empty: unsigned; see README.md)")
set(SM64AR_BUNDLE_ID "com.local.sm64ar" CACHE STRING "Bundle identifier (a device build needs one unique to your team)")
add_executable(SM64AR MACOSX_BUNDLE SM64ARApp.swift ShellPoll.swift LookLoad.swift vos_app.mm)
set_source_files_properties(vos_app.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
set_target_properties(SM64AR PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${SM64AR_BUNDLE_ID}
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 7
  XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
  XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/SM64AR-Bridging-Header.h
  # The Xcode generator copies C compile options into OTHER_SWIFT_FLAGS and swiftc rejects them.
  XCODE_ATTRIBUTE_OTHER_SWIFT_FLAGS "")
if(SM64AR_TEAM_ID)
  set_target_properties(SM64AR PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${SM64AR_TEAM_ID}
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES)
else()
  set_target_properties(SM64AR PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
endif()
target_link_libraries(SM64AR PRIVATE ar_vos ar_cube_adapter sm64_adapter "-framework SwiftUI" "-framework CompositorServices"
                      "-framework ARKit" "-framework Metal" "-framework Foundation")
# The bundle is flat, so lang/, dynos/, palettes/, mods/ go to SM64AR.app/res and the adapter is told so
# (0002-Library-seams-and-host-hooks.patch).
set(SM64_RES_SRC ${PROJECT_SOURCE_DIR}/build/_src/sm64/build/us_pc)
foreach(dir lang dynos palettes mods)
  if(NOT IS_DIRECTORY ${SM64_RES_SRC}/${dir})
    message(FATAL_ERROR "Missing ${SM64_RES_SRC}/${dir}\nRun ./build.py (or python3 scripts/sm64_oracle.py) first.")
  endif()
  add_custom_command(TARGET SM64AR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:SM64AR>/res/${dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SM64_RES_SRC}/${dir} "$<TARGET_BUNDLE_DIR:SM64AR>/res/${dir}")
endforeach()
set(SM64AR_BUNDLED_DIR "" CACHE PATH "Staged data to bundle (set by visionos_build.py)")
add_custom_command(TARGET SM64AR POST_BUILD COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:SM64AR>/Bundled")
if(SM64AR_BUNDLED_DIR)
  add_custom_command(TARGET SM64AR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SM64AR_BUNDLED_DIR}" "$<TARGET_BUNDLE_DIR:SM64AR>/Bundled")
endif()
