// Copyright (c) 2026 CCP Games
//
// C-ABI backend: the non-template half of BlueCAbiValue.h - errors, value lifetime, and the
// conversions of numbers, strings, vectors and objects.

#include "include/BlueCAbiValue.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#if defined( __GNUC__ ) || defined( __clang__ )
#include <cxxabi.h>
#endif


//--------------------------------------------------------------------
// Errors
//--------------------------------------------------------------------

static int32_t BlueCSetErrorV( BlueCError* err, int32_t code, const char* prefix, const char* format, va_list args )
{
	if( !err )
	{
		return code;
	}

	BlueCErrorClear( err );
	err->code = code;

	va_list measure;
	va_copy( measure, args );
	const int bodyLength = vsnprintf( nullptr, 0, format, measure );
	va_end( measure );
	if( bodyLength < 0 )
	{
		return code;
	}

	const size_t prefixLength = prefix ? strlen( prefix ) : 0;
	const size_t length = prefixLength + size_t( bodyLength );
	char* message = static_cast<char*>( CCP_MALLOC( "BlueCError", length + 1 ) );
	if( !message )
	{
		// The code alone still says what went wrong
		return code;
	}
	if( prefixLength )
	{
		memcpy( message, prefix, prefixLength );
	}
	vsnprintf( message + prefixLength, size_t( bodyLength ) + 1, format, args );

	err->message = message;
	err->message_len = length;
	return code;
}

int32_t BlueCSetError( BlueCError* err, int32_t code, const char* format, ... )
{
	va_list args;
	va_start( args, format );
	BlueCSetErrorV( err, code, nullptr, format, args );
	va_end( args );
	return code;
}

int32_t BlueCSetArgumentError( BlueCError* err, int32_t code, unsigned argIndex, const char* format, ... )
{
	char prefix[32];
	if( argIndex == BLUE_CABI_VALUE_INDEX )
	{
		snprintf( prefix, sizeof( prefix ), "value: " );
	}
	else if( argIndex == BLUE_CABI_RESULT_INDEX )
	{
		snprintf( prefix, sizeof( prefix ), "result: " );
	}
	else
	{
		snprintf( prefix, sizeof( prefix ), "argument %u: ", argIndex );
	}

	va_list args;
	va_start( args, format );
	BlueCSetErrorV( err, code, prefix, format, args );
	va_end( args );
	return code;
}

bool BlueCTypeMismatch( BlueCError* err, unsigned argIndex, const char* expected, const BlueCValue& value )
{
	BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected %s, got %s", expected, BlueCKindName( value.kind ) );
	return false;
}

std::string BlueCTypeName( const char* typeidName )
{
#if defined( __GNUC__ ) || defined( __clang__ )
	int status = 0;
	char* demangled = abi::__cxa_demangle( typeidName, nullptr, nullptr, &status );
	if( demangled )
	{
		std::string name( demangled );
		free( demangled );
		return name;
	}
#endif
	return typeidName ? typeidName : "";
}

bool BlueCUnsupportedType( BlueCError* err, unsigned argIndex, const char* typeName )
{
	BlueCSetArgumentError( err, BLUE_E_UNSUPPORTED, argIndex, "type %s has no C-ABI conversion", BlueCTypeName( typeName ).c_str() );
	return false;
}

void BlueCErrorClear( BlueCError* err )
{
	if( !err )
	{
		return;
	}
	if( err->message )
	{
		CCP_FREE( err->message );
	}
	err->code = BLUE_OK;
	err->message = nullptr;
	err->message_len = 0;
}

const char* BlueCKindName( uint32_t kind )
{
	switch( kind )
	{
	case BLUE_VK_NONE:
		return "none";
	case BLUE_VK_BOOL:
		return "bool";
	case BLUE_VK_INT:
		return "int";
	case BLUE_VK_UINT:
		return "uint";
	case BLUE_VK_DOUBLE:
		return "double";
	case BLUE_VK_STRING:
		return "string";
	case BLUE_VK_OBJECT:
		return "object";
	case BLUE_VK_VECTOR:
		return "vector";
	case BLUE_VK_ARRAY:
		return "array";
	case BLUE_VK_MAP:
		return "map";
	default:
		return "invalid kind";
	}
}

const char* BlueCClassNameOf( IRoot* obj )
{
	if( !obj )
	{
		return "None";
	}
	const Be::ClassInfo* type = obj->ClassType();
	if( !type || !type->mClassId || !type->mClassId->GetName() )
	{
		return "<unknown>";
	}
	return type->mClassId->GetName();
}


