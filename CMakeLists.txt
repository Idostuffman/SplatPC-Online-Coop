cmake_minimum_required(VERSION 3.18)
project(SplatPCOnlineCoop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()

add_library(online_coop_core STATIC
    src/mod_settings.cpp
    src/netplay.cpp
    src/netplay_connect.cpp
    src/netplay_video.cpp
    src/coop_hooks.cpp
    src/online_menu.cpp
)

target_include_directories(online_coop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(online_coop_core PUBLIC HAVE_MINIAUDIO)

if(TARGET SDL2::SDL2-static)
    target_link_libraries(online_coop_core PUBLIC SDL2::SDL2-static)
    target_compile_definitions(online_coop_core PUBLIC HAVE_SDL2)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(online_coop_core PUBLIC SDL2::SDL2)
    target_compile_definitions(online_coop_core PUBLIC HAVE_SDL2)
else()
    message(WARNING "SDL2 not found; guest viewer (--join) video/controller loop will be disabled.")
endif()

if(WIN32)
    target_link_libraries(online_coop_core PUBLIC
        ws2_32
        winmm
        mfplat
        mfuuid
        shell32
        ole32
        oleaut32
        ksuser
    )
    if(MSVC)
        target_compile_options(online_coop_core PUBLIC /utf-8 /EHsc)
    endif()
else()
    target_compile_options(online_coop_core PRIVATE -fPIC)
    find_package(Threads REQUIRED)
    target_link_libraries(online_coop_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    find_library(ALSA_LIB asound)
    if(ALSA_LIB)
        target_link_libraries(online_coop_core PUBLIC ${ALSA_LIB})
    endif()
    find_library(MATH_LIB m)
    if(MATH_LIB)
        target_link_libraries(online_coop_core PUBLIC ${MATH_LIB})
    endif()
endif()

add_library(online_coop MODULE src/mod_entry.cpp)
target_link_libraries(online_coop PRIVATE online_coop_core)
set_target_properties(online_coop PROPERTIES PREFIX "")
if(MINGW)
    target_link_options(online_coop PRIVATE -static)
elseif(MSVC)
    target_compile_options(online_coop PRIVATE /utf-8)
endif()

option(ONLINE_COOP_BUILD_TESTS "Build Online Co-op unit and mod API tests" ON)
if(ONLINE_COOP_BUILD_TESTS)
    enable_testing()

    add_executable(online_coop_tests tests/test_netplay.cpp)
    target_link_libraries(online_coop_tests PRIVATE online_coop_core)
    if(MSVC)
        target_compile_options(online_coop_tests PRIVATE /utf-8)
    endif()
    add_test(NAME online_coop_netplay COMMAND online_coop_tests)

    add_executable(online_coop_mod_test tests/test_mod_integration.cpp)
    target_include_directories(online_coop_mod_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(NOT WIN32)
        target_link_libraries(online_coop_mod_test PRIVATE ${CMAKE_DL_LIBS})
    elseif(MSVC)
        target_compile_options(online_coop_mod_test PRIVATE /utf-8)
    endif()
    add_test(NAME online_coop_mod_integration COMMAND online_coop_mod_test $<TARGET_FILE:online_coop>)
endif()
