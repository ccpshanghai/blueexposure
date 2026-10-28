// Copyright (c) 2026 CCP Games
//
// Members through the C ABI: attributes (VarEntry offset and VARTYPE), properties
// (VarEntry::mCGet / mCSet), inherited attributes, object references and weak references.

#include "TestCAbiUtil.h"
#include "TestAttributes.h"
#include "TestMultipleInterfaces.h"
#include "TestProperties.h"
#include "TestReferences.h"
#include "TestSuperclassAttributes.h"

using namespace CAbiTest;

// TestProperties is not declared with BLUE_CLASS, so it is created by class id
class TestPropertiesPtr : public BluePtr<TestProperties>
{
public:
	bool CreateInstance() { return BluePtr<TestProperties>::CreateInstance( *TestProperties::Clsid() ); }
};

//--------------------------------------------------------------------
// Attributes
//--------------------------------------------------------------------

// Set name to in, read it back, and compare the result with expected
template< typename T >
static void SetAndGet( T* obj, const char* name, const BlueCValue& in, const BlueCValue& expected )
{
	SetOk( obj, name, in );
	Result r;
	Get( r, obj, name );
	EXPECT_CABI_OK( r ) << " getting " << name;
	ASSERT_EQ( r.Kind(), expected.kind ) << name;
	switch( expected.kind )
	{
	case BLUE_VK_STRING:
		EXPECT_EQ( r.S(), std::string( expected.u.str.ptr, expected.u.str.len ) ) << name;
		break;
	case BLUE_VK_DOUBLE:
		EXPECT_EQ( r.D(), expected.u.d ) << name;
		break;
	default:
		EXPECT_EQ( r.out.u.u, expected.u.u ) << name;
		break;
	}
}

template< typename T >
static void ExpectAllAttributes( T* obj )
{
	const std::string unicode = "\xC3\xBE\xC3\xA6\xE2\x82\xAC";
	SetAndGet( obj, "myString", Str( "string" ), Str( "string" ) );
	SetAndGet( obj, "myUnicode", Str( unicode ), Str( unicode ) );
	SetAndGet( obj, "myBool", Bool( true ), Bool( true ) );
	SetAndGet( obj, "myInt", Int( -123456 ), Int( -123456 ) );
	SetAndGet( obj, "myUInt", UInt( 4000000000u ), UInt( 4000000000u ) );
	SetAndGet( obj, "myFloat", Double( 0.75 ), Double( 0.75 ) );
	SetAndGet( obj, "myDouble", Double( 0.1 ), Double( 0.1 ) );
	SetAndGet( obj, "myInt64", Int( -5000000000LL ), Int( -5000000000LL ) );
	SetAndGet( obj, "myUInt64", UInt( ~uint64_t( 0 ) ), UInt( ~uint64_t( 0 ) ) );
	SetAndGet( obj, "sharedString", Str( "shared" ), Str( "shared" ) );
	SetAndGet( obj, "sharedStringW", Str( unicode ), Str( unicode ) );
}

TEST( CAbiAttributes, EveryType )
{
	TestAttributesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	ExpectAllAttributes( (TestAttributes*)obj );
}

TEST( CAbiAttributes, Inherited )
{
	// TestSuperclassAttributes maps nothing itself; its parent's attributes are found with the
	// parent offset applied
	TestSuperclassAttributesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	ptrdiff_t extraOffset = -1;
	EXPECT_NE( BlueCFindMember( obj->ClassType(), "myInt", 5, &extraOffset ), nullptr );
	EXPECT_EQ( extraOffset, 0 );
	ExpectAllAttributes( (TestSuperclassAttributes*)obj );
}

