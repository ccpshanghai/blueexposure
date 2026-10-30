// Copyright (c) 2026 CCP Games
//
// BlueCValue layout and every value kind, round-tripped through the test classes' C thunks;
// conversions no test class method uses (Matrix, Color, Quaternion, std::list, Be::Optional
// returns, ...) are checked on BlueCExtract / BlueCWrap directly.

#include "TestCAbiUtil.h"
#include "TestCallbacks.h"
#include "TestMethods.h"
#include "TestMultipleInterfaces.h"

#include <cmath>
#include <list>
#include <map>

using namespace CAbiTest;

//--------------------------------------------------------------------
// Layout
//--------------------------------------------------------------------

TEST( CAbiLayout, CMatchesCpp )
{
	size_t c[16] = {};
	ASSERT_EQ( BlueCAbiTestCLayout( c, 16 ), size_t( 14 ) );

	const size_t cpp[] = {
		sizeof( BlueCValue ),
		offsetof( BlueCValue, kind ),
		offsetof( BlueCValue, aux ),
		offsetof( BlueCValue, u ),
		offsetof( BlueCValue, u.str.len ),
		offsetof( BlueCValue, u.callback ),
		offsetof( BlueCValue, u.vec.v ),
		offsetof( BlueCValue, u.arr.count ),
		offsetof( BlueCValue, u.map.count ),
		sizeof( BlueCError ),
		offsetof( BlueCError, message ),
		offsetof( BlueCError, message_len ),
		sizeof( BlueCMethodDef ),
		sizeof( BlueCCallback ),
	};
	for( size_t i = 0; i < 14; ++i )
	{
		EXPECT_EQ( c[i], cpp[i] ) << "layout entry " << i;
	}
	EXPECT_EQ( sizeof( BlueCValue ), size_t( 40 ) );
	EXPECT_EQ( offsetof( BlueCValue, u ), size_t( 8 ) );
	EXPECT_EQ( sizeof( BlueCError ), size_t( 24 ) );
}

TEST( CAbiLayout, Version )
{
	EXPECT_EQ( BLUE_CABI_VERSION, ( 1u << 16 ) | 0u );
}


//--------------------------------------------------------------------
// Scalars and strings through TestMethods
//--------------------------------------------------------------------

class CAbiValues : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE( m_methods.CreateInstance() );
		ASSERT_TRUE( m_interfaces.CreateInstance() );
	}

	TestMethods* Methods() { return m_methods; }

	TestMethodsPtr m_methods;
	TestMultipleInterfacesPtr m_interfaces;
};