//--------------------------------------------------------------------
// Value storage
//--------------------------------------------------------------------

BlueCValue* BlueCAllocateValues( size_t count, BlueCError* err )
{
	if( count > std::numeric_limits<size_t>::max() / sizeof( BlueCValue ) )
	{
		BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "too many values: %zu", count );
		return nullptr;
	}
	const size_t size = count ? count * sizeof( BlueCValue ) : sizeof( BlueCValue );
	BlueCValue* values = static_cast<BlueCValue*>( CCP_MALLOC( "BlueCValue", size ) );
	if( !values )
	{
		BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "out of memory allocating %zu values", count );
		return nullptr;
	}
	memset( values, 0, size );
	return values;
}

void BlueCFreeValues( BlueCValue* values )
{
	if( values )
	{
		CCP_FREE( values );
	}
}

static void BlueCReleaseValues( BlueCValue* values, size_t count )
{
	if( !values )
	{
		return;
	}
	for( size_t i = 0; i < count; ++i )
	{
		BlueCValueRelease( &values[i] );
	}
	BlueCFreeValues( values );
}

void BlueCValueRelease( BlueCValue* value )
{
	if( !value )
	{
		return;
	}

	switch( value->kind )
	{
	case BLUE_VK_STRING:
		if( value->u.str.ptr )
		{
			CCP_FREE( const_cast<char*>( value->u.str.ptr ) );
		}
		break;
	case BLUE_VK_OBJECT:
		if( value->u.obj )
		{
			reinterpret_cast<IRoot*>( value->u.obj )->Unlock();
		}
		break;
	case BLUE_VK_VECTOR:
		if( ( value->aux & BLUE_VEC_COUNT_MASK ) > 4 )
		{
			BlueCReleaseValues( value->u.arr.items, value->u.arr.count );
		}
		break;
	case BLUE_VK_ARRAY:
		BlueCReleaseValues( value->u.arr.items, value->u.arr.count );
		break;
	case BLUE_VK_MAP:
		BlueCReleaseValues( value->u.map.kv, 2 * value->u.map.count );
		break;
	default:
		break;
	}

	BlueCValueSetNone( *value );
}


//--------------------------------------------------------------------
// Numbers
//--------------------------------------------------------------------

bool BlueCExtractBool( const BlueCValue& value, bool& result, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_BOOL:
	case BLUE_VK_INT:
		result = value.u.i != 0;
		return true;
	case BLUE_VK_UINT:
		result = value.u.u != 0;
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, "bool", value );
	}
}

bool BlueCExtractSigned( const BlueCValue& value, int64_t& result, int64_t minValue, int64_t maxValue, const char* typeName, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_BOOL:
	case BLUE_VK_INT:
		if( value.u.i < minValue || value.u.i > maxValue )
		{
			BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "%lld does not fit in [%lld, %lld]",
								   (long long)value.u.i, (long long)minValue, (long long)maxValue );
			return false;
		}
		result = value.u.i;
		return true;
	case BLUE_VK_UINT:
		if( maxValue < 0 || value.u.u > uint64_t( maxValue ) )
		{
			BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "%llu does not fit in [%lld, %lld]",
								   (unsigned long long)value.u.u, (long long)minValue, (long long)maxValue );
			return false;
		}
		result = int64_t( value.u.u );
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, typeName, value );
	}
}

bool BlueCExtractUnsigned( const BlueCValue& value, uint64_t& result, uint64_t maxValue, const char* typeName, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_BOOL:
	case BLUE_VK_INT:
		if( value.u.i < 0 || uint64_t( value.u.i ) > maxValue )
		{
			BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "%lld does not fit in [0, %llu]",
								   (long long)value.u.i, (unsigned long long)maxValue );
			return false;
		}
		result = uint64_t( value.u.i );
		return true;
	case BLUE_VK_UINT:
		if( value.u.u > maxValue )
		{
			BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "%llu does not fit in [0, %llu]",
								   (unsigned long long)value.u.u, (unsigned long long)maxValue );
			return false;
		}
		result = value.u.u;
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, typeName, value );
	}
}

bool BlueCExtractDouble( const BlueCValue& value, double& result, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_DOUBLE:
		result = value.u.d;
		return true;
	case BLUE_VK_INT:
		result = double( value.u.i );
		return true;
	case BLUE_VK_UINT:
		result = double( value.u.u );
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, "number", value );
	}
}

void BlueCWrapBool( bool value, BlueCValue& out )
{
	BlueCValueSetNone( out );
	out.kind = BLUE_VK_BOOL;
	out.u.i = value ? 1 : 0;
}

