// Copyright (c) 2026 CCP Games
//
// Conversions where the C-ABI backend is stricter than the Python one, or follows it more
// closely than child 1 did:
//   - narrow C++ strings must be UTF-8 to become a STRING (they carry no encoding);
//   - a finite double beyond float's range is BLUE_E_OVERFLOW, not a cast (undefined behaviour);
//   - an array attribute of 16 floats is a matrix only when its IID says so.

#include "TestCAbiUtil.h"
#include "TestAttributes.h"
#include "TestMethods.h"

#include <cfloat>
#include <cmath>
#include <limits>

using namespace CAbiTest;

// Attributes whose VARTYPE no shared test class has (CSTRING, CHARARRAY, Matrix with and
// without its IID); used by the C-ABI tests only
BLUE_CLASS( TestCAbiConversions ) : public IRoot
{
public:
	EXPOSE_TO_BLUE();

	TestCAbiConversions()
	{
		memset( &m_matrix, 0, sizeof( m_matrix ) );
		memset( &m_floats16, 0, sizeof( m_floats16 ) );
		memset( &m_vector, 0, sizeof( m_vector ) );
		memset( m_chars, 0, sizeof( m_chars ) );
	}

	~TestCAbiConversions()
	{
		if( m_cstring )
		{
			CCP_FREE( m_cstring );
		}
	}

	Matrix m_matrix;
	Matrix m_floats16;
	Vector3 m_vector;
	char* m_cstring = nullptr;
	char m_chars[8];
};

TYPEDEF_BLUECLASS( TestCAbiConversions );

BLUE_DEFINE( TestCAbiConversions );

const Be::ClassInfo* TestCAbiConversions::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiConversions, "Attributes for the C-ABI conversion tests" )
		MAP_INTERFACE( TestCAbiConversions )
		MAP_ATTRIBUTE( "matrix", m_matrix, "a Matrix (BlueMatrixIID)", Be::READWRITE )
		MAP_ATTRIBUTE_WITH_IID( "floats16", m_floats16, "16 floats without the matrix IID", Be::READWRITE, nullptr )
		MAP_ATTRIBUTE( "vector", m_vector, "a Vector3", Be::READWRITE )
		MAP_ATTRIBUTE( "cstring", m_cstring, "a CSTRING", Be::READWRITE )
		MAP_ATTRIBUTE( "chars", m_chars, "a CHARARRAY", Be::READWRITE )
	EXPOSURE_END()
}

namespace
{

// Bytes that are not UTF-8, with where the first invalid sequence starts
struct InvalidUtf8
{
	const char* text;
	size_t length;
	size_t offset;
	const char* what;
};

const InvalidUtf8 s_invalidUtf8[] = {
	{ "\xFF", 1, 0, "not a lead byte" },
	{ "ab\x80", 3, 2, "lone continuation byte" },
	{ "\xC0\xAF", 2, 0, "overlong" },
	{ "x\xED\xA0\x80", 4, 1, "surrogate" },
	{ "\xF4\x90\x80\x80", 4, 0, "past U+10FFFF" },
	{ "ok\xE2\x82", 4, 2, "truncated" },
};

class CAbiConversions : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE( m_methods.CreateInstance() );
		ASSERT_TRUE( m_attributes.CreateInstance() );
		ASSERT_TRUE( m_conversions.CreateInstance() );
	}

	TestMethods* Methods() { return m_methods; }
	TestAttributes* Attributes() { return m_attributes; }
	TestCAbiConversions* Conversions() { return m_conversions; }

	TestMethodsPtr m_methods;
	TestAttributesPtr m_attributes;
	TestCAbiConversionsPtr m_conversions;
};

} // namespace


//--------------------------------------------------------------------
// Narrow strings and UTF-8
//--------------------------------------------------------------------