TEST_F( CAbiValues, Bool )
{
	Result t, f, fromInt, wrong;
	Call( t, Methods(), "MethodReturningBool", { Bool( true ) } );
	Call( f, Methods(), "MethodReturningBool", { Bool( false ) } );
	Call( fromInt, Methods(), "MethodReturningBool", { Int( 5 ) } );
	Call( wrong, Methods(), "MethodReturningBool", { Str( "yes" ) } );

	EXPECT_CABI_OK( t );
	EXPECT_EQ( t.Kind(), uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( t.I(), 1 );
	EXPECT_CABI_OK( f );
	EXPECT_EQ( f.I(), 0 );
	EXPECT_CABI_OK( fromInt );
	EXPECT_EQ( fromInt.I(), 1 );
	EXPECT_CABI_ERROR( wrong, BLUE_E_TYPE );
	EXPECT_NE( wrong.Message().find( "argument 0" ), std::string::npos ) << wrong.Message();
}

TEST_F( CAbiValues, Int )
{
	Result neg, fromUInt, overflow, fromDouble;
	Call( neg, Methods(), "MethodReturningInt", { Int( -42 ) } );
	Call( fromUInt, Methods(), "MethodReturningInt", { UInt( 7 ) } );
	Call( overflow, Methods(), "MethodReturningInt", { Int( int64_t( 1 ) << 40 ) } );
	Call( fromDouble, Methods(), "MethodReturningInt", { Double( 1.5 ) } );

	EXPECT_CABI_OK( neg );
	EXPECT_EQ( neg.Kind(), uint32_t( BLUE_VK_INT ) );
	EXPECT_EQ( neg.I(), -42 );
	EXPECT_CABI_OK( fromUInt );
	EXPECT_EQ( fromUInt.I(), 7 );
	EXPECT_CABI_ERROR( overflow, BLUE_E_OVERFLOW );
	EXPECT_CABI_ERROR( fromDouble, BLUE_E_TYPE );
}

TEST_F( CAbiValues, UnsignedInt )
{
	Result big, negative;
	Call( big, Methods(), "MethodReturningUnsignedInt", { UInt( 4000000000u ) } );
	Call( negative, Methods(), "MethodReturningUnsignedInt", { Int( -1 ) } );

	EXPECT_CABI_OK( big );
	EXPECT_EQ( big.Kind(), uint32_t( BLUE_VK_UINT ) );
	EXPECT_EQ( big.U(), 4000000000u );
	EXPECT_CABI_ERROR( negative, BLUE_E_OVERFLOW );
}

TEST_F( CAbiValues, FloatAndDouble )
{
	Result f, fromInt, d;
	Call( f, Methods(), "MethodReturningFloat", { Double( 0.5 ) } );
	Call( fromInt, Methods(), "MethodReturningFloat", { Int( 3 ) } );
	Call( d, Methods(), "MethodReturningDouble", { Double( 1.0 / 3.0 ) } );

	EXPECT_CABI_OK( f );
	EXPECT_EQ( f.Kind(), uint32_t( BLUE_VK_DOUBLE ) );
	EXPECT_EQ( f.D(), 0.5 );
	EXPECT_CABI_OK( fromInt );
	EXPECT_EQ( fromInt.D(), 3.0 );
	EXPECT_CABI_OK( d );
	EXPECT_EQ( d.D(), 1.0 / 3.0 );
}

TEST_F( CAbiValues, String )
{
	// Embedded NUL survives, and the input need not be NUL-terminated
	const std::string withNul( "hello\0world", 11 );
	const char prefix[] = { 'a', 'b', 'c', 'X', 'Y' };

	Result s, cut, constChar, empty, wrong;
	Call( s, Methods(), "MethodReturningString", { Str( withNul ) } );
	Call( cut, Methods(), "MethodReturningString", { Str( prefix, 3 ) } );
	Call( constChar, Methods(), "MethodReturningConstChar", { Str( prefix, 3 ) } );
	Call( empty, Methods(), "MethodReturningString", { Str( nullptr, 0 ) } );
	Call( wrong, Methods(), "MethodReturningString", { Int( 1 ) } );

	EXPECT_CABI_OK( s );
	EXPECT_EQ( s.S(), withNul );
	EXPECT_CABI_OK( cut );
	EXPECT_EQ( cut.S(), "abc" );
	EXPECT_CABI_OK( constChar );
	EXPECT_EQ( constChar.S(), "abc" );
	EXPECT_CABI_OK( empty );
	EXPECT_EQ( empty.S(), "" );
	EXPECT_CABI_ERROR( wrong, BLUE_E_TYPE );
}

TEST_F( CAbiValues, WideString )
{
	// Two-, three- and four-byte UTF-8 sequences round-trip through wchar_t
	const std::string text = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9D\x84\x9E";

	Result w, constW, function, invalid, overlong;
	Call( w, Methods(), "MethodReturningWString", { Str( text ) } );
	Call( constW, Methods(), "MethodReturningConstWChar", { Str( text ) } );
	CallFunction( function, "FunctionReturningWString", { Str( text ) } );
	Call( invalid, Methods(), "MethodReturningWString", { Str( "\xFF", 1 ) } );
	Call( overlong, Methods(), "MethodReturningWString", { Str( "\xC0\xAF", 2 ) } );

	EXPECT_CABI_OK( w );
	EXPECT_EQ( w.S(), text );
	EXPECT_CABI_OK( constW );
	EXPECT_EQ( constW.S(), text );
	EXPECT_CABI_OK( function );
	EXPECT_EQ( function.S(), text );
	EXPECT_CABI_ERROR( invalid, BLUE_E_INVALID_ARGUMENT );
	EXPECT_CABI_ERROR( overlong, BLUE_E_INVALID_ARGUMENT );
}

TEST_F( CAbiValues, SharedStrings )
{
	const std::string wide = "\xC3\xA6\xC3\xB0";

	Result toShared, fromShared, toSharedW, fromSharedW;
	Call( toShared, Methods(), "ConvertStringToSharedString", { Str( "pooled" ) } );
	Call( fromShared, Methods(), "ConvertSharedStringToString", { Str( "pooled" ) } );
	Call( toSharedW, Methods(), "ConvertWStringToSharedStringW", { Str( wide ) } );
	Call( fromSharedW, Methods(), "ConvertSharedStringWToWString", { Str( wide ) } );

	EXPECT_CABI_OK( toShared );
	EXPECT_EQ( toShared.S(), "pooled" );
	EXPECT_CABI_OK( fromShared );
	EXPECT_EQ( fromShared.S(), "pooled" );
	EXPECT_CABI_OK( toSharedW );
	EXPECT_EQ( toSharedW.S(), wide );
	EXPECT_CABI_OK( fromSharedW );
	EXPECT_EQ( fromSharedW.S(), wide );
}

TEST_F( CAbiValues, Enum )
{
	Result r, overflow;
	Call( r, Methods(), "MethodWithEnumParam", { Int( TestMethods::THREE ) } );
	Call( overflow, Methods(), "MethodWithEnumParam", { Int( int64_t( 1 ) << 33 ) } );

	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.Kind(), uint32_t( BLUE_VK_INT ) );
	EXPECT_EQ( r.I(), int64_t( TestMethods::THREE ) );
	EXPECT_CABI_ERROR( overflow, BLUE_E_OVERFLOW );
}


