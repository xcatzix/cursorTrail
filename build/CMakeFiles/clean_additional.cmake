# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "src/CMakeFiles/kwin_proxyx_windtrail_config_autogen.dir/AutogenUsed.txt"
  "src/CMakeFiles/kwin_proxyx_windtrail_config_autogen.dir/ParseCache.txt"
  "src/CMakeFiles/proxyx_windtrail_autogen.dir/AutogenUsed.txt"
  "src/CMakeFiles/proxyx_windtrail_autogen.dir/ParseCache.txt"
  "src/kwin_proxyx_windtrail_config_autogen"
  "src/proxyx_windtrail_autogen"
  )
endif()
