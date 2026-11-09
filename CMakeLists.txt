# Copyright (c) 2026 CCP Games

cmake_minimum_required(VERSION 3.21)

project(BlueExposure VERSION 1.0.0)

include(cmake/CcpTargetConfigurations.cmake)
include(cmake/CcpDocsGenerator.cmake)
include(cmake/CcpPackageConfigHelpers.cmake)
include(cmake/CcpBuildConfigurations.cmake)

find_package(carbon-core CONFIG REQUIRED)
find_package(Python3 COMPONENTS Development Interpreter REQUIRED)

ccp_add_library(BlueExposure STATIC
    BlueClasses.cpp
    BlueClasses.h
    BlueClasses_Blue.cpp
    BlueExposure.cpp
    BlueExtractArgument.cpp
    BlueListUtil.cpp
    BlueMemberIterator.cpp
    BluePythonThunkers.cpp
    BluePythonThunkers.h
    BluePyWrap.cpp
    BluePyWrap.h
    BlueRegistration.cpp
    BlueRegistrationPython.cpp
    BlueScriptCallback.cpp
    BlueSharedString.cpp
    BlueStdResult.cpp
    BlueStructureList.cpp
    BlueTypes.cpp
    BlueUtil.cpp
    BlueVariable.cpp
    Copier.cpp
    Copier_Blue.cpp
    Find.cpp
    InterfaceDefinitions.cpp
    PythonKlass.cpp
    TypeInfo.cpp
    TypeInfo.h
)

set(PUBLIC_HEADER_FILES
    include/BlueCallFunction.h
    include/BlueCallFunctionHelpers.h
    include/BlueCallFunctionWithBeResult.h
    include/BlueClass.h
    include/BlueDict.h
    include/BlueExposure.h
    include/BlueExposureHelpers.h
    include/BlueExposureHelpersPython.h
    include/BlueExposureMacros.h
    include/BlueExposureMacrosDeprecated.h
    include/BlueExposureMacrosPython.h
    include/BlueExposureTypeSignature.h
    include/BlueExtractArgument.h
    include/BlueExtractArgumentPython.h
    include/BlueFunctionTraits.h
    include/BlueInvoke.h
    include/BlueInvokeName.h
    include/BlueListUtil.h
    include/BlueMemberFunctionAsCallable.h
    include/BlueMemberFunctionTraits.h
    include/BlueMemberIterator.h
    include/BluePythonObject.h
    include/BlueRegistration.h
    include/BlueScriptCallback.h
    include/BlueScriptValue.h
    include/BlueSharedString.h
    include/BlueSmartPtr.h
    include/BlueSmartPy.h
    include/BlueStdResult.h
    include/BlueStringTable.h
    include/BlueStructureList.h
    include/BlueTypes.h
    include/BlueTypeTraits.h
    include/BlueUtil.h
    include/BlueVariable.h
    include/BlueVectorTypes.h
    include/BlueWeakRef.h
    include/BlueWrapReturnValue.h
    include/BlueWrapReturnValuePython.h
    include/Copier.h
    include/Find.h
    include/IBlueClasses.h
    include/IBlueDict.h
    include/IBlueRtti.h
    include/IBlueStructureList.h
    include/ICopier.h
    include/ICustomPersist.h
    include/IInitialize.h
    include/IList.h
    include/INotify.h
    include/IPythonMethods.h
    include/IPythonNumeric.h
    include/PythonKlass.h
    include/StringBeResult.h
)

target_sources(BlueExposure PRIVATE ${PUBLIC_HEADER_FILES})

target_compile_definitions(BlueExposure
    PUBLIC
        BLUE_WITH_PYTHON
    PRIVATE
        BLUEBUILD
)

target_link_libraries(BlueExposure PUBLIC CcpCore Python3::Python)