//--------------------------------------------------------------------
// Vectors
//--------------------------------------------------------------------

static void ExpectVector( const Result& r, uint32_t aux, std::initializer_list<double> components )
{
	EXPECT_CABI_OK( r );
	ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_VECTOR ) );
	EXPECT_EQ( r.out.aux, aux );
	size_t i = 0;
	for( double c : components )
	{
		EXPECT_EQ( r.out.u.vec.v[i], c ) << "component " << i;
		++i;
	}
}

TEST_F( CAbiValues, Vectors )
{
	Result v2, v2d, v3, v3d, v3i, v4d;
	Call( v2, Methods(), "MethodReturningVector2", { Vec( { 1, 2 } ) } );
	CallFunction( v2d, "FunctionReturningVector2d", { Vec( { 0.1, 0.2 }, BLUE_VEC_ELEMENT_DOUBLE ) } );
	Call( v3, Methods(), "MethodReturningVector3", { Vec( { 1.5, -2, 3 } ) } );
	CallFunction( v3d, "FunctionReturningVector3d", { Vec( { 0.1, 0.2, 0.3 }, BLUE_VEC_ELEMENT_DOUBLE ) } );
	CallFunction( v3i, "FunctionReturningVector3i", { Vec( { 1, -2, 3 }, BLUE_VEC_ELEMENT_INT32 ) } );
	CallFunction( v4d, "FunctionReturningVector4d", { Vec( { 0.1, 0.2, 0.3, 0.4 }, BLUE_VEC_ELEMENT_DOUBLE ) } );

	ExpectVector( v2, 2 | BLUE_VEC_ELEMENT_FLOAT, { 1, 2 } );
	ExpectVector( v2d, 2 | BLUE_VEC_ELEMENT_DOUBLE, { 0.1, 0.2 } );
	ExpectVector( v3, 3 | BLUE_VEC_ELEMENT_FLOAT, { 1.5, -2, 3 } );
	ExpectVector( v3d, 3 | BLUE_VEC_ELEMENT_DOUBLE, { 0.1, 0.2, 0.3 } );
	ExpectVector( v3i, 3 | BLUE_VEC_ELEMENT_INT32, { 1, -2, 3 } );
	ExpectVector( v4d, 4 | BLUE_VEC_ELEMENT_DOUBLE, { 0.1, 0.2, 0.3, 0.4 } );
}

TEST_F( CAbiValues, VectorFromArrayAndShortVector )
{
	std::vector<BlueCValue> items = { Int( 1 ), Double( 2.5 ), UInt( 3 ) };
	Result fromArray, shortVector;
	Call( fromArray, Methods(), "MethodReturningVector3", { Array( items ) } );
	// Like the Python backend, fewer components than the type has are accepted; the rest are 0
	Call( shortVector, Methods(), "MethodReturningVector3", { Vec( { 7, 8 } ) } );

	ExpectVector( fromArray, 3 | BLUE_VEC_ELEMENT_FLOAT, { 1, 2.5, 3 } );
	ExpectVector( shortVector, 3 | BLUE_VEC_ELEMENT_FLOAT, { 7, 8, 0 } );
}