void BlueCWrapInt( int64_t value, BlueCValue& out )
{
	BlueCValueSetNone( out );
	out.kind = BLUE_VK_INT;
	out.u.i = value;
}

void BlueCWrapUInt( uint64_t value, BlueCValue& out )
{
	BlueCValueSetNone( out );
	out.kind = BLUE_VK_UINT;
	out.u.u = value;
}

void BlueCWrapDouble( double value, BlueCValue& out )
{
	BlueCValueSetNone( out );
	out.kind = BLUE_VK_DOUBLE;
	out.u.d = value;
}


//--------------------------------------------------------------------
// Strings
//--------------------------------------------------------------------

bool BlueCUtf8ToWide( const char* utf8, size_t length, std::wstring& result )
{
	result.clear();
	result.reserve( length );
	const unsigned char* s = reinterpret_cast<const unsigned char*>( utf8 );
	size_t i = 0;
	while( i < length )
	{
		uint32_t c = s[i];
		size_t extra = 0;
		uint32_t minValue = 0;
		if( c < 0x80 )
		{
			extra = 0;
		}
		else if( ( c & 0xE0 ) == 0xC0 )
		{
			c &= 0x1F;
			extra = 1;
			minValue = 0x80;
		}
		else if( ( c & 0xF0 ) == 0xE0 )
		{
			c &= 0x0F;
			extra = 2;
			minValue = 0x800;
		}
		else if( ( c & 0xF8 ) == 0xF0 )
		{
			c &= 0x07;
			extra = 3;
			minValue = 0x10000;
		}
		else
		{
			return false;
		}
		if( length - i <= extra )
		{
			return false;
		}
		for( size_t k = 1; k <= extra; ++k )
		{
			const uint32_t next = s[i + k];
			if( ( next & 0xC0 ) != 0x80 )
			{
				return false;
			}
			c = ( c << 6 ) | ( next & 0x3F );
		}
		// Overlong forms, surrogates and values past U+10FFFF are not UTF-8
		if( c < minValue || c > 0x10FFFF || ( c >= 0xD800 && c <= 0xDFFF ) )
		{
			return false;
		}
		i += extra + 1;

		if( sizeof( wchar_t ) == 2 && c >= 0x10000 )
		{
			c -= 0x10000;
			result.push_back( wchar_t( 0xD800 + ( c >> 10 ) ) );
			result.push_back( wchar_t( 0xDC00 + ( c & 0x3FF ) ) );
		}
		else
		{
			result.push_back( wchar_t( c ) );
		}
	}
	return true;
}

bool BlueCWideToUtf8( const wchar_t* wide, size_t length, std::string& result )
{
	result.clear();
	result.reserve( length );
	for( size_t i = 0; i < length; ++i )
	{
		uint32_t c = uint32_t( wide[i] );
		if( sizeof( wchar_t ) == 2 )
		{
			c &= 0xFFFF;
			if( c >= 0xD800 && c <= 0xDBFF )
			{
				if( i + 1 >= length )
				{
					return false;
				}
				const uint32_t low = uint32_t( wide[i + 1] ) & 0xFFFF;
				if( low < 0xDC00 || low > 0xDFFF )
				{
					return false;
				}
				c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( low - 0xDC00 );
				++i;
			}
		}
		if( ( c >= 0xD800 && c <= 0xDFFF ) || c > 0x10FFFF )
		{
			return false;
		}

		if( c < 0x80 )
		{
			result.push_back( char( c ) );
		}
		else if( c < 0x800 )
		{
			result.push_back( char( 0xC0 | ( c >> 6 ) ) );
			result.push_back( char( 0x80 | ( c & 0x3F ) ) );
		}
		else if( c < 0x10000 )
		{
			result.push_back( char( 0xE0 | ( c >> 12 ) ) );
			result.push_back( char( 0x80 | ( ( c >> 6 ) & 0x3F ) ) );
			result.push_back( char( 0x80 | ( c & 0x3F ) ) );
		}
		else
		{
			result.push_back( char( 0xF0 | ( c >> 18 ) ) );
			result.push_back( char( 0x80 | ( ( c >> 12 ) & 0x3F ) ) );
			result.push_back( char( 0x80 | ( ( c >> 6 ) & 0x3F ) ) );
			result.push_back( char( 0x80 | ( c & 0x3F ) ) );
		}
	}
	return true;
}

static bool BlueCCheckString( const BlueCValue& value, unsigned argIndex, BlueCError* err )
{
	if( value.kind != BLUE_VK_STRING )
	{
		return BlueCTypeMismatch( err, argIndex, "string", value );
	}
	if( !value.u.str.ptr && value.u.str.len )
	{
		BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "string with a null pointer and length %zu", value.u.str.len );
		return false;
	}
	return true;
}