TEST( CAbiAttributes, Errors )
{
	TestAttributesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestAttributes* raw = obj;

	Result overflow, type, unknown, nullValue;
	Set( overflow, raw, "myInt", Int( int64_t( 1 ) << 40 ) );
	Set( type, raw, "myString", Int( 1 ) );
	Get( unknown, raw, "noSuchMember" );
	nullValue.status = BlueCSetMember( reinterpret_cast<IRoot*>( raw ), "myInt", 5, nullptr, &nullValue.err );

	EXPECT_CABI_ERROR( overflow, BLUE_E_OVERFLOW );
	EXPECT_NE( overflow.Message().find( "value:" ), std::string::npos ) << overflow.Message();
	EXPECT_CABI_ERROR( type, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( unknown, BLUE_E_NOT_FOUND );
	EXPECT_CABI_ERROR( nullValue, BLUE_E_INVALID_ARGUMENT );

	// The failed assignments left the values alone
	Result r;
	Get( r, raw, "myInt" );
	EXPECT_EQ( r.I(), 0 );
}

TEST( CAbiAttributes, EmbeddedObjectIsReadOnly )
{
	// myVector is a parent-locked list (VARTYPE IROOT), mapped Be::READ
	TestAttributesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestAttributes* raw = obj;

	Result get, set;
	Get( get, raw, "myVector" );
	Set( set, raw, "myVector", None() );

	EXPECT_CABI_OK( get );
	ASSERT_EQ( get.Kind(), uint32_t( BLUE_VK_OBJECT ) );
	EXPECT_STRNE( BlueCClassNameOf( get.O() ), "<unknown>" );
	EXPECT_CABI_ERROR( set, BLUE_E_READ_ONLY );
}


//--------------------------------------------------------------------
// Properties
//--------------------------------------------------------------------

TEST( CAbiProperties, EveryType )
{
	TestPropertiesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestProperties* raw = obj;
	const std::string unicode = "\xC3\xBE\xC3\xA6\xE2\x82\xAC";

	SetAndGet( raw, "myString", Str( "property" ), Str( "property" ) );
	SetAndGet( raw, "myUnicode", Str( unicode ), Str( unicode ) );
	SetAndGet( raw, "myBool", Bool( true ), Bool( true ) );
	SetAndGet( raw, "myInt", Int( -9 ), Int( -9 ) );
	SetAndGet( raw, "myUInt", UInt( 9 ), UInt( 9 ) );
	SetAndGet( raw, "myFloat", Double( 1.25 ), Double( 1.25 ) );
	SetAndGet( raw, "myDouble", Double( 2.5 ), Double( 2.5 ) );
	SetAndGet( raw, "myInt64", Int( INT64_MIN ), Int( INT64_MIN ) );
	SetAndGet( raw, "myUInt64", UInt( UINT64_MAX ), UInt( UINT64_MAX ) );
	SetAndGet( raw, "sharedString", Str( "pooled" ), Str( "pooled" ) );
	SetAndGet( raw, "sharedStringW", Str( unicode ), Str( unicode ) );
}

TEST( CAbiProperties, ThunksInTheMemberTable )
{
	// The VarEntry of a property carries the C getter and setter; attributes carry neither
	const Be::VarEntry* property = BlueCFindMember( TestProperties::ClassType_(), "myInt", 5, nullptr );
	const Be::VarEntry* readOnly = BlueCFindMember( TestProperties::ClassType_(), "myStringConst", 13, nullptr );
	const Be::VarEntry* attribute = BlueCFindMember( TestProperties::ClassType_(), "returnError", 11, nullptr );
	ASSERT_NE( property, nullptr );
	ASSERT_NE( readOnly, nullptr );
	ASSERT_NE( attribute, nullptr );
	EXPECT_NE( property->mCGet, nullptr );
	EXPECT_NE( property->mCSet, nullptr );
	EXPECT_NE( readOnly->mCGet, nullptr );
	EXPECT_EQ( readOnly->mCSet, nullptr );
	EXPECT_EQ( attribute->mCGet, nullptr );
	EXPECT_EQ( attribute->mCSet, nullptr );

	// Called directly
	TestPropertiesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	BlueCValue in = Int( 77 );
	BlueCError err = { BLUE_OK, nullptr, 0 };
	EXPECT_EQ( property->mCSet( AsC( (TestProperties*)obj ), &in, &err ), int32_t( BLUE_OK ) );
	Result r;
	r.status = property->mCGet( AsC( (TestProperties*)obj ), &r.out, &r.err );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.I(), 77 );

	Result wrongSelf;
	TestAttributesPtr other;
	ASSERT_TRUE( other.CreateInstance() );
	wrongSelf.status = property->mCGet( AsC( (TestAttributes*)other ), &wrongSelf.out, &wrongSelf.err );
	EXPECT_CABI_ERROR( wrongSelf, BLUE_E_SELF );
}

