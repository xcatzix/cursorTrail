kcoreaddons_add_plugin(cursortrailon
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

target_sources(cursortrailon PRIVATE
    main.cpp
    cursortrailon.cpp
    cursortrailon.h
)

# CMake's automoc scanner does not know this KWin macro by default.
set_property(TARGET cursortrailon APPEND PROPERTY
    AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY)

target_link_libraries(cursortrailon PRIVATE
    KF6::CoreAddons
    KF6::ConfigCore
    KWin::kwin
)

# Text caret support: the top-level CMakeLists.txt detects which of KWin's
# Wayland text-input headers are installed and sets these variables.
if(CURSORTRAILON_TEXT_INPUT_ENABLED)
    foreach(_v 1 2 3)
        if(CURSORTRAILON_HAVE_TI_V${_v})
            target_compile_definitions(cursortrailon PRIVATE
                CURSORTRAILON_HAVE_TI_V${_v})
        endif()
    endforeach()
endif()

kcoreaddons_add_plugin(kwin_cursortrailon_config
    INSTALL_NAMESPACE "kwin/effects/configs"
)

target_sources(kwin_cursortrailon_config PRIVATE
    cursortrailon_config.cpp
    cursortrailon_config.h
)

target_link_libraries(kwin_cursortrailon_config PRIVATE
    Qt6::DBus
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::KCMUtils
)
