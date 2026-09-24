# CMake generated Testfile for 
# Source directory: /Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests
# Build directory: /Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/core/libcorrect/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(convolutional_test "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests/convolutional_test_runner")
set_tests_properties(convolutional_test PROPERTIES  WORKING_DIRECTORY "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests" _BACKTRACE_TRIPLES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;7;add_test;/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;0;")
add_test(convolutional_shim_test "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests/convolutional_shim_test_runner")
set_tests_properties(convolutional_shim_test PROPERTIES  WORKING_DIRECTORY "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests" _BACKTRACE_TRIPLES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;29;add_test;/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;0;")
add_test(reed_solomon_test "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests/reed_solomon_test_runner")
set_tests_properties(reed_solomon_test PROPERTIES  WORKING_DIRECTORY "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests" _BACKTRACE_TRIPLES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;35;add_test;/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;0;")
add_test(reed_solomon_shim_interop_test "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests/reed_solomon_shim_interop_test_runner")
set_tests_properties(reed_solomon_shim_interop_test PROPERTIES  WORKING_DIRECTORY "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/tests" _BACKTRACE_TRIPLES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;49;add_test;/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/core/libcorrect/tests/CMakeLists.txt;0;")