TEST( CAbiProperties, ReadOnly )
{
	TestPropertiesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestProperties* raw = obj;
	SetOk( raw, "myString", Str( "abc" ) );

	Result constValue, constRef, set;
	Get( constValue, raw, "myStringConst" );
	Get( constRef, raw, "myStringConstRef" );
	Set( set, raw, "myStringConst", Str( "x" ) );

	EXPECT_EQ( constValue.S(), "abc" );
	EXPECT_EQ( constRef.S(), "abc" );
	EXPECT_CABI_ERROR( set, BLUE_E_READ_ONLY );
}

TEST( CAbiProperties, BeResultGetterAndSetter )
{
	// GetIntWithError( int& ) / SetIntWithError( int ) return Be::Result
	TestPropertiesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestProperties* raw = obj;

	SetAndGet( raw, "myIntWithError", Int( 31 ), Int( 31 ) );

	SetOk( raw, "returnError", Bool( true ) );
	Result get, set;
	Get( get, raw, "myIntWithError" );
	Set( set, raw, "myIntWithError", Int( 1 ) );
	EXPECT_CABI_ERROR( get, BLUE_E_FAILED );
	EXPECT_EQ( get.Message(), "Failed!" );
	EXPECT_CABI_ERROR( set, BLUE_E_FAILED );
}

TEST( CAbiProperties, WrongValue )
{
	TestPropertiesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );

	Result type, overflow;
	Set( type, (TestProperties*)obj, "myBool", Str( "true" ) );
	Set( overflow, (TestProperties*)obj, "myUInt", Int( -1 ) );
	EXPECT_CABI_ERROR( type, BLUE_E_TYPE );
	EXPECT_EQ( type.Message(), "value: expected bool, got string" );
	EXPECT_CABI_ERROR( overflow, BLUE_E_OVERFLOW );
}


//--------------------------------------------------------------------
// References
//--------------------------------------------------------------------

TEST( CAbiReferences, StrongReference )
{
	TestReferencesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestReferences* raw = obj;
	TestMultipleInterfacesPtr target;
	ASSERT_TRUE( target.CreateInstance() );
	TestMultipleInterfaces* t = target;
	const int refs = RefCount( t );

	SetOk( raw, "strongRef", Obj( t ) );
	EXPECT_EQ( RefCount( t ), refs + 1 );
	{
		Result r;
		Get( r, raw, "strongRef" );
		EXPECT_CABI_OK( r );
		ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_OBJECT ) );
		EXPECT_EQ( dynamic_cast<TestMultipleInterfaces*>( r.O() ), t );
		EXPECT_EQ( RefCount( t ), refs + 2 );
	}

	SetOk( raw, "strongRef", None() );
	EXPECT_EQ( RefCount( t ), refs );
	Result cleared;
	Get( cleared, raw, "strongRef" );
	EXPECT_EQ( cleared.Kind(), uint32_t( BLUE_VK_NONE ) );
}

TEST( CAbiReferences, WeakReference )
{
	TestReferencesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestReferences* raw = obj;

	const uint32_t live = LiveCount<TestReferences>();
	{
		TestReferencesPtr target;
		ASSERT_TRUE( target.CreateInstance() );
		TestReferences* t = target;
		const int refs = RefCount( t );

		SetOk( raw, "weakRef", Obj( t ) );
		EXPECT_EQ( RefCount( t ), refs );

		Result r;
		Get( r, raw, "weakRef" );
		EXPECT_EQ( r.O(), reinterpret_cast<IRoot*>( t ) );
	}
	// The target is gone, and the weak reference with it
	EXPECT_EQ( LiveCount<TestReferences>(), live );
	Result gone;
	Get( gone, raw, "weakRef" );
	EXPECT_CABI_OK( gone );
	EXPECT_EQ( gone.Kind(), uint32_t( BLUE_VK_NONE ) );
}
