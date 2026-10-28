// Copyright (c) 2026 CCP Games
//
// BlueExposureCAbiTests: the classes of tests/ through the C-ABI backend only.

#include <gtest/gtest.h>

// The test classes' module (the Python test module defines this in BLUE_STANDARD_MODULE_INIT)
const char* g_moduleName = "_blueexposurecabitest";

int main( int argc, char** argv )
{
	// What a module init does, minus Python: make the classes creatable by clsid
	BeClasses->RegisterClasses( BlueRegistration::GetClassRegs() );

	testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
