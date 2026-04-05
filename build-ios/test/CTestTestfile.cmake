# CMake generated Testfile for 
# Source directory: /Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test
# Build directory: /Users/danielsinclair/vscode/escli.refac7/vehicle-sim/build-ios/test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[vehicle-sim-tests]=] "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/build-ios/test/Debug/vehicle-sim-tests.app/Contents/MacOS/vehicle-sim-tests")
  set_tests_properties([=[vehicle-sim-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;20;add_test;/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[vehicle-sim-tests]=] "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/build-ios/test/Release/vehicle-sim-tests.app/Contents/MacOS/vehicle-sim-tests")
  set_tests_properties([=[vehicle-sim-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;20;add_test;/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[vehicle-sim-tests]=] "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/build-ios/test/MinSizeRel/vehicle-sim-tests.app/Contents/MacOS/vehicle-sim-tests")
  set_tests_properties([=[vehicle-sim-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;20;add_test;/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[vehicle-sim-tests]=] "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/build-ios/test/RelWithDebInfo/vehicle-sim-tests.app/Contents/MacOS/vehicle-sim-tests")
  set_tests_properties([=[vehicle-sim-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;20;add_test;/Users/danielsinclair/vscode/escli.refac7/vehicle-sim/test/CMakeLists.txt;0;")
else()
  add_test([=[vehicle-sim-tests]=] NOT_AVAILABLE)
endif()