TEST_F( CAbiValues, VectorErrors )
{
	std::vector<BlueCValue> badItems = { Int( 1 ), Str( "x" ) };
	Result tooMany, notInteger, intOverflow, badItem, notVector;
	Call( tooMany, Methods(), "MethodReturningVector3", { Vec( { 1, 2, 3, 4 } ) } );
	CallFunction( notInteger, "FunctionReturningVector3i", { Vec( { 1.5, 2, 3 } ) } );
	CallFunction( intOverflow, "FunctionReturningVector3i", { Vec( { 1e12, 2, 3 } ) } );
	Call( badItem, Methods(), "MethodReturningVector3", { Array( badItems ) } );
	Call( notVector, Methods(), "MethodReturningVector3", { Str( "1,2,3" ) } );

	EXPECT_CABI_ERROR( tooMany, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( notInteger, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( intOverflow, BLUE_E_OVERFLOW );
	EXPECT_CABI_ERROR( badItem, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( notVector, BLUE_E_TYPE );
}


//--------------------------------------------------------------------
// Containers
//--------------------------------------------------------------------

TEST_F( CAbiValues, ListArguments )
{
	std::vector<BlueCValue> ints = { Int( 1 ), Int( 2 ), Int( 3 ) };
	std::vector<BlueCValue> none;
	std::vector<BlueCValue> inner1 = { Int( 1 ), Int( 2 ) };
	std::vector<BlueCValue> inner2 = { Int( 3 ) };
	std::vector<BlueCValue> nested = { Array( inner1 ), Array( inner2 ) };
	std::vector<BlueCValue> vectors = { Vec( { 1, 2, 3 } ), Vec( { 4, 5, 6 } ) };
	std::vector<BlueCValue> mixed = { Int( 1 ), Str( "two" ) };

	Result r1, r2, r3, r4, bad, notArray;
	Call( r1, Methods(), "MethodWithListOfIntParameter", { Array( ints ) } );
	Call( r2, Methods(), "MethodWithListOfIntParameter", { Array( none ) } );
	Call( r3, Methods(), "MethodWithListOfListOfIntParameter", { Array( nested ) } );
	Call( r4, Methods(), "MethodWithListOfVector3Parameter", { Array( vectors ) } );
	Call( bad, Methods(), "MethodWithListOfIntParameter", { Array( mixed ) } );
	Call( notArray, Methods(), "MethodWithListOfIntParameter", { Int( 1 ) } );

	EXPECT_CABI_OK( r1 );
	EXPECT_EQ( r1.I(), 3 );
	EXPECT_CABI_OK( r2 );
	EXPECT_EQ( r2.I(), 0 );
	EXPECT_CABI_OK( r3 );
	EXPECT_EQ( r3.I(), 3 );
	EXPECT_CABI_OK( r4 );
	EXPECT_EQ( r4.I(), 2 );
	EXPECT_CABI_ERROR( bad, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( notArray, BLUE_E_TYPE );
}

TEST_F( CAbiValues, ListReturns )
{
	Result ints, vectors, empty;
	Call( ints, Methods(), "MethodReturningListOfInt", { Int( 4 ) } );
	Call( vectors, Methods(), "MethodReturningListOfVector3", { Int( 2 ) } );
	Call( empty, Methods(), "MethodReturningListOfInt", { Int( 0 ) } );

	EXPECT_CABI_OK( ints );
	ASSERT_EQ( ints.Kind(), uint32_t( BLUE_VK_ARRAY ) );
	ASSERT_EQ( ints.out.u.arr.count, size_t( 4 ) );
	for( size_t i = 0; i < 4; ++i )
	{
		EXPECT_EQ( ints.out.u.arr.items[i].kind, uint32_t( BLUE_VK_INT ) );
		EXPECT_EQ( ints.out.u.arr.items[i].u.i, int64_t( i ) );
	}

	EXPECT_CABI_OK( vectors );
	ASSERT_EQ( vectors.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( vectors.out.u.arr.items[1].kind, uint32_t( BLUE_VK_VECTOR ) );
	EXPECT_EQ( vectors.out.u.arr.items[1].aux, 3 | BLUE_VEC_ELEMENT_FLOAT );
	EXPECT_EQ( vectors.out.u.arr.items[1].u.vec.v[2], 1.0 );

	EXPECT_CABI_OK( empty );
	EXPECT_EQ( empty.Kind(), uint32_t( BLUE_VK_ARRAY ) );
	EXPECT_EQ( empty.out.u.arr.count, size_t( 0 ) );
}

TEST_F( CAbiValues, Maps )
{
	std::vector<BlueCValue> kv = { Str( "a" ), Int( 1 ), Str( "b" ), Int( 2 ) };
	std::vector<BlueCValue> badKey = { Int( 1 ), Int( 1 ) };

	Result count, returned, bad;
	Call( count, Methods(), "MethodWithMapOfStringToIntParameter", { Map( kv ) } );
	Call( returned, Methods(), "MethodReturningMapOfStringToInt", { Int( 3 ) } );
	Call( bad, Methods(), "MethodWithMapOfStringToIntParameter", { Map( badKey ) } );

	EXPECT_CABI_OK( count );
	EXPECT_EQ( count.I(), 2 );

	EXPECT_CABI_OK( returned );
	ASSERT_EQ( returned.Kind(), uint32_t( BLUE_VK_MAP ) );
	ASSERT_EQ( returned.out.u.map.count, size_t( 3 ) );
	std::map<std::string, int64_t> got;
	for( size_t i = 0; i < 3; ++i )
	{
		got[AsString( returned.out.u.map.kv[2 * i] )] = returned.out.u.map.kv[2 * i + 1].u.i;
	}
	EXPECT_EQ( got, ( std::map<std::string, int64_t>{ { "one", 1 }, { "two", 2 }, { "three", 3 } } ) );

	EXPECT_CABI_ERROR( bad, BLUE_E_TYPE );
}

#if !BLUE_WITH_PYTHON
// With Python on, BlueScriptCallbackStatus takes the GIL even when no callback is set, so these
// need an interpreter; std::pair is covered by CAbiConvert.Containers there
TEST( CAbiValuesCallbacks, PairReturn )
{
	// Without a callback set the call fails and returns its preset result: a pair
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );

	Result pairInt, pairString;
	Call( pairInt, (TestCallbacks*)callbacks, "CallCallbackInt" );
	Call( pairString, (TestCallbacks*)callbacks, "CallCallbackStringInt", { Int( 5 ) } );

	EXPECT_CABI_OK( pairInt );
	ASSERT_EQ( pairInt.Kind(), uint32_t( BLUE_VK_ARRAY ) );
	ASSERT_EQ( pairInt.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( pairInt.out.u.arr.items[0].kind, uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( pairInt.out.u.arr.items[0].u.i, 0 );
	EXPECT_EQ( pairInt.out.u.arr.items[1].u.i, int64_t( int( 0xDeadBeef ) ) );

	EXPECT_CABI_OK( pairString );
	ASSERT_EQ( pairString.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( AsString( pairString.out.u.arr.items[1] ), "0xDeadBeef" );
}
#endif

TEST( CAbiValuesCallbacks, HasValidCallback )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );

	Result valid;
	Call( valid, (TestCallbacks*)callbacks, "HasValidCallback" );
	EXPECT_CABI_OK( valid );
	EXPECT_EQ( valid.Kind(), uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( valid.I(), 0 );
}

TEST_F( CAbiValues, ArgumentsBuiltInC )
{
	BlueCValue args[2];
	BlueCAbiTestCMakeArguments( args, "text", 4, 1.5 );

	Result s, d;
	Call( s, Methods(), "MethodReturningString", { args[0] } );
	Call( d, Methods(), "MethodReturningDouble", { args[1] } );
	EXPECT_CABI_OK( s );
	EXPECT_EQ( s.S(), "text" );
	EXPECT_CABI_OK( d );
	EXPECT_EQ( d.D(), 1.5 );
}

#if BLUE_WITH_PYTHON
// With Python on, BlueScriptCallback is the Python callback: no C-ABI conversion
TEST( CAbiValuesCallbacks, PythonCallbackIsUnsupported )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );

	Result get, set, property;
	Call( get, (TestCallbacks*)callbacks, "GetCallback" );
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { None() } );
	Get( property, (TestCallbacks*)callbacks, "callback" );

	EXPECT_CABI_ERROR( get, BLUE_E_UNSUPPORTED );
	EXPECT_CABI_ERROR( set, BLUE_E_UNSUPPORTED );
	EXPECT_CABI_ERROR( property, BLUE_E_UNSUPPORTED );
	EXPECT_EQ( get.Message(), "result: type BlueScriptCallback has no C-ABI conversion" );
	EXPECT_EQ( set.Message(), "argument 0: type BlueScriptCallback has no C-ABI conversion" );
}

TEST_F( CAbiValues, PyObjectIsUnsupported )
{
	Result r;
	Call( r, Methods(), "MethodReturningPyObject", { None() } );
	EXPECT_CABI_ERROR( r, BLUE_E_UNSUPPORTED );
}
#endif


//--------------------------------------------------------------------
// Objects
//--------------------------------------------------------------------

TEST_F( CAbiValues, ObjectRoundTrip )
{
	TestMultipleInterfaces* obj = m_interfaces;
	const int refs = RefCount( obj );
	{
		Result r;
		Call( r, Methods(), "MethodReturningBlueObject", { Obj( obj ) } );
		EXPECT_CABI_OK( r );
		ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_OBJECT ) );
		EXPECT_EQ( r.O(), reinterpret_cast<IRoot*>( obj ) );
		// The returned value holds one reference; the borrowed argument none
		EXPECT_EQ( RefCount( obj ), refs + 1 );
	}
	EXPECT_EQ( RefCount( obj ), refs );
}

TEST_F( CAbiValues, ObjectNoneAndWrongClass )
{
	Result none, wrongClass, wrongKind, nullObject;
	BlueCValue nullObj = None();
	nullObj.kind = BLUE_VK_OBJECT;

	Call( none, Methods(), "MethodReturningBlueObject", { None() } );
	Call( wrongClass, Methods(), "MethodReturningBlueObject", { Obj( Methods() ) } );
	Call( wrongKind, Methods(), "MethodReturningBlueObject", { Int( 1 ) } );
	Call( nullObject, Methods(), "MethodReturningBlueObject", { nullObj } );

	EXPECT_CABI_OK( none );
	EXPECT_EQ( none.Kind(), uint32_t( BLUE_VK_NONE ) );
	EXPECT_CABI_ERROR( wrongClass, BLUE_E_TYPE );
	EXPECT_NE( wrongClass.Message().find( "TestMethods" ), std::string::npos ) << wrongClass.Message();
	EXPECT_CABI_ERROR( wrongKind, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( nullObject, BLUE_E_INVALID_ARGUMENT );
}


//--------------------------------------------------------------------
// Conversions no test class method uses
//--------------------------------------------------------------------

template< typename T >
static void RoundTrip( const T& value, T& back, uint32_t kind, uint32_t aux = 0 )
{
	BlueCValueHolder holder;
	BlueCError err = { BLUE_OK, nullptr, 0 };
	ASSERT_TRUE( BlueCWrap( value, holder.Get(), &err ) ) << ( err.message ? err.message : "" );
	EXPECT_EQ( holder.Get().kind, kind );
	EXPECT_EQ( holder.Get().aux, aux );
	ASSERT_TRUE( BlueCExtract( holder.Get(), back, 0, &err ) ) << ( err.message ? err.message : "" );
	BlueCErrorClear( &err );
}

TEST( CAbiConvert, Matrix )
{
	Matrix m;
	for( int i = 0; i < 16; ++i )
	{
		( &m._11 )[i] = float( i ) + 0.5f;
	}
	Matrix back = {};
	RoundTrip( m, back, BLUE_VK_VECTOR, 16 | BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_MATRIX );
	EXPECT_EQ( memcmp( &m, &back, sizeof( m ) ), 0 );

	// Four rows, as the Python backend takes it
	std::vector<BlueCValue> rows = {
		Vec( { 1, 0, 0, 0 } ), Vec( { 0, 1, 0, 0 } ), Vec( { 0, 0, 1, 0 } ), Vec( { 5, 6, 7, 1 } )
	};
	Matrix fromRows = {};
	BlueCError err = { BLUE_OK, nullptr, 0 };
	ASSERT_TRUE( BlueCExtract( Array( rows ), fromRows, 0, &err ) );
	EXPECT_EQ( fromRows._11, 1.0f );
	EXPECT_EQ( fromRows._42, 6.0f );

	std::vector<BlueCValue> threeRows = { Vec( { 1, 0, 0, 0 } ), Vec( { 0, 1, 0, 0 } ), Vec( { 0, 0, 1, 0 } ) };
	EXPECT_FALSE( BlueCExtract( Array( threeRows ), fromRows, 0, &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_TYPE ) );
	BlueCErrorClear( &err );
}

TEST( CAbiConvert, TaggedVectors )
{
	Color c = { 0.25f, 0.5f, 0.75f, 1.0f };
	Color cBack = {};
	RoundTrip( c, cBack, BLUE_VK_VECTOR, 4 | BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_COLOR );
	EXPECT_EQ( memcmp( &c, &cBack, sizeof( c ) ), 0 );

	Quaternion q = { 0, 0, 0, 1 };
	Quaternion qBack = {};
	RoundTrip( q, qBack, BLUE_VK_VECTOR, 4 | BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_QUATERNION );
	EXPECT_EQ( memcmp( &q, &qBack, sizeof( q ) ), 0 );

	Vector4 v = { 1, 2, 3, 4 };
	Vector4 vBack = {};
	RoundTrip( v, vBack, BLUE_VK_VECTOR, 4 | BLUE_VEC_ELEMENT_FLOAT );
	EXPECT_EQ( memcmp( &v, &vBack, sizeof( v ) ), 0 );
}

TEST( CAbiConvert, Containers )
{
	std::list<std::string> list = { "a", "", "c" };
	std::list<std::string> listBack;
	RoundTrip( list, listBack, BLUE_VK_ARRAY );
	EXPECT_EQ( list, listBack );

	std::map<int, std::wstring> map = { { 1, L"one" }, { -2, L"æ" } };
	std::map<int, std::wstring> mapBack;
	RoundTrip( map, mapBack, BLUE_VK_MAP );
	EXPECT_EQ( map, mapBack );

	std::pair<int, std::vector<double>> pair( 3, { 1.5, 2.5 } );
	std::pair<int, std::vector<double>> pairBack;
	RoundTrip( pair, pairBack, BLUE_VK_ARRAY );
	EXPECT_EQ( pair, pairBack );

	std::vector<BlueCValue> three = { Int( 1 ), Int( 2 ), Int( 3 ) };
	BlueCError err = { BLUE_OK, nullptr, 0 };
	std::pair<int, int> wrongSize;
	EXPECT_FALSE( BlueCExtract( Array( three ), wrongSize, 0, &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_TYPE ) );
	BlueCErrorClear( &err );
}

TEST( CAbiConvert, Optional )
{
	BlueCValueHolder unassigned, assigned;
	BlueCError err = { BLUE_OK, nullptr, 0 };
	EXPECT_TRUE( BlueCWrap( Be::Optional<int>(), unassigned.Get(), &err ) );
	EXPECT_EQ( unassigned.Get().kind, uint32_t( BLUE_VK_NONE ) );
	EXPECT_TRUE( BlueCWrap( Be::Optional<int>( 9 ), assigned.Get(), &err ) );
	EXPECT_EQ( assigned.Get().kind, uint32_t( BLUE_VK_INT ) );
	EXPECT_EQ( assigned.Get().u.i, 9 );

	Be::Optional<int> back;
	EXPECT_TRUE( BlueCExtract( Int( 4 ), back, 0, &err ) );
	EXPECT_TRUE( back.IsAssigned() );
	EXPECT_EQ( back.GetValue(), 4 );
}

TEST( CAbiConvert, IntegerWidths )
{
	BlueCError err = { BLUE_OK, nullptr, 0 };

	// long and long long are distinct types; both convert (no LP64 overload clash)
	long l = 0;
	long long ll = 0;
	unsigned long ul = 0;
	size_t size = 0;
	int8_t i8 = 0;
	uint8_t u8 = 0;
	char c = 0;
	EXPECT_TRUE( BlueCExtract( Int( -5000000000LL ), l, 0, &err ) );
	EXPECT_EQ( l, -5000000000L );
	EXPECT_TRUE( BlueCExtract( Int( -5000000000LL ), ll, 0, &err ) );
	EXPECT_TRUE( BlueCExtract( UInt( ~uint64_t( 0 ) ), ul, 0, &err ) );
	EXPECT_TRUE( BlueCExtract( UInt( 12 ), size, 0, &err ) );
	EXPECT_TRUE( BlueCExtract( Int( 'x' ), c, 0, &err ) );
	EXPECT_EQ( c, 'x' );
	EXPECT_TRUE( BlueCExtract( Int( -128 ), i8, 0, &err ) );
	EXPECT_FALSE( BlueCExtract( Int( 128 ), i8, 0, &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_OVERFLOW ) );
	EXPECT_FALSE( BlueCExtract( Int( 256 ), u8, 0, &err ) );
	EXPECT_FALSE( BlueCExtract( UInt( ~uint64_t( 0 ) ), ll, 0, &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_OVERFLOW ) );

	BlueCValueHolder wrapped;
	EXPECT_TRUE( BlueCWrap( size_t( 7 ), wrapped.Get(), &err ) );
	EXPECT_EQ( wrapped.Get().kind, uint32_t( BLUE_VK_UINT ) );
	BlueCErrorClear( &err );
}

TEST( CAbiConvert, ReleaseNested )
{
	// A map of string to list of objects: release frees every level and drops the references
	TestMultipleInterfacesPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestMultipleInterfaces* raw = obj;
	const int refs = RefCount( raw );

	std::map<std::string, std::vector<TestMultipleInterfaces*>> value = { { "objects", { raw, raw, nullptr } } };
	BlueCValue out;
	BlueCError err = { BLUE_OK, nullptr, 0 };
	ASSERT_TRUE( BlueCWrap( value, out, &err ) );
	EXPECT_EQ( RefCount( raw ), refs + 2 );
	BlueCValueRelease( &out );
	EXPECT_EQ( RefCount( raw ), refs );
	EXPECT_EQ( out.kind, uint32_t( BLUE_VK_NONE ) );

	// Releasing NONE, and clearing an empty error, are no-ops
	BlueCValueRelease( &out );
	BlueCErrorClear( &err );
	BlueCValueRelease( nullptr );
	BlueCErrorClear( nullptr );
}

TEST( CAbiConvert, KindNames )
{
	EXPECT_STREQ( BlueCKindName( BLUE_VK_MAP ), "map" );
	EXPECT_STREQ( BlueCKindName( BLUE_VK_CALLBACK ), "callback" );
	EXPECT_STREQ( BlueCKindName( BLUE_VK_BYTES ), "bytes" );
	EXPECT_STREQ( BlueCKindName( 99 ), "invalid kind" );
}


//--------------------------------------------------------------------
// Bytes
//--------------------------------------------------------------------

TEST( CAbiBytes, WrapAndExtract )
{
	// Any byte, NUL and invalid UTF-8 included
	const unsigned char data[] = { 0x00, 0xFF, 'a', 0xC0, 0x00, 0x80 };

	BlueCValueHolder holder;
	BlueCError err = { BLUE_OK, nullptr, 0 };
	ASSERT_TRUE( BlueCWrapBytes( data, sizeof( data ), holder.Get(), &err ) );
	const BlueCValue& v = holder.Get();
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_BYTES ) );
	EXPECT_EQ( v.aux, 0u );
	ASSERT_EQ( v.u.str.len, sizeof( data ) );
	EXPECT_NE( static_cast<const void*>( v.u.str.ptr ), static_cast<const void*>( data ) ) << "wrapping copies";
	EXPECT_EQ( memcmp( v.u.str.ptr, data, sizeof( data ) ), 0 );

	// Extract borrows the value's buffer
	const void* back = nullptr;
	size_t length = 0;
	ASSERT_TRUE( BlueCExtractBytes( v, back, length, 0, &err ) );
	EXPECT_EQ( back, static_cast<const void*>( v.u.str.ptr ) );
	EXPECT_EQ( length, sizeof( data ) );

	// Empty, with and without data
	BlueCValueHolder empty, emptyNull;
	ASSERT_TRUE( BlueCWrapBytes( data, 0, empty.Get(), &err ) );
	EXPECT_EQ( empty.Get().kind, uint32_t( BLUE_VK_BYTES ) );
	EXPECT_EQ( empty.Get().u.str.len, size_t( 0 ) );
	ASSERT_TRUE( BlueCWrapBytes( nullptr, 0, emptyNull.Get(), &err ) );
	EXPECT_EQ( emptyNull.Get().kind, uint32_t( BLUE_VK_BYTES ) );
	ASSERT_TRUE( BlueCExtractBytes( Bytes( nullptr, 0 ), back, length, 0, &err ) );
	EXPECT_EQ( back, nullptr );
	EXPECT_EQ( length, size_t( 0 ) );
	EXPECT_EQ( err.code, int32_t( BLUE_OK ) );

	// Null data with a length
	BlueCValueHolder nullData;
	EXPECT_FALSE( BlueCWrapBytes( nullptr, 3, nullData.Get(), &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_INVALID_ARGUMENT ) );
	EXPECT_EQ( nullData.Get().kind, uint32_t( BLUE_VK_NONE ) );
	EXPECT_FALSE( BlueCExtractBytes( Bytes( nullptr, 3 ), back, length, 0, &err ) );
	EXPECT_EQ( err.code, int32_t( BLUE_E_INVALID_ARGUMENT ) );
	BlueCErrorClear( &err );
}