bool BlueCExtractString( const BlueCValue& value, std::string& result, unsigned argIndex, BlueCError* err )
{
	if( !BlueCCheckString( value, argIndex, err ) )
	{
		return false;
	}
	result.assign( value.u.str.ptr ? value.u.str.ptr : "", value.u.str.len );
	return true;
}

bool BlueCExtractWString( const BlueCValue& value, std::wstring& result, unsigned argIndex, BlueCError* err )
{
	if( !BlueCCheckString( value, argIndex, err ) )
	{
		return false;
	}
	if( !BlueCUtf8ToWide( value.u.str.ptr ? value.u.str.ptr : "", value.u.str.len, result ) )
	{
		BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "string is not valid UTF-8" );
		return false;
	}
	return true;
}

bool BlueCExtractSharedString( const BlueCValue& value, BlueSharedString& result, unsigned argIndex, BlueCError* err )
{
	std::string s;
	if( !BlueCExtractString( value, s, argIndex, err ) )
	{
		return false;
	}
	result = BlueSharedString( s );
	return true;
}

bool BlueCExtractSharedString( const BlueCValue& value, BlueSharedStringW& result, unsigned argIndex, BlueCError* err )
{
	std::wstring s;
	if( !BlueCExtractWString( value, s, argIndex, err ) )
	{
		return false;
	}
	result = BlueSharedStringW( s );
	return true;
}

bool BlueCWrapString( const char* value, size_t length, BlueCValue& out, BlueCError* err )
{
	BlueCValueSetNone( out );
	char* copy = static_cast<char*>( CCP_MALLOC( "BlueCValue/string", length + 1 ) );
	if( !copy )
	{
		BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "out of memory copying a string of %zu bytes", length );
		return false;
	}
	if( length )
	{
		memcpy( copy, value, length );
	}
	copy[length] = 0;
	out.kind = BLUE_VK_STRING;
	out.u.str.ptr = copy;
	out.u.str.len = length;
	return true;
}

bool BlueCWrapWString( const wchar_t* value, size_t length, BlueCValue& out, BlueCError* err )
{
	BlueCValueSetNone( out );
	std::string utf8;
	if( !BlueCWideToUtf8( value, length, utf8 ) )
	{
		BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "wide string is not valid Unicode" );
		return false;
	}
	return BlueCWrapString( utf8.data(), utf8.size(), out, err );
}


//--------------------------------------------------------------------
// Vectors and matrices
//--------------------------------------------------------------------

bool BlueCExpectKind( const BlueCValue& value, uint32_t kind, const char* expected, unsigned argIndex, BlueCError* err )
{
	if( value.kind != kind )
	{
		return BlueCTypeMismatch( err, argIndex, expected, value );
	}
	if( ( kind == BLUE_VK_ARRAY && value.u.arr.count && !value.u.arr.items ) ||
		( kind == BLUE_VK_MAP && value.u.map.count && !value.u.map.kv ) )
	{
		BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "%s with a null item pointer", expected );
		return false;
	}
	return true;
}

// The components of a VECTOR value, wherever they are stored
static bool BlueCGetVectorComponents( const BlueCValue& value, double* elements, size_t capacity, size_t& count, unsigned argIndex, BlueCError* err )
{
	count = value.aux & BLUE_VEC_COUNT_MASK;
	if( count > capacity )
	{
		BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected at most %zu components, got %zu", capacity, count );
		return false;
	}
	if( count <= 4 )
	{
		for( size_t i = 0; i < count; ++i )
		{
			elements[i] = value.u.vec.v[i];
		}
		return true;
	}
	if( value.u.arr.count != count || !value.u.arr.items )
	{
		BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "vector of %zu components holds %zu items", count, value.u.arr.count );
		return false;
	}
	for( size_t i = 0; i < count; ++i )
	{
		if( !BlueCExtractDouble( value.u.arr.items[i], elements[i], argIndex, err ) )
		{
			return false;
		}
	}
	return true;
}

