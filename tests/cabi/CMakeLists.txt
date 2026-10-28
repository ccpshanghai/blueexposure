# Copyright (c) 2026 CCP Games
#
# BlueExposureCAbiTests: the test classes of tests/ called only through the C-ABI backend
# (ClassInfo::mCMethodTable, VarEntry::mCGet/mCSet, attribute offsets and the C function list).

find_package(GTest REQUIRED)
include(GoogleTest)

# BlueCAbi.h must compile as plain C; this object library is built as C11 and asks the C
# compiler for the layout the C++ side asserts
add_library(BlueExposureCAbiHeaderC OBJECT
    TestCAbiHeader.c
)
target_include_directories(BlueExposureCAbiHeaderC PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(BlueExposureCAbiHeaderC PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "Tests"
)

add_executable(BlueExposureCAbiTests
    main.cpp
    TestCAbiCalls.cpp
    TestCAbiMembers.cpp
    TestCAbiValues.cpp
    TestCAbiUtil.h
    ../TestAttributes.cpp
    ../TestAttributes_Blue.cpp
    ../TestCallbacks.cpp
    ../TestCallbacks_Blue.cpp
    ../TestFunctions.cpp
    ../TestMethods.cpp
    ../TestMethods_Blue.cpp
    ../TestMultipleInterfaces.cpp
    ../TestMultipleInterfaces_Blue.cpp
    ../TestOptionalArgs.cpp
    ../TestOptionalArgs_Blue.cpp
    ../TestProperties.cpp
    ../TestProperties_Blue.cpp
    ../TestReferences.cpp
    ../TestReferences_Blue.cpp
    ../TestSuperclassAttributes_Blue.cpp
    ../TestSuperclassMethods_Blue.cpp
    $<TARGET_OBJECTS:BlueExposureCAbiHeaderC>
)

# The test class sources rely on these being precompiled, as in tests/CMakeLists.txt
target_precompile_headers(BlueExposureCAbiTests
    PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<BlueExposure.h>>"
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../BoolBeResult.h>"
)
target_include_directories(BlueExposureCAbiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(BlueExposureCAbiTests PRIVATE BLUEBUILD)
target_link_libraries(BlueExposureCAbiTests PRIVATE BlueExposure GTest::gtest)

set_target_properties(BlueExposureCAbiTests PROPERTIES FOLDER "Tests")

if(CMAKE_IMPORT_LIBRARY_SUFFIX)
    add_custom_command(TARGET BlueExposureCAbiTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:BlueExposureCAbiTests> $<TARGET_FILE_DIR:BlueExposureCAbiTests>
            COMMAND_EXPAND_LISTS
            VERBATIM
    )
endif()

gtest_discover_tests(BlueExposureCAbiTests)
