cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(LilyGoUI CONFIG REQUIRED)

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

add_library(lilygo_ui_xiaozhi_core STATIC
    src/pages/assistant/assistant_model.cpp)
target_include_directories(lilygo_ui_xiaozhi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_xiaozhi_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_xiaozhi_core PUBLIC cxx_std_17)

add_library(lilygo_ui_xiaozhi_ui STATIC
    src/app.cpp
    src/components/components.cpp
    src/pages/assistant/assistant_view.cpp
    src/pages/assistant/assistant_view_model.cpp
    src/pages/assistant/hardware_mic_input.cpp
    src/pages/assistant/session_controller.cpp)
target_include_directories(lilygo_ui_xiaozhi_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_xiaozhi_ui PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_xiaozhi_ui PUBLIC
    lilygo_ui_xiaozhi_core
    LilyGoUI::Runtime)

add_executable(lilygo_ui_xiaozhi src/main.cpp)
target_compile_options(lilygo_ui_xiaozhi PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_xiaozhi PRIVATE lilygo_ui_xiaozhi_ui)
set_target_properties(lilygo_ui_xiaozhi PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

set(LILYGO_XIAOZHI_BUNDLED_HELPER "ON" CACHE STRING
    "Build the bundled protocol helper: AUTO, ON, or OFF")
set_property(CACHE LILYGO_XIAOZHI_BUNDLED_HELPER PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${LILYGO_XIAOZHI_BUNDLED_HELPER}" _xiaozhi_helper_mode)
if(NOT _xiaozhi_helper_mode MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR
        "LILYGO_XIAOZHI_BUNDLED_HELPER must be AUTO, ON, or OFF")
endif()

if(CM0_SIMULATOR)
    find_package(OpenSSL REQUIRED)
    pkg_check_modules(XIAOZHI_OPUS REQUIRED IMPORTED_TARGET opus)
    add_executable(lilygo_ui_xiaozhi_helper src/protocol/xiaozhi_helper.c)
    target_compile_features(lilygo_ui_xiaozhi_helper PRIVATE c_std_99)
    target_compile_options(lilygo_ui_xiaozhi_helper PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(lilygo_ui_xiaozhi_helper PRIVATE XIAOZHI_SDL_AUDIO=1)
    target_link_libraries(lilygo_ui_xiaozhi_helper PRIVATE
        OpenSSL::SSL OpenSSL::Crypto PkgConfig::XIAOZHI_OPUS PkgConfig::SDL2)
    set_target_properties(lilygo_ui_xiaozhi_helper PROPERTIES
        OUTPUT_NAME "lilygo-ui-xiaozhi-helper")
    target_compile_definitions(lilygo_ui_xiaozhi_ui PRIVATE
        LILYGO_XIAOZHI_PROJECT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.xiaozhi"
        LILYGO_XIAOZHI_HOST_HELPER="$<TARGET_FILE:lilygo_ui_xiaozhi_helper>")
    add_dependencies(lilygo_ui_xiaozhi_ui lilygo_ui_xiaozhi_helper)
elseif(NOT _xiaozhi_helper_mode STREQUAL "OFF")
    find_path(XIAOZHI_ALSA_INCLUDE_DIR NAMES alsa/asoundlib.h)
    find_path(XIAOZHI_OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h)
    find_path(XIAOZHI_OPUS_INCLUDE_DIR NAMES opus.h PATH_SUFFIXES opus)
    find_library(XIAOZHI_ALSA_LIBRARY NAMES asound)
    find_library(XIAOZHI_CRYPTO_LIBRARY NAMES crypto)
    find_library(XIAOZHI_OPENSSL_LIBRARY NAMES ssl)
    find_library(XIAOZHI_OPUS_LIBRARY NAMES opus)

    set(_xiaozhi_helper_missing)
    foreach(_dependency IN ITEMS
            ALSA_INCLUDE_DIR OPENSSL_INCLUDE_DIR OPUS_INCLUDE_DIR
            ALSA_LIBRARY CRYPTO_LIBRARY OPENSSL_LIBRARY OPUS_LIBRARY)
        if(NOT XIAOZHI_${_dependency})
            list(APPEND _xiaozhi_helper_missing "XIAOZHI_${_dependency}")
        endif()
    endforeach()

    list(JOIN _xiaozhi_helper_missing ", " _xiaozhi_helper_missing_text)
    if(NOT _xiaozhi_helper_missing)
        find_package(Threads REQUIRED)
        add_executable(lilygo_ui_xiaozhi_helper
            src/protocol/xiaozhi_helper.c)
        target_compile_features(lilygo_ui_xiaozhi_helper PRIVATE c_std_99)
        target_compile_options(lilygo_ui_xiaozhi_helper PRIVATE
            -Wall -Wextra -Wpedantic)
        target_include_directories(lilygo_ui_xiaozhi_helper PRIVATE
            ${XIAOZHI_ALSA_INCLUDE_DIR}
            ${XIAOZHI_OPENSSL_INCLUDE_DIR}
            ${XIAOZHI_OPUS_INCLUDE_DIR})
        target_link_libraries(lilygo_ui_xiaozhi_helper PRIVATE
            ${XIAOZHI_OPENSSL_LIBRARY}
            ${XIAOZHI_CRYPTO_LIBRARY}
            ${XIAOZHI_ALSA_LIBRARY}
            ${XIAOZHI_OPUS_LIBRARY}
            Threads::Threads)
        set_target_properties(lilygo_ui_xiaozhi_helper PROPERTIES
            OUTPUT_NAME "lilygo-ui-xiaozhi-helper")
        message(STATUS "Building bundled Xiaozhi protocol helper")
    elseif(_xiaozhi_helper_mode STREQUAL "ON")
        message(FATAL_ERROR
            "Bundled Xiaozhi helper requested but dependencies are missing: "
            "${_xiaozhi_helper_missing_text}\n"
            "For the CM0 BSP SDK, run python3 scripts/prepare-helper-sdk.py "
            "from the project root, then configure again.")
    else()
        message(WARNING
            "Bundled Xiaozhi helper is unavailable "
            "(${_xiaozhi_helper_missing_text}). "
            "You must provide a compatible helper through LILYGO_XIAOZHI_HELPER "
            "on the target. CM0 does not include the legacy K230 firmware helper.")
    endif()
elseif(NOT CM0_SIMULATOR)
    message(STATUS "Bundled Xiaozhi protocol helper disabled")
endif()

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_xiaozhi
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
if(TARGET lilygo_ui_xiaozhi_helper)
    install(TARGETS lilygo_ui_xiaozhi_helper
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
        COMPONENT app)
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    if(CM0_SIMULATOR)
        add_executable(xiaozhi-audio-capture-tests tests/test_audio_capture.c)
        target_compile_features(xiaozhi-audio-capture-tests PRIVATE c_std_99)
        target_compile_options(xiaozhi-audio-capture-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        target_compile_definitions(xiaozhi-audio-capture-tests PRIVATE XIAOZHI_SDL_AUDIO=1)
        target_link_libraries(xiaozhi-audio-capture-tests PRIVATE
            OpenSSL::SSL OpenSSL::Crypto PkgConfig::XIAOZHI_OPUS PkgConfig::SDL2)
        add_test(NAME xiaozhi-audio-capture COMMAND xiaozhi-audio-capture-tests)

        add_executable(xiaozhi-helper-identity-tests tests/test_helper_identity.c)
        target_compile_features(xiaozhi-helper-identity-tests PRIVATE c_std_99)
        target_compile_options(xiaozhi-helper-identity-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        target_compile_definitions(xiaozhi-helper-identity-tests PRIVATE XIAOZHI_SDL_AUDIO=1)
        target_link_libraries(xiaozhi-helper-identity-tests PRIVATE
            OpenSSL::SSL OpenSSL::Crypto PkgConfig::XIAOZHI_OPUS PkgConfig::SDL2)
        add_test(NAME xiaozhi-helper-identity COMMAND xiaozhi-helper-identity-tests)

        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME xiaozhi-protocol-session COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol_session.py
            $<TARGET_FILE:lilygo_ui_xiaozhi_helper>)
        set_tests_properties(xiaozhi-protocol-session PROPERTIES TIMEOUT 45)
    endif()

    add_executable(xiaozhi-core-tests tests/test_core.cpp)
    target_compile_options(xiaozhi-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(xiaozhi-core-tests PRIVATE
        lilygo_ui_xiaozhi_core)
    add_test(NAME xiaozhi-core COMMAND xiaozhi-core-tests)

    add_executable(xiaozhi-view-model-tests tests/test_view_model.cpp)
    target_compile_options(xiaozhi-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(xiaozhi-view-model-tests PRIVATE
        lilygo_ui_xiaozhi_ui)
    add_test(NAME xiaozhi-view-model COMMAND xiaozhi-view-model-tests)

    add_executable(xiaozhi-session-tests tests/test_session_controller.cpp)
    target_compile_options(xiaozhi-session-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(xiaozhi-session-tests PRIVATE lilygo_ui_xiaozhi_ui)
    add_test(NAME xiaozhi-session COMMAND xiaozhi-session-tests)

    add_executable(xiaozhi-render-ui tests/render_ui.cpp)
    target_compile_options(xiaozhi-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(xiaozhi-render-ui PRIVATE
        lilygo_ui_xiaozhi_ui)
    foreach(viewport IN ITEMS portrait landscape embedded)
        set(_parent_args)
        if(viewport STREQUAL "landscape")
            set(_width 1232)
            set(_height 568)
        else()
            set(_width 568)
            set(_height 1232)
        endif()
        if(viewport STREQUAL "embedded")
            set(_parent_args embedded)
        endif()
        add_test(NAME xiaozhi-render-${viewport} COMMAND xiaozhi-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/xiaozhi-${viewport}.ppm default ${_parent_args})
        add_test(NAME xiaozhi-render-history-${viewport} COMMAND xiaozhi-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/xiaozhi-history-${viewport}.ppm
            history ${_parent_args})
        add_test(NAME xiaozhi-render-binding-${viewport} COMMAND xiaozhi-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/xiaozhi-binding-${viewport}.ppm binding ${_parent_args})
        set_tests_properties(xiaozhi-render-binding-${viewport} PROPERTIES ENVIRONMENT
            "LILYGO_XIAOZHI_MOCK=0;LILYGO_XIAOZHI_HELPER=$<TARGET_FILE:xiaozhi-session-tests>;LILYGO_XIAOZHI_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/binding-${viewport}")
        set_tests_properties(xiaozhi-render-${viewport}
            xiaozhi-render-history-${viewport} PROPERTIES
            ENVIRONMENT "LILYGO_XIAOZHI_MOCK=1")
    endforeach()
    # UI snapshots exercise offline conversations explicitly. Interactive startup
    # uses real activation and never inherits this test-only environment.
    set_tests_properties(xiaozhi-view-model PROPERTIES
        ENVIRONMENT "LILYGO_XIAOZHI_MOCK=1")
endif()

# New applications have no legacy CM0 package to replace. Add Debian migration
# fields only when this project supersedes an actually published package.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(_xiaozhi_package_dependencies
    "libc6, libstdc++6, libgcc-s1, libdrm2, libfreetype6, libpng16-16t64, zlib1g, libbz2-1.0, libbrotli1, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
if(TARGET lilygo_ui_xiaozhi_helper)
    string(APPEND _xiaozhi_package_dependencies
        ", libssl3, libasound2, libopus0, ca-certificates")
endif()
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS "${_xiaozhi_package_dependencies}")
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
