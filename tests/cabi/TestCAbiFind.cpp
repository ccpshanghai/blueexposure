// Copyright (c) 2026 CCP Games
//
// The C++ object-graph search in Find.h (FindFirstRoute, FindInterface, FindReference,
// RouteStep) builds in every flavour, not only with Python.

#include "TestCAbiUtil.h"
#include "TestMultipleInterfaces.h"
#include "TestReferences.h"
#include "Find.h"

using namespace CAbiTest;

TEST( CAbiFind, RouteInterfaceAndReference )
{
	TestReferencesPtr from;
	ASSERT_TRUE( from.CreateInstance() );
	TestReferences* raw = from;
	IRoot* fromRoot = reinterpret_cast<IRoot*>( raw );
	TestMultipleInterfacesPtr target;
	ASSERT_TRUE( target.CreateInstance() );
	IRoot* targetRoot = reinterpret_cast<IRoot*>( static_cast<TestMultipleInterfaces*>( target ) );

	EXPECT_FALSE( FindReference( fromRoot, targetRoot ) );
	EXPECT_FALSE( FindFirstRoute( fromRoot, targetRoot, nullptr ) );
	EXPECT_TRUE( FindInterface( fromRoot, "ITestInterfaceTwo" ).empty() );

	SetOk( raw, "strongRef", Obj( targetRoot ) );

	EXPECT_TRUE( FindReference( fromRoot, targetRoot ) );
	EXPECT_FALSE( FindReference( targetRoot, fromRoot ) );

	std::vector<RouteStep> route;
	ASSERT_TRUE( FindFirstRoute( fromRoot, targetRoot, &route ) );
	ASSERT_EQ( route.size(), 1u );
	IRoot* next = route[0].GetNextObject();
	ASSERT_NE( next, nullptr );
	EXPECT_EQ( next->GetRootObject(), targetRoot->GetRootObject() );

	std::vector<IRootPtr> found = FindInterface( fromRoot, "ITestInterfaceTwo" );
	ASSERT_EQ( found.size(), 1u );
	EXPECT_EQ( found[0]->GetRootObject(), targetRoot->GetRootObject() );

	SetOk( raw, "strongRef", None() );
}