bool BlueCExtractComponents( const BlueCValue& value, double* elements, size_t count, bool integral, const char* typeName, unsigned argIndex, BlueCError* err )
{
	for( size_t i = 0; i < count; ++i )
	{
		elements[i] = 0;
	}

	size_t given = 0;
	if( value.kind == BLUE_VK_VECTOR )
	{
		if( !BlueCGetVectorComponents( value, elements, count, given, argIndex, err ) )
		{
			return false;
		}
	}
	else if( value.kind == BLUE_VK_ARRAY )
	{
		if( !BlueCExpectKind( value, BLUE_VK_ARRAY, typeName, argIndex, err ) )
		{
			return false;
		}
		given = value.u.arr.count;
		if( given > count )
		{
			BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected at most %zu components, got %zu", count, given );
			return false;
		}
		for( size_t i = 0; i < given; ++i )
		{
			if( !BlueCExtractDouble( value.u.arr.items[i], elements[i], argIndex, err ) )
			{
				return false;
			}
		}
	}
	else
	{
		return BlueCTypeMismatch( err, argIndex, typeName, value );
	}

	if( integral )
	{
		for( size_t i = 0; i < given; ++i )
		{
			const double c = elements[i];
			if( c != std::floor( c ) )
			{
				BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "component %zu (%g) is not an integer", i, c );
				return false;
			}
			if( c < double( std::numeric_limits<int32_t>::min() ) || c > double( std::numeric_limits<int32_t>::max() ) )
			{
				BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "component %zu (%g) does not fit in int32", i, c );
				return false;
			}
		}
	}
	return true;
}

bool BlueCExtractMatrix( const BlueCValue& value, float* elements16, unsigned argIndex, BlueCError* err )
{
	double components[16] = {};

	if( value.kind == BLUE_VK_VECTOR || ( value.kind == BLUE_VK_ARRAY && value.u.arr.count == 16 ) )
	{
		if( !BlueCExtractComponents( value, components, 16, false, "matrix", argIndex, err ) )
		{
			return false;
		}
		const size_t given = value.kind == BLUE_VK_VECTOR ? ( value.aux & BLUE_VEC_COUNT_MASK ) : value.u.arr.count;
		if( given != 16 )
		{
			BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected a matrix of 16 components, got %zu", given );
			return false;
		}
	}
	else if( value.kind == BLUE_VK_ARRAY )
	{
		if( !BlueCExpectKind( value, BLUE_VK_ARRAY, "matrix", argIndex, err ) )
		{
			return false;
		}
		if( value.u.arr.count != 4 )
		{
			BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected a matrix of 4 rows or 16 components, got %zu items", value.u.arr.count );
			return false;
		}
		for( size_t row = 0; row < 4; ++row )
		{
			if( !BlueCExtractComponents( value.u.arr.items[row], components + 4 * row, 4, false, "matrix row", argIndex, err ) )
			{
				return false;
			}
		}
	}
	else
	{
		return BlueCTypeMismatch( err, argIndex, "matrix", value );
	}

	for( size_t i = 0; i < 16; ++i )
	{
		elements16[i] = float( components[i] );
	}
	return true;
}

void BlueCWrapComponents( const double* elements, uint32_t count, uint32_t elementAndTag, BlueCValue& out )
{
	CCP_ASSERT( count <= 4 );
	BlueCValueSetNone( out );
	out.kind = BLUE_VK_VECTOR;
	out.aux = ( count & BLUE_VEC_COUNT_MASK ) | ( elementAndTag & ~BLUE_VEC_COUNT_MASK );
	for( uint32_t i = 0; i < count && i < 4; ++i )
	{
		out.u.vec.v[i] = elements[i];
	}
}

bool BlueCWrapMatrix( const float* elements16, BlueCValue& out, BlueCError* err )
{
	BlueCValueSetNone( out );
	BlueCValue* items = BlueCAllocateValues( 16, err );
	if( !items )
	{
		return false;
	}
	for( size_t i = 0; i < 16; ++i )
	{
		BlueCWrapDouble( double( elements16[i] ), items[i] );
	}
	out.kind = BLUE_VK_VECTOR;
	out.aux = 16 | BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_MATRIX;
	out.u.arr.items = items;
	out.u.arr.count = 16;
	return true;
}


//--------------------------------------------------------------------
// Objects
//--------------------------------------------------------------------

bool BlueCExtractObject( const BlueCValue& value, IRoot*& result, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_NONE:
		result = nullptr;
		return true;
	case BLUE_VK_OBJECT:
		if( !value.u.obj )
		{
			BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "object value with a null pointer (use none)" );
			return false;
		}
		result = reinterpret_cast<IRoot*>( value.u.obj );
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, "object", value );
	}
}

void BlueCWrapObject( IRoot* obj, BlueCValue& out )
{
	BlueCValueSetNone( out );
	if( !obj )
	{
		return;
	}
	obj->Lock();
	out.kind = BLUE_VK_OBJECT;
	out.u.obj = reinterpret_cast<BlueObject*>( obj );
}
