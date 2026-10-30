// Copyright (c) 2026 CCP Games
//
// Interface methods of the C-ABI backend (BLUE_REGISTER_CTHUNKER): C methods registered for an
// interface IID, which every object whose class or a parent implements that interface answers
// to, after its class's own methods. The C twin of BLUE_REGISTER_THUNKER.

#include "TestCAbiUtil.h"
#include "TestMethods.h"
#include "TestMultipleInterfaces.h"
#include "TestSuperclassMethods.h"

#include <map>
#include <string>

using namespace CAbiTest;

namespace
{

// Returns a string naming the thunk, so a test can tell which method a lookup found
template< size_t N >
int32_t ReturnName( BlueObject* self, const BlueCValue*, uint32_t argc, BlueCValue* out, BlueCError* err, const char ( &name )[N] )
{
	if( !self )
	{
		return BlueCSetError( err, BLUE_E_SELF, "no object" );
	}
	if( argc )
	{
		return BlueCSetError( err, BLUE_E_ARGUMENT_COUNT, "no arguments expected" );
	}
	return BlueCValueSetString( out, name, N - 1, err );
}

int32_t InterfaceOnly( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	return ReturnName( self, args, argc, out, err, "interface InterfaceOnly" );
}

// Same name as a TestMethods method: the class's own method must win
int32_t IntArity0( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	return ReturnName( self, args, argc, out, err, "interface IntArity0" );
}

int32_t InterfaceOne( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	// A thunk on an interface resolves self to that interface, as the Python thunkers do
	ITestInterfaceOne* one = dynamic_cast<ITestInterfaceOne*>( reinterpret_cast<IRoot*>( self ) );
	if( !one )
	{
		return BlueCSetError( err, BLUE_E_SELF, "not an ITestInterfaceOne" );
	}
	one->MethodOne();
	return ReturnName( self, args, argc, out, err, "interface InterfaceOne" );
}

// A second table for the same interface, without signatures
int32_t InterfaceOneAgain( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	return ReturnName( self, args, argc, out, err, "interface InterfaceOneAgain" );
}

// TestMethods answers to its own class name as an interface (EXPOSURE_BEGIN maps it), so a
// thunker on that IID reaches TestMethods and, through the parent chain, TestSuperclassMethods
const BlueCMethodDef s_testMethodsThunks[] = {
	{ "InterfaceOnly", &InterfaceOnly, "an interface method", 0 },
	{ "IntArity0", &IntArity0, "hidden by the class method", 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

// C signatures, in BlueCAbiTypeName.h's names; hand-written ones may name types no C++ type has
const char* const s_bytesArgs[] = { "bytes", "int64" };

const BlueCSignature s_testMethodsSignatures[] = {
	{ "string", nullptr, 0, 0 },
	{ "bytes", s_bytesArgs, 2, 1 },
};

const BlueCMethodDef s_interfaceOneThunks[] = {
	{ "InterfaceOne", &InterfaceOne, "on ITestInterfaceOne", 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

const BlueCMethodDef s_interfaceOneAgainThunks[] = {
	// The first registration of a name wins
	{ "InterfaceOne", &InterfaceOneAgain, "shadowed by the first table", 0 },
	{ "InterfaceOneAgain", &InterfaceOneAgain, "no signature", 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

} // namespace

BLUE_REGISTER_CTHUNKER_WITH_SIGNATURES( s_testMethodsThunks, s_testMethodsSignatures, Be::IID( "TestMethods" ) );
BLUE_REGISTER_CTHUNKER( s_interfaceOneThunks, GetITestInterfaceOneIID() );
BLUE_REGISTER_CTHUNKER( s_interfaceOneAgainThunks, GetITestInterfaceOneIID() );


//--------------------------------------------------------------------
// Registration
//--------------------------------------------------------------------

TEST( CAbiInterfaceMethods, Registered )
{
	const CThunkerRegsVector& regs = BlueRegistration::GetGlobalCThunkerRegs();
	size_t found = 0;
	for( const BlueCThunkerRegistration& reg : regs )
	{
		if( reg.mMethods == s_testMethodsThunks )
		{
			++found;
			EXPECT_TRUE( reg.mIID.IsEqual( Be::IID( "TestMethods" ) ) );
			EXPECT_EQ( reg.mSignatures, s_testMethodsSignatures );
		}
		if( reg.mMethods == s_interfaceOneThunks )
		{
			++found;
			EXPECT_TRUE( reg.mIID.IsEqual( GetITestInterfaceOneIID() ) );
			EXPECT_EQ( reg.mSignatures, nullptr );
		}
	}
	EXPECT_EQ( found, size_t( 2 ) );
}


//--------------------------------------------------------------------
// Lookup: the class chain first, then interface methods
//--------------------------------------------------------------------

TEST( CAbiInterfaceMethods, FoundAfterTheClassChain )
{
	TestMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	const Be::ClassInfo* type = obj->ClassType();

	EXPECT_EQ( BlueCFindMethod( type, "InterfaceOnly", 13 ), &s_testMethodsThunks[0] );

	Result r;
	Call( r, (TestMethods*)obj, "InterfaceOnly" );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.S(), "interface InterfaceOnly" );
}

TEST( CAbiInterfaceMethods, ClassMethodWins )
{
	TestMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	const BlueCMethodDef* def = BlueCFindMethod( obj->ClassType(), "IntArity0", 9 );
	ASSERT_NE( def, nullptr );
	EXPECT_NE( def, &s_testMethodsThunks[1] );

	Result r;
	Call( r, (TestMethods*)obj, "IntArity0" );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.Kind(), uint32_t( BLUE_VK_INT ) );
}

TEST( CAbiInterfaceMethods, ParentsInterfacesAreWalked )
{
	// TestSuperclassMethods maps only its own name; TestMethods' interfaces come with the parent
	TestSuperclassMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	EXPECT_EQ( BlueCFindMethod( obj->ClassType(), "InterfaceOnly", 13 ), &s_testMethodsThunks[0] );

	Result r;
	Call( r, (TestSuperclassMethods*)obj, "InterfaceOnly" );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.S(), "interface InterfaceOnly" );
}

TEST( CAbiInterfaceMethods, OnlyForImplementors )
{
	TestMethodsPtr methods;
	TestMultipleInterfacesPtr interfaces;
	ASSERT_TRUE( methods.CreateInstance() );
	ASSERT_TRUE( interfaces.CreateInstance() );

	EXPECT_EQ( BlueCFindMethod( methods->ClassType(), "InterfaceOne", 12 ), nullptr );
	EXPECT_EQ( BlueCFindMethod( interfaces->ClassType(), "InterfaceOnly", 13 ), nullptr );

	// A class without methods of its own gets its interfaces'; the first table registered wins
	EXPECT_EQ( BlueCFindMethod( interfaces->ClassType(), "InterfaceOne", 12 ), &s_interfaceOneThunks[0] );
	EXPECT_EQ( BlueCFindMethod( interfaces->ClassType(), "InterfaceOneAgain", 17 ), &s_interfaceOneAgainThunks[1] );

	Result r;
	Call( r, (TestMultipleInterfaces*)interfaces, "InterfaceOne" );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.S(), "interface InterfaceOne" );

	// Names are matched exactly, with their length
	EXPECT_EQ( BlueCFindMethod( interfaces->ClassType(), "InterfaceOn", 11 ), nullptr );
	EXPECT_EQ( BlueCFindMethod( interfaces->ClassType(), "InterfaceOneX", 12 ), &s_interfaceOneThunks[0] );
}


//--------------------------------------------------------------------
// Enumeration (BlueCForEachMethod)
//--------------------------------------------------------------------

struct Visited
{
	std::vector<std::string> names;
	std::map<std::string, const BlueCMethodDef*> methods;
	std::map<std::string, const BlueCSignature*> signatures;
	size_t stopAfter = 0;
};

static bool Visit( const BlueCMethodDef* method, const BlueCSignature* signature, void* context )
{
	Visited& visited = *static_cast<Visited*>( context );
	visited.names.push_back( method->name );
	EXPECT_EQ( visited.methods.count( method->name ), size_t( 0 ) ) << method->name << " visited twice";
	visited.methods[method->name] = method;
	visited.signatures[method->name] = signature;
	return visited.stopAfter == 0 || visited.names.size() < visited.stopAfter;
}

static size_t IndexOf( const Visited& visited, const char* name )
{
	for( size_t i = 0; i < visited.names.size(); ++i )
	{
		if( visited.names[i] == name )
		{
			return i;
		}
	}
	return size_t( -1 );
}

TEST( CAbiInterfaceMethods, ForEachMatchesFind )
{
	TestSuperclassMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	const Be::ClassInfo* type = obj->ClassType();

	Visited visited;
	EXPECT_TRUE( BlueCForEachMethod( type, &Visit, &visited ) );

	// Each name once, and the method BlueCFindMethod finds for it
	ASSERT_FALSE( visited.names.empty() );
	for( const auto& entry : visited.methods )
	{
		EXPECT_EQ( entry.second, BlueCFindMethod( type, entry.first.c_str(), entry.first.size() ) ) << entry.first;
	}

	// Every C method of the parent is there, followed by the interface method
	size_t parentMethods = 0;
	for( const BlueCMethodDef* def = type->mParentClassInfo->mCMethodTable; def->name; ++def )
	{
		EXPECT_NE( IndexOf( visited, def->name ), size_t( -1 ) ) << def->name;
		++parentMethods;
	}
	EXPECT_EQ( visited.names.size(), parentMethods + 1 );
	EXPECT_EQ( IndexOf( visited, "InterfaceOnly" ), visited.names.size() - 1 );
	EXPECT_EQ( visited.methods["IntArity0"], BlueCFindMethod( type, "IntArity0", 9 ) );
}

TEST( CAbiInterfaceMethods, ForEachSignatures )
{
	TestMethodsPtr methods;
	TestMultipleInterfacesPtr interfaces;
	ASSERT_TRUE( methods.CreateInstance() );
	ASSERT_TRUE( interfaces.CreateInstance() );

	// Class methods: the C signature MAP_METHOD_AND_WRAP recorded, at the method's index
	Visited visited;
	EXPECT_TRUE( BlueCForEachMethod( methods->ClassType(), &Visit, &visited ) );
	const BlueCSignature* intArity3 = visited.signatures["IntArity3"];
	ASSERT_NE( intArity3, nullptr );
	EXPECT_EQ( intArity3->argCount, 3u );
	EXPECT_STREQ( intArity3->returns, "int32" );
	const Be::ClassInfo* type = methods->ClassType();
	EXPECT_EQ( intArity3, &type->mCSignatures[visited.methods["IntArity3"] - type->mCMethodTable] );

	// Interface methods: the registration's signatures, or none
	EXPECT_EQ( visited.signatures["InterfaceOnly"], &s_testMethodsSignatures[0] );
	EXPECT_STREQ( visited.signatures["InterfaceOnly"]->returns, "string" );
	// Hidden by the class method of that name, so its signature is the class method's
	EXPECT_EQ( visited.signatures["IntArity0"]->argCount, 0u );
	EXPECT_STREQ( visited.signatures["IntArity0"]->returns, "int32" );

	Visited other;
	EXPECT_TRUE( BlueCForEachMethod( interfaces->ClassType(), &Visit, &other ) );
	EXPECT_EQ( other.names, ( std::vector<std::string>{ "InterfaceOne", "InterfaceOneAgain" } ) );
	EXPECT_EQ( other.methods["InterfaceOne"], &s_interfaceOneThunks[0] );
	EXPECT_EQ( other.signatures["InterfaceOne"], nullptr );
	EXPECT_EQ( other.signatures["InterfaceOneAgain"], nullptr );
}

TEST( CAbiInterfaceMethods, ForEachStops )
{
	TestMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );

	Visited visited;
	visited.stopAfter = 2;
	EXPECT_FALSE( BlueCForEachMethod( obj->ClassType(), &Visit, &visited ) );
	EXPECT_EQ( visited.names.size(), size_t( 2 ) );

	EXPECT_TRUE( BlueCForEachMethod( nullptr, &Visit, &visited ) );
	EXPECT_EQ( visited.names.size(), size_t( 2 ) );
}