target_include_directories(BlueExposure
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# --- Invoke hook (carbon-bind): libblue_invoke and the generated BlueInvokeConfig.h -----------
option(BLUE_INVOKE_DISABLE "Compile the Invoke hook statements in the exposure macros away" OFF)
option(BLUE_INVOKE_TESTS "Build the Linux-capable tests/invoke suite (independent of BUILD_TESTING)" OFF)

# Build ID = SHA-256 of the hook's ABI version (BLUE_INVOKE_ABI_VERSION in BlueInvoke.h), the hook's
# own files and the disable switch. No compiler name or version and no other header: the same
# commit gives the same ID on every host and compiler (carbon-bind's shared metadata compares it).
# Files are hashed with CR removed, so a CRLF checkout gives the same ID. Editing one of them
# re-runs configure, so the ID moves with every hook change even when the version is not bumped.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/BlueInvoke.h" _blue_invoke_abi REGEX "^#define BLUE_INVOKE_ABI_VERSION [0-9]+$")
string(REGEX REPLACE "^#define BLUE_INVOKE_ABI_VERSION ([0-9]+)$" "\\1" _blue_invoke_abi "${_blue_invoke_abi}")
if(NOT _blue_invoke_abi MATCHES "^[0-9]+$")
    message(FATAL_ERROR "BlueInvoke: no '#define BLUE_INVOKE_ABI_VERSION <n>' line in include/BlueInvoke.h")
endif()
set(_blue_invoke_id_text "abi=${_blue_invoke_abi}\n")
foreach(_f IN ITEMS include/BlueInvoke.h include/BlueInvokeConfig.h.in include/BlueInvokeName.h src/BlueInvoke.cpp)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_f}" _c)
    string(REPLACE "\r" "" _c "${_c}")
    string(SHA256 _h "${_c}")
    string(APPEND _blue_invoke_id_text "${_f}=${_h}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()
if(BLUE_INVOKE_DISABLE)
    set(BLUE_INVOKE_DISABLE_VALUE 1)
else()
    set(BLUE_INVOKE_DISABLE_VALUE 0)
endif()
string(APPEND _blue_invoke_id_text "disable=${BLUE_INVOKE_DISABLE_VALUE}\n")
string(SHA256 _blue_invoke_id "${_blue_invoke_id_text}")
string(SUBSTRING "${_blue_invoke_id}" 0 16 BLUE_INVOKE_BUILD_ID)
set(BLUE_INVOKE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/blue_invoke_generated")
configure_file(include/BlueInvokeConfig.h.in "${BLUE_INVOKE_GENERATED_DIR}/BlueInvokeConfig.h" @ONLY)
message(STATUS "BlueInvoke: build ID ${BLUE_INVOKE_BUILD_ID}, BLUE_INVOKE_DISABLE=${BLUE_INVOKE_DISABLE_VALUE}")

add_library(blue_invoke SHARED src/BlueInvoke.cpp include/BlueInvoke.h "${BLUE_INVOKE_GENERATED_DIR}/BlueInvokeConfig.h")
set_target_properties(blue_invoke PROPERTIES
    OUTPUT_NAME blue_invoke
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_compile_features(blue_invoke PUBLIC cxx_std_17)
target_include_directories(blue_invoke
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${BLUE_INVOKE_GENERATED_DIR}>
        $<INSTALL_INTERFACE:include>
)
# Every consumer DSO of BlueExposure gets a DT_NEEDED on libblue_invoke (one registry per process).
target_link_libraries(BlueExposure PUBLIC blue_invoke)

if(BLUE_INVOKE_TESTS)
    enable_testing()
    add_subdirectory(tests/invoke)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(BUILD_TESTING "Build tests. Enabled by default." ON)
    include(CTest)
    if(BUILD_TESTING)
	    add_subdirectory(tests)
    endif()

    option (INSTALL_TO_MONOLITH "Install this package to a perforce branch vendor folder - provided for backwards compatability" OFF)

    if (INSTALL_TO_MONOLITH)
        install(
                TARGETS BlueExposure blue_invoke
                EXPORT BlueExposureTargets
                CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
                ARCHIVE DESTINATION lib/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
                LIBRARY DESTINATION lib/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
                RUNTIME DESTINATION bin/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
        )
        # Install rule for available public headers
        install(FILES ${PUBLIC_HEADER_FILES} "${BLUE_INVOKE_GENERATED_DIR}/BlueInvokeConfig.h" DESTINATION include)

        configure_ccp_vendor_config_file(
                TARGET BlueExposure
                DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/BlueExposureConfig.cmake"
                DEPENDENCIES "CcpCore;Python"
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/BlueExposureConfig.cmake DESTINATION .)
    else() # VCPKG install mode
        # Install static library archive and the shared Invoke registry
        install(
                TARGETS BlueExposure blue_invoke
                EXPORT BlueExposureTargets
                CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
                ARCHIVE DESTINATION lib/
                LIBRARY DESTINATION lib/
                RUNTIME DESTINATION bin/
        )

        # Install public header files
        install(FILES ${PUBLIC_HEADER_FILES} "${BLUE_INVOKE_GENERATED_DIR}/BlueInvokeConfig.h" DESTINATION include)

        # Install target export configuration
        install(
                EXPORT BlueExposureTargets
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/carbon-blueexposure
                FILE carbon-blueexposure.cmake
        )
        install(
                FILES carbon-blueexposureConfig.cmake
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/carbon-blueexposure
        )
    endif()
endif()