TEST( CAbiUtf8, IsUtf8 )
{
	const std::string valid = std::string( "a\0b", 3 ) + "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9D\x84\x9E \xF4\x8F\xBF\xBF";
	EXPECT_TRUE( BlueCIsUtf8( valid.data(), valid.size(), nullptr ) );
	EXPECT_TRUE( BlueCIsUtf8( nullptr, 0, nullptr ) );

	for( const InvalidUtf8& bad : s_invalidUtf8 )
	{
		size_t offset = 99;
		EXPECT_FALSE( BlueCIsUtf8( bad.text, bad.length, &offset ) ) << bad.what;
		EXPECT_EQ( offset, bad.offset ) << bad.what;
		EXPECT_FALSE( BlueCIsUtf8( bad.text, bad.length, nullptr ) ) << bad.what;
	}
}

TEST_F( CAbiConversions, NarrowReturnsMustBeUtf8 )
{
	// Arguments are taken as given; the strings blue hands back must be UTF-8
	for( const InvalidUtf8& bad : s_invalidUtf8 )
	{
		SCOPED_TRACE( bad.what );
		Result s, constChar, shared;
		Call( s, Methods(), "MethodReturningString", { Str( bad.text, bad.length ) } );
		Call( constChar, Methods(), "MethodReturningConstChar", { Str( bad.text, bad.length ) } );
		Call( shared, Methods(), "ConvertStringToSharedString", { Str( bad.text, bad.length ) } );
		EXPECT_CABI_ERROR( s, BLUE_E_INVALID_ARGUMENT );
		EXPECT_CABI_ERROR( constChar, BLUE_E_INVALID_ARGUMENT );
		EXPECT_CABI_ERROR( shared, BLUE_E_INVALID_ARGUMENT );
		EXPECT_EQ( s.Message(), "string is not valid UTF-8 (byte " + std::to_string( bad.offset ) + " of " + std::to_string( bad.length ) + ")" );
	}

	const std::string unicode = "\xC3\xBE\xC3\xA6\xE2\x82\xAC";
	Result ok;
	Call( ok, Methods(), "MethodReturningString", { Str( unicode ) } );
	EXPECT_CABI_OK( ok );
	EXPECT_EQ( ok.S(), unicode );
}

TEST_F( CAbiConversions, NarrowAttributesMustBeUtf8 )
{
	const char bad[] = "a\xFF";
	const char* names[] = { "myString", "sharedString" };
	for( const char* name : names )
	{
		SCOPED_TRACE( name );
		SetOk( Attributes(), name, Str( bad, 2 ) );
		Result r;
		Get( r, Attributes(), name );
		EXPECT_CABI_ERROR( r, BLUE_E_INVALID_ARGUMENT );
	}

	const char* cNames[] = { "cstring", "chars" };
	for( const char* name : cNames )
	{
		SCOPED_TRACE( name );
		Result good;
		SetOk( Conversions(), name, Str( "\xC3\xA6" ) );
		Get( good, Conversions(), name );
		EXPECT_CABI_OK( good ) << name;
		EXPECT_EQ( good.S(), "\xC3\xA6" ) << name;

		Result r;
		SetOk( Conversions(), name, Str( bad, 2 ) );
		Get( r, Conversions(), name );
		EXPECT_CABI_ERROR( r, BLUE_E_INVALID_ARGUMENT );
	}
}

TEST( CAbiUtf8, ValueSetString )
{
	Result bad, good;
	bad.status = BlueCValueSetString( &bad.out, "\xC0\xAF", 2, &bad.err );
	EXPECT_CABI_ERROR( bad, BLUE_E_INVALID_ARGUMENT );

	good.status = BlueCValueSetString( &good.out, "\xE2\x82\xAC", 3, &good.err );
	EXPECT_CABI_OK( good );
	EXPECT_EQ( good.S(), "\xE2\x82\xAC" );

	// Binary data goes as bytes, unchecked
	Result bytes;
	bytes.status = BlueCValueSetBytes( &bytes.out, "\xC0\xAF", 2, &bytes.err );
	EXPECT_CABI_OK( bytes );
	EXPECT_EQ( bytes.Kind(), uint32_t( BLUE_VK_BYTES ) );
}