TEST( CAbiBytes, StrictKinds )
{
	// Bytes are not text and text is not bytes, either way round
	const char text[] = "abc";
	const void* data = nullptr;
	size_t length = 0;
	std::string s;
	std::wstring w;

	Result fromString, fromNone, toString, toWString;
	EXPECT_FALSE( BlueCExtractBytes( Str( text ), data, length, 1, &fromString.err ) );
	EXPECT_EQ( fromString.err.code, int32_t( BLUE_E_TYPE ) );
	EXPECT_EQ( fromString.Message(), "argument 1: expected bytes, got string" );
	EXPECT_FALSE( BlueCExtractBytes( None(), data, length, 0, &fromNone.err ) );
	EXPECT_EQ( fromNone.err.code, int32_t( BLUE_E_TYPE ) );
	EXPECT_FALSE( BlueCExtractString( Bytes( text, 3 ), s, 0, &toString.err ) );
	EXPECT_EQ( toString.err.code, int32_t( BLUE_E_TYPE ) );
	EXPECT_FALSE( BlueCExtractWString( Bytes( text, 3 ), w, 0, &toWString.err ) );
	EXPECT_EQ( toWString.err.code, int32_t( BLUE_E_TYPE ) );
}

TEST_F( CAbiValues, BytesAreNotAStringArgument )
{
	const char text[] = "abc";
	Result r;
	Call( r, Methods(), "MethodReturningString", { Bytes( text, 3 ) } );
	EXPECT_CABI_ERROR( r, BLUE_E_TYPE );
}

