# Copyright (c) 2026 CCP Games
#
# The Invoke hook's own tests (carbon-bind). Linux-capable and independent of BUILD_TESTING, which
# must stay OFF on Linux (tests/CMakeLists.txt refuses it). Configure with -DBLUE_INVOKE_TESTS=ON,
# with a build directory outside this source tree.
#
# The same test sources build three DSOs:
#   invoke_test_classes   hooked, module "invoke_test_classes"
#   invoke_test_unhooked  BLUE_INVOKE_DISABLE=1: must register nothing
#   invoke_test_otherid   BLUE_INVOKE_BUILD_ID="other": tables must carry a different build ID
# invoke_tests initialises Python (exposure runs at static init and needs it), dlopen()s them and
# checks everything through the C API.

foreach(_variant classes unhooked otherid)
    set(_t invoke_test_${_variant})
    add_library(${_t} SHARED InvokeTestClasses.cpp InvokeTestClasses.h)
    target_compile_definitions(${_t} PRIVATE BLUEBUILD INVOKE_TEST_MODULE="${_t}")
    target_link_libraries(${_t} PRIVATE BlueExposure Python3::Python)
    set_target_properties(${_t} PROPERTIES FOLDER "Tests/Invoke" CXX_VISIBILITY_PRESET hidden)
endforeach()
target_compile_definitions(invoke_test_unhooked PRIVATE BLUE_INVOKE_DISABLE=1)
target_compile_definitions(invoke_test_otherid PRIVATE BLUE_INVOKE_BUILD_ID="other")

# The embedded interpreter's home: the tree that provides libpython (never a system Python).
if(NOT BLUE_INVOKE_TEST_PYTHON_HOME)
    list(GET Python3_LIBRARY_DIRS 0 _py_libdir)
    get_filename_component(BLUE_INVOKE_TEST_PYTHON_HOME "${_py_libdir}" DIRECTORY)
endif()
message(STATUS "BlueInvoke tests: Python home ${BLUE_INVOKE_TEST_PYTHON_HOME}")

add_executable(invoke_tests invoke_tests.cpp)
target_compile_definitions(invoke_tests PRIVATE
    BLUE_WITH_PYTHON=1
    INVOKE_TEST_LIB_CLASSES="$<TARGET_FILE:invoke_test_classes>"
    INVOKE_TEST_LIB_UNHOOKED="$<TARGET_FILE:invoke_test_unhooked>"
    INVOKE_TEST_LIB_OTHERID="$<TARGET_FILE:invoke_test_otherid>"
    INVOKE_TEST_PYTHON_HOME="${BLUE_INVOKE_TEST_PYTHON_HOME}"
)
# Headers only from BlueExposure/CcpCore (IRoot's interface); the driver links no Blue code.
# blue_invoke's directories first: CcpCore's may be an installed prefix holding an older
# BlueInvokeConfig.h, which would otherwise shadow the generated one.
target_include_directories(invoke_tests PRIVATE
    $<TARGET_PROPERTY:blue_invoke,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:BlueExposure,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:CcpCore,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(invoke_tests PRIVATE blue_invoke Python3::Python ${CMAKE_DL_LIBS})
target_link_options(invoke_tests PRIVATE "LINKER:-rpath-link,${BLUE_INVOKE_TEST_PYTHON_HOME}/lib")
add_dependencies(invoke_tests invoke_test_classes invoke_test_unhooked invoke_test_otherid)
set_target_properties(invoke_tests PROPERTIES FOLDER "Tests/Invoke")

add_test(NAME invoke_tests COMMAND invoke_tests)
