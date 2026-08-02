cmake_minimum_required(VERSION 3.24)

project(DKRPort VERSION 1.0.0 LANGUAGES CXX)

option(DKRPORT_BUILD_TESTS "Build DKR Port tests" ON)
option(DKRPORT_BUILD_NATIVE_UI "Build the SDL3/RmlUi native launcher" ON)
option(DKRPORT_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(DKRPORT_ENABLE_SANITIZERS "Enable Address/Undefined Behaviour sanitizers" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

set(DKRPORT_CORE_SOURCES
    src/app/Application.cpp
    src/core/Crc32.cpp
    src/core/FileUtil.cpp
    src/core/Logger.cpp
    src/core/Paths.cpp
    src/core/Rom.cpp
    src/core/ResourceSetup.cpp
    src/core/Sha1.cpp
    src/core/ZipWriter.cpp
    src/input/InputConfig.cpp
    src/game/BootSession.cpp
    src/renderer/f3ddkr/F3DDKRRegistry.cpp
    src/renderer/f3ddkr/RendererSelfTest.cpp
)

add_library(dkrport_core STATIC ${DKRPORT_CORE_SOURCES})
target_include_directories(dkrport_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
dkrport_set_warnings(dkrport_core)
dkrport_enable_sanitizers(dkrport_core)

set(DKRPORT_NATIVE_UI_SOURCES)
if(DKRPORT_BUILD_NATIVE_UI)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(RmlUi CONFIG REQUIRED)

    if(TARGET RmlUi::RmlUi)
        set(DKRPORT_RMLUI_TARGET RmlUi::RmlUi)
    elseif(TARGET RmlUi::Core)
        set(DKRPORT_RMLUI_TARGET RmlUi::Core)
    else()
        message(FATAL_ERROR "RmlUi was found, but no supported CMake target was exported. Expected RmlUi::RmlUi or RmlUi::Core.")
    endif()

    set(DKRPORT_NATIVE_UI_SOURCES
        src/ui/NativeLauncher.cpp
        src/ui/RmlEventListener.cpp
        src/ui/RmlSdlRenderInterface.cpp
        src/ui/RmlSdlSystemInterface.cpp
        src/input/InputManager.cpp
    )

    add_library(dkrport_native_ui STATIC ${DKRPORT_NATIVE_UI_SOURCES})
    target_include_directories(dkrport_native_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    # These are PUBLIC because the native UI public headers expose SDL3 and
    # RmlUi types. Consumers must inherit their include directories and link
    # requirements, especially the executable containing SDL_main.h.
    target_link_libraries(dkrport_native_ui
        PUBLIC
            dkrport_core
            SDL3::SDL3
            ${DKRPORT_RMLUI_TARGET}
    )
    dkrport_set_warnings(dkrport_native_ui)
    dkrport_enable_sanitizers(dkrport_native_ui)
endif()

add_executable(DKRPort src/app/main.cpp)
if(WIN32 AND DKRPORT_BUILD_NATIVE_UI)
    set_target_properties(DKRPort PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_link_libraries(DKRPort PRIVATE dkrport_core)
if(DKRPORT_BUILD_NATIVE_UI)
    # main.cpp includes SDL3/SDL_main.h directly. Link SDL explicitly here so
    # the SDL header usage requirement is never hidden behind a static library.
    target_link_libraries(DKRPort PRIVATE dkrport_native_ui SDL3::SDL3)
    target_compile_definitions(DKRPort PRIVATE DKRPORT_HAS_NATIVE_UI=1)
else()
    target_compile_definitions(DKRPort PRIVATE DKRPORT_HAS_NATIVE_UI=0)
endif()
dkrport_set_warnings(DKRPort)
dkrport_enable_sanitizers(DKRPort)

if(MSVC)
    set_property(TARGET DKRPort dkrport_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if(TARGET dkrport_native_ui)
        set_property(TARGET dkrport_native_ui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

set_target_properties(DKRPort PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(DKRPort PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
    )
endforeach()

if(DKRPORT_BUILD_NATIVE_UI)
    add_custom_command(TARGET DKRPort POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui"
            "$<TARGET_FILE_DIR:DKRPort>/assets/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/portable.txt"
            "$<TARGET_FILE_DIR:DKRPort>/portable.txt"
        COMMENT "Copying native launcher assets"
    )
endif()

include(CTest)
if(DKRPORT_BUILD_TESTS)
    enable_testing()
    add_executable(dkrport_tests tests/test_main.cpp)
    target_link_libraries(dkrport_tests PRIVATE dkrport_core)
    dkrport_set_warnings(dkrport_tests)
    dkrport_enable_sanitizers(dkrport_tests)
    add_test(NAME dkrport_tests COMMAND dkrport_tests)
endif()

install(TARGETS DKRPort RUNTIME DESTINATION .)
install(FILES portable.txt DESTINATION . OPTIONAL)
if(DKRPORT_BUILD_NATIVE_UI)
    install(DIRECTORY assets/ui DESTINATION assets)
endif()