TEST( CAbiBytes, Allocate )
{
	// Fill in place, then report a short read by lowering the length
	BlueCValue v;
	BlueCError err = { BLUE_OK, nullptr, 0 };
	char* buffer = BlueCAllocateBytes( 8, v, &err );
	ASSERT_NE( buffer, nullptr );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_BYTES ) );
	EXPECT_EQ( static_cast<const char*>( buffer ), v.u.str.ptr );
	EXPECT_EQ( v.u.str.len, size_t( 8 ) );
	memcpy( buffer, "\x01\x02\x03", 3 );
	v.u.str.len = 3;

	const void* data = nullptr;
	size_t length = 0;
	ASSERT_TRUE( BlueCExtractBytes( v, data, length, 0, &err ) );
	EXPECT_EQ( length, size_t( 3 ) );
	EXPECT_EQ( memcmp( data, "\x01\x02\x03", 3 ), 0 );
	BlueCValueRelease( &v );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_NONE ) );

	// Zero bytes still allocate (so a buffer pointer is always returned)
	ASSERT_NE( BlueCAllocateBytes( 0, v, &err ), nullptr );
	EXPECT_EQ( v.u.str.len, size_t( 0 ) );
	BlueCValueRelease( &v );

	// An impossible size fails cleanly
	EXPECT_EQ( BlueCAllocateBytes( std::numeric_limits<size_t>::max(), v, &err ), nullptr );
	EXPECT_EQ( err.code, int32_t( BLUE_E_OUT_OF_MEMORY ) );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_NONE ) );
	BlueCErrorClear( &err );
}

TEST( CAbiBytes, InsideContainersAreReleased )
{
	// An array holding bytes: BlueCValueRelease frees them with the array
	BlueCValue v;
	BlueCValueSetNone( v );
	ASSERT_EQ( BlueCValueSetArray( &v, 2, nullptr ), int32_t( BLUE_OK ) );
	ASSERT_EQ( BlueCValueSetBytes( &v.u.arr.items[0], "\x00\xFF", 2, nullptr ), int32_t( BLUE_OK ) );
	ASSERT_EQ( BlueCValueSetString( &v.u.arr.items[1], "x", 1, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( v.u.arr.items[0].kind, uint32_t( BLUE_VK_BYTES ) );
	BlueCValueRelease( &v );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_NONE ) );
}
