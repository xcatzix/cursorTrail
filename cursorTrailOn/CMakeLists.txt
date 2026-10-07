cmake_minimum_required(VERSION 3.20)

project(cursortrailon VERSION 1.3.0 LANGUAGES CXX)

# --- Pin Qt 6 (must happen BEFORE the first find_package() call) -------------
# Qt6QmlMacros.cmake only creates the versionless qt_* commands when
# QT_DEFAULT_MAJOR_VERSION is 6; otherwise it aborts with:
#   qt_generate_foreign_qml_types() is only available in Qt 6.
# If this variable is not set, the FIRST Qt that CMake finds defines it, so on
# systems that ship Qt 5 next to Qt 6 the default can silently become 5.
set(QT_MAJOR_VERSION 6)
set(QT_DEFAULT_MAJOR_VERSION 6)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt5 ON)
# ---------------------------------------------------------------------------

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "cursorTrailOn currently supports Linux only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)
include(GNUInstallDirs)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# CoreTools gives us the Qt6::qtpaths / Qt6::qmake imported targets below.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Qml)
find_package(KF6 REQUIRED COMPONENTS CoreAddons Config KCMUtils)
find_package(KWin 6.7 REQUIRED)
find_package(epoxy REQUIRED)

# Native KWin effects must be installed where this Qt 6 build searches
# for plugins. This also avoids distro-specific paths such as lib vs lib64.
#
# "qtpaths6" is not installed by every distro (Arch ships it in qt6-tools,
# some minimal Fedora installs do not pull it in either), so instead of a hard
# REQUIRED find_program() we try several sources, best first:
#   1. the Qt6::qtpaths imported target   (no binary on PATH needed, Qt >= 6.4)
#   2. qtpaths6 / qtpaths-qt6 / qtpaths   (classic PATH lookup)
#   3. Qt6::qmake -query QT_INSTALL_PLUGINS
#   4. derive <libdir>/qt6/plugins from the location of libQt6Core
#   5. ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/plugins
function(cursortrailon_detect_qt_plugin_dir out_var)
    set(_dir "")

    # --- 1. Qt6::qtpaths imported target -------------------------------------
    if(TARGET Qt6::qtpaths)
        get_target_property(_qtpaths Qt6::qtpaths IMPORTED_LOCATION)
        if(NOT _qtpaths)
            get_target_property(_qtpaths Qt6::qtpaths LOCATION)
        endif()
        if(_qtpaths AND EXISTS "${_qtpaths}")
            foreach(_opt --plugin-dir --plugins-dir)
                execute_process(
                    COMMAND "${_qtpaths}" ${_opt}
                    OUTPUT_VARIABLE _out
                    ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                if(NOT _out STREQUAL "" AND IS_ABSOLUTE "${_out}")
                    set(_dir "${_out}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    # --- 2. qtpaths executable on PATH ----------------------------------------
    if(NOT _dir)
        find_program(CURSORTRAILON_QTPATHS_BIN NAMES qtpaths6 qtpaths-qt6 qtpaths)
        if(CURSORTRAILON_QTPATHS_BIN)
            foreach(_opt --plugin-dir --plugins-dir)
                execute_process(
                    COMMAND "${CURSORTRAILON_QTPATHS_BIN}" ${_opt}
                    OUTPUT_VARIABLE _out
                    ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                if(NOT _out STREQUAL "" AND IS_ABSOLUTE "${_out}")
                    set(_dir "${_out}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    # --- 3. qmake6 -query QT_INSTALL_PLUGINS ----------------------------------
    if(NOT _dir)
        set(_qmake "")
        if(TARGET Qt6::qmake)
            get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
            if(NOT _qmake)
                get_target_property(_qmake Qt6::qmake LOCATION)
            endif()
        endif()
        if(NOT _qmake)
            find_program(_qmake NAMES qmake6 qmake-qt6)
        endif()
        if(_qmake AND EXISTS "${_qmake}")
            execute_process(
                COMMAND "${_qmake}" -query QT_INSTALL_PLUGINS
                OUTPUT_VARIABLE _out
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(NOT _out STREQUAL "" AND IS_ABSOLUTE "${_out}")
                set(_dir "${_out}")
            endif()
        endif()
    endif()

    # --- 4. derive from the location of libQt6Core -----------------------------
    if(NOT _dir AND TARGET Qt6::Core)
        get_target_property(_core Qt6::Core IMPORTED_LOCATION)
        if(NOT _core)
            get_target_property(_core Qt6::Core LOCATION)
        endif()
        if(_core)
            get_filename_component(_libdir "${_core}" DIRECTORY)
            foreach(_candidate "${_libdir}/qt6/plugins" "${_libdir}/plugins")
                if(IS_DIRECTORY "${_candidate}")
                    set(_dir "${_candidate}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    # --- 5. last resort: <prefix>/<libdir>/qt6/plugins -------------------------
    if(NOT _dir)
        set(_dir "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/plugins")
    endif()

    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

# Allow an explicit override: -DCURSORTRAILON_QT_PLUGIN_DIR=/usr/lib/qt6/plugins
if(NOT CURSORTRAILON_QT_PLUGIN_DIR)
    cursortrailon_detect_qt_plugin_dir(CURSORTRAILON_QT_PLUGIN_DIR)
endif()

if(NOT IS_ABSOLUTE "${CURSORTRAILON_QT_PLUGIN_DIR}")
    message(FATAL_ERROR "Could not determine an absolute Qt 6 plugin directory "
                        "(got: ${CURSORTRAILON_QT_PLUGIN_DIR}). "
                        "Pass -DCURSORTRAILON_QT_PLUGIN_DIR=/usr/lib/qt6/plugins manually.")
endif()

set(KDE_INSTALL_PLUGINDIR "${CURSORTRAILON_QT_PLUGIN_DIR}" CACHE PATH
    "Qt 6 plugin directory used by KWin" FORCE)

# --- Text caret support ------------------------------------------------------
# The caret trail reads the caret rectangle that Wayland clients report through
# the text-input protocol, using KWin's internal SeatInterface / TextInput*
# classes. Those headers are only usable if kwin-devel installs them, so detect
# them here instead of failing the whole build.
#   cmake -DCURSORTRAILON_TEXT_CURSOR=OFF ...   builds the mouse trail only
option(CURSORTRAILON_TEXT_CURSOR
       "Build text caret trail support (needs KWin Wayland headers)" ON)

set(CURSORTRAILON_TEXT_INPUT_ENABLED OFF)
if(CURSORTRAILON_TEXT_CURSOR)
    get_target_property(_kwin_includes KWin::kwin INTERFACE_INCLUDE_DIRECTORIES)
    set(_kwin_header_dir "")
    foreach(_dir IN LISTS _kwin_includes)
        if(EXISTS "${_dir}/wayland_server.h"
           AND EXISTS "${_dir}/wayland/seat.h"
           AND EXISTS "${_dir}/wayland/surface.h")
            set(_kwin_header_dir "${_dir}")
            break()
        endif()
    endforeach()

    if(_kwin_header_dir)
        foreach(_v 1 2 3)
            if(EXISTS "${_kwin_header_dir}/wayland/textinput_v${_v}.h")
                set(CURSORTRAILON_HAVE_TI_V${_v} ON)
                set(CURSORTRAILON_TEXT_INPUT_ENABLED ON)
            endif()
        endforeach()
    endif()

    if(NOT CURSORTRAILON_TEXT_INPUT_ENABLED)
        message(WARNING
            "KWin's Wayland text-input headers were not found "
            "(searched: ${_kwin_includes}). Building WITHOUT text caret "
            "trail support; the mouse trail is unaffected.")
    endif()
endif()
set(CURSORTRAILON_TEXT_INPUT_ENABLED "${CURSORTRAILON_TEXT_INPUT_ENABLED}"
    CACHE INTERNAL "cursorTrailOn: text caret support compiled in")

add_subdirectory(src)

message(STATUS "cursorTrailOn version: ${PROJECT_VERSION}")
message(STATUS "Qt plugin directory: ${CURSORTRAILON_QT_PLUGIN_DIR}")
message(STATUS "Text caret trail: ${CURSORTRAILON_TEXT_INPUT_ENABLED}")

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