//--------------------------------------------------------------------
// double -> float
//--------------------------------------------------------------------

TEST_F( CAbiConversions, FloatOverflow )
{
	Result over, under, max, inf, nan, tiny, fromInt, asDouble;
	Call( over, Methods(), "MethodReturningFloat", { Double( 1e39 ) } );
	Call( under, Methods(), "MethodReturningFloat", { Double( -1e39 ) } );
	Call( max, Methods(), "MethodReturningFloat", { Double( FLT_MAX ) } );
	Call( inf, Methods(), "MethodReturningFloat", { Double( -std::numeric_limits<double>::infinity() ) } );
	Call( nan, Methods(), "MethodReturningFloat", { Double( std::numeric_limits<double>::quiet_NaN() ) } );
	Call( tiny, Methods(), "MethodReturningFloat", { Double( 1e-50 ) } );
	Call( fromInt, Methods(), "MethodReturningFloat", { UInt( ~uint64_t( 0 ) ) } );
	Call( asDouble, Methods(), "MethodReturningDouble", { Double( 1e300 ) } );

	EXPECT_CABI_ERROR( over, BLUE_E_OVERFLOW );
	EXPECT_EQ( over.Message(), "argument 0: 1e+39 does not fit in float" );
	EXPECT_CABI_ERROR( under, BLUE_E_OVERFLOW );
	EXPECT_CABI_OK( max );
	EXPECT_EQ( max.D(), double( FLT_MAX ) );
	// inf and NaN pass unchanged, and underflow to zero is not an error
	EXPECT_CABI_OK( inf );
	EXPECT_EQ( inf.D(), -std::numeric_limits<double>::infinity() );
	EXPECT_CABI_OK( nan );
	EXPECT_TRUE( std::isnan( nan.D() ) );
	EXPECT_CABI_OK( tiny );
	EXPECT_EQ( tiny.D(), 0.0 );
	EXPECT_CABI_OK( fromInt );
	EXPECT_CABI_OK( asDouble );
	EXPECT_EQ( asDouble.D(), 1e300 );
}

TEST_F( CAbiConversions, FloatAttributeOverflowLeavesTheValue )
{
	SetOk( Attributes(), "myFloat", Double( 0.5 ) );
	Result set, get;
	Set( set, Attributes(), "myFloat", Double( 1e39 ) );
	EXPECT_EQ( set.status, int32_t( BLUE_E_OVERFLOW ) ) << Describe( set );
	EXPECT_EQ( set.Message(), "value: 1e+39 does not fit in float" );
	Get( get, Attributes(), "myFloat" );
	EXPECT_EQ( get.D(), 0.5 );
}

TEST_F( CAbiConversions, VectorOverflow )
{
	Result over;
	Call( over, Methods(), "MethodReturningVector3", { Vec( { 1, -1e39, 0 } ) } );
	EXPECT_CABI_ERROR( over, BLUE_E_OVERFLOW );
	EXPECT_EQ( over.Message(), "argument 0: component 1 (-1e+39) does not fit in float" );

	// Double components are not narrowed
	Vector3d d = {};
	Result direct;
	EXPECT_TRUE( BlueCExtract( Vec( { 1, -1e39, 0 }, BLUE_VEC_ELEMENT_DOUBLE ), d, 0, &direct.err ) ) << direct.Message();
	EXPECT_EQ( d.y, -1e39 );

	// An attribute is assigned only when every component fits
	SetOk( Conversions(), "vector", Vec( { 1, 2, 3 } ) );
	Result set, get;
	Set( set, Conversions(), "vector", Vec( { 4, 5, 1e39 } ) );
	EXPECT_EQ( set.status, int32_t( BLUE_E_OVERFLOW ) ) << Describe( set );
	Get( get, Conversions(), "vector" );
	ASSERT_EQ( get.Kind(), uint32_t( BLUE_VK_VECTOR ) );
	EXPECT_EQ( get.out.u.vec.v[0], 1.0 );
	EXPECT_EQ( get.out.u.vec.v[2], 3.0 );
}

