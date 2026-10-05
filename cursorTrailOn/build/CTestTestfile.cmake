# CMake generated Testfile for 
# Source directory: /home/giqorg/Share/UploadGithup/cursorTrailOn-Source-v1/cursorTrailOn
# Build directory: /home/giqorg/Share/UploadGithup/cursorTrailOn-Source-v1/cursorTrailOn/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[appstreamtest]=] "/usr/bin/cmake" "-DAPPSTREAMCLI=/usr/bin/appstreamcli" "-DINSTALL_FILES=/home/giqorg/Share/UploadGithup/cursorTrailOn-Source-v1/cursorTrailOn/build/install_manifest.txt" "-P" "/usr/share/ECM/kde-modules/appstreamtest.cmake")
set_tests_properties([=[appstreamtest]=] PROPERTIES  _BACKTRACE_TRIPLES "/usr/share/ECM/kde-modules/KDECMakeSettings.cmake;177;add_test;/usr/share/ECM/kde-modules/KDECMakeSettings.cmake;195;appstreamtest;/usr/share/ECM/kde-modules/KDECMakeSettings.cmake;0;;/home/giqorg/Share/UploadGithup/cursorTrailOn-Source-v1/cursorTrailOn/CMakeLists.txt;30;include;/home/giqorg/Share/UploadGithup/cursorTrailOn-Source-v1/cursorTrailOn/CMakeLists.txt;0;")
subdirs("src")
