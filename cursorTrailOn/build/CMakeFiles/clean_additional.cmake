# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "src/CMakeFiles/cursortrailon_autogen.dir/AutogenUsed.txt"
  "src/CMakeFiles/cursortrailon_autogen.dir/ParseCache.txt"
  "src/CMakeFiles/kwin_cursortrailon_config_autogen.dir/AutogenUsed.txt"
  "src/CMakeFiles/kwin_cursortrailon_config_autogen.dir/ParseCache.txt"
  "src/cursortrailon_autogen"
  "src/kwin_cursortrailon_config_autogen"
  )
endif()