TEST_F( CAbiConversions, MatrixOverflow )
{
	std::vector<BlueCValue> items( 16, Double( 1 ) );
	items[5] = Double( 1e39 );

	Matrix m = {};
	Result direct;
	EXPECT_FALSE( BlueCExtract( Array( items ), m, 0, &direct.err ) );
	EXPECT_EQ( direct.err.code, int32_t( BLUE_E_OVERFLOW ) );
	EXPECT_EQ( direct.Message(), "argument 0: component 5 (1e+39) does not fit in float" );

	Result set;
	Set( set, Conversions(), "matrix", Array( items ) );
	EXPECT_EQ( set.status, int32_t( BLUE_E_OVERFLOW ) ) << Describe( set );
	EXPECT_EQ( Conversions()->m_matrix._12, 0.0f ) << "a failed set leaves the matrix alone";
}


//--------------------------------------------------------------------
// 16-float arrays: a matrix by IID only
//--------------------------------------------------------------------

TEST_F( CAbiConversions, MatrixAttribute )
{
	std::vector<BlueCValue> rows = {
		Vec( { 1, 0, 0, 0 } ), Vec( { 0, 1, 0, 0 } ), Vec( { 0, 0, 1, 0 } ), Vec( { 5, 6, 7, 1 } )
	};
	SetOk( Conversions(), "matrix", Array( rows ) );

	Result r;
	Get( r, Conversions(), "matrix" );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.Kind(), uint32_t( BLUE_VK_VECTOR ) );
	EXPECT_EQ( r.out.aux, 16u | BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_MATRIX );
	ASSERT_EQ( r.out.u.arr.count, size_t( 16 ) );
	EXPECT_EQ( r.out.u.arr.items[13].u.d, 6.0 );
}

TEST_F( CAbiConversions, SixteenFloatsWithoutTheMatrixIID )
{
	for( int i = 0; i < 16; ++i )
	{
		( &Conversions()->m_floats16._11 )[i] = float( i );
	}

	// Read as a plain ARRAY of 16 numbers
	Result r;
	Get( r, Conversions(), "floats16" );
	EXPECT_CABI_OK( r );
	ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_ARRAY ) );
	EXPECT_EQ( r.out.aux, 0u );
	ASSERT_EQ( r.out.u.arr.count, size_t( 16 ) );
	EXPECT_EQ( r.out.u.arr.items[15].kind, uint32_t( BLUE_VK_DOUBLE ) );
	EXPECT_EQ( r.out.u.arr.items[15].u.d, 15.0 );

	// Assigned like any vector: up to 16 numbers, the rest zero; matrix rows are not accepted
	std::vector<BlueCValue> three = { Double( 7 ), Double( 8 ), Double( 9 ) };
	SetOk( Conversions(), "floats16", Array( three ) );
	EXPECT_EQ( Conversions()->m_floats16._11, 7.0f );
	EXPECT_EQ( Conversions()->m_floats16._13, 9.0f );
	EXPECT_EQ( Conversions()->m_floats16._14, 0.0f );
	EXPECT_EQ( Conversions()->m_floats16._44, 0.0f );

	std::vector<BlueCValue> rows = {
		Vec( { 1, 0, 0, 0 } ), Vec( { 0, 1, 0, 0 } ), Vec( { 0, 0, 1, 0 } ), Vec( { 0, 0, 0, 1 } )
	};
	Result set;
	Set( set, Conversions(), "floats16", Array( rows ) );
	EXPECT_EQ( set.status, int32_t( BLUE_E_TYPE ) ) << Describe( set );
}
