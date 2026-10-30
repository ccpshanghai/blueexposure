// Copyright (c) 2026 CCP Games
//
// C-ABI backend: the non-template half of BlueCAbiValue.h - errors, value lifetime, and the
// conversions of numbers, strings, vectors and objects.

#include "include/BlueCAbiValue.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <new>

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

std::string BlueCDemangle( const char* typeidName )
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
	BlueCSetArgumentError( err, BLUE_E_UNSUPPORTED, argIndex, "type %s has no C-ABI conversion", BlueCDemangle( typeName ).c_str() );
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
	case BLUE_VK_CALLBACK:
		return "callback";
	case BLUE_VK_BYTES:
		return "bytes";
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

int32_t BlueCSetErrorMessage( BlueCError* err, int32_t code, const char* message, size_t length )
{
	if( !err )
	{
		return code;
	}
	BlueCErrorClear( err );
	err->code = code;
	if( !message )
	{
		return code;
	}
	char* copy = static_cast<char*>( CCP_MALLOC( "BlueCError", length + 1 ) );
	if( !copy )
	{
		return code;
	}
	if( length )
	{
		memcpy( copy, message, length );
	}
	copy[length] = 0;
	err->message = copy;
	err->message_len = length;
	return code;
}

static int32_t BlueCWrapUtf8( const char* value, size_t length, BlueCValue& out, BlueCError* err );

int32_t BlueCValueSetString( BlueCValue* out, const char* utf8, size_t length, BlueCError* err )
{
	if( !out )
	{
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null value" );
	}
	if( !utf8 && length )
	{
		BlueCValueSetNone( *out );
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null string of %zu bytes", length );
	}
	return BlueCWrapUtf8( utf8, length, *out, err );
}

int32_t BlueCValueSetBytes( BlueCValue* out, const void* data, size_t length, BlueCError* err )
{
	if( !out )
	{
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null value" );
	}
	if( !data && length )
	{
		BlueCValueSetNone( *out );
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null data of %zu bytes", length );
	}
	return BlueCWrapBytes( data, length, *out, err ) ? int32_t( BLUE_OK ) : int32_t( BLUE_E_OUT_OF_MEMORY );
}

// out as an ARRAY / MAP over itemCount freshly allocated NONE values (a MAP's are key, value, ...)
static int32_t BlueCValueSetItems( BlueCValue* out, uint32_t kind, size_t itemCount, BlueCError* err )
{
	if( !out )
	{
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null value" );
	}
	BlueCValueSetNone( *out );
	BlueCValue* items = nullptr;
	if( itemCount )
	{
		items = BlueCAllocateValues( itemCount, err );
		if( !items )
		{
			return BLUE_E_OUT_OF_MEMORY;
		}
	}
	out->kind = kind;
	if( kind == BLUE_VK_MAP )
	{
		out->u.map.kv = items;
		out->u.map.count = itemCount / 2;
	}
	else
	{
		out->u.arr.items = items;
		out->u.arr.count = itemCount;
	}
	return BLUE_OK;
}

int32_t BlueCValueSetArray( BlueCValue* out, size_t count, BlueCError* err )
{
	return BlueCValueSetItems( out, BLUE_VK_ARRAY, count, err );
}

int32_t BlueCValueSetMap( BlueCValue* out, size_t pairCount, BlueCError* err )
{
	if( pairCount > std::numeric_limits<size_t>::max() / 2 )
	{
		if( out )
		{
			BlueCValueSetNone( *out );
		}
		return BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "too many pairs: %zu", pairCount );
	}
	return BlueCValueSetItems( out, BLUE_VK_MAP, 2 * pairCount, err );
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
	case BLUE_VK_BYTES:
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
	case BLUE_VK_CALLBACK:
		BlueCCallbackRelease( value->u.callback );
		break;
	default:
		break;
	}

	BlueCValueSetNone( *value );
}


//--------------------------------------------------------------------
// Callback handles
//--------------------------------------------------------------------

struct BlueCallbackHandle
{
	BlueCCallback callback;
	std::atomic<uint32_t> refCount;
};

BlueCallbackHandle* BlueCCallbackCreate( const BlueCCallback* callback, BlueCError* err )
{
	if( !callback || !callback->fn )
	{
		BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "callback without a function" );
		return nullptr;
	}
	void* memory = CCP_MALLOC( "BlueCallbackHandle", sizeof( BlueCallbackHandle ) );
	if( !memory )
	{
		BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "out of memory allocating a callback" );
		return nullptr;
	}
	BlueCallbackHandle* handle = new( memory ) BlueCallbackHandle;
	handle->callback = *callback;
	handle->refCount.store( 1, std::memory_order_relaxed );
	return handle;
}

void BlueCCallbackAddRef( BlueCallbackHandle* handle )
{
	if( handle )
	{
		handle->refCount.fetch_add( 1, std::memory_order_relaxed );
	}
}

void BlueCCallbackRelease( BlueCallbackHandle* handle )
{
	if( !handle || handle->refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
	{
		return;
	}
	if( handle->callback.destroy )
	{
		handle->callback.destroy( handle->callback.user_data );
	}
	handle->~BlueCallbackHandle();
	CCP_FREE( handle );
}

const BlueCCallback* BlueCCallbackGet( const BlueCallbackHandle* handle )
{
	return handle ? &handle->callback : nullptr;
}

int32_t BlueCCallbackInvoke( BlueCallbackHandle* handle, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	if( out )
	{
		BlueCValueSetNone( *out );
	}
	if( !handle )
	{
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null callback handle" );
	}

	// fn always gets an out and an err of its own, so it may write them unconditionally
	BlueCValue result;
	BlueCValueSetNone( result );
	BlueCError error = { BLUE_OK, nullptr, 0 };
	int32_t status = BLUE_OK;
	try
	{
		status = handle->callback.fn( handle->callback.user_data, args, argc, &result, &error );
	}
	catch( ... )
	{
		// A C++ host; anything else cannot throw through a C function pointer
		status = BLUE_E_EXCEPTION;
		BlueCSetError( &error, status, "callback threw a C++ exception" );
	}

	if( status == BLUE_OK )
	{
		BlueCErrorClear( &error );
		if( out )
		{
			*out = result;
		}
		else
		{
			BlueCValueRelease( &result );
		}
		return BLUE_OK;
	}

	BlueCValueRelease( &result );
	if( status > 0 )
	{
		status = BLUE_E_FAILED;
	}
	if( !err )
	{
		BlueCErrorClear( &error );
		return status;
	}
	BlueCErrorClear( err );
	if( error.message )
	{
		// Keep the callback's message as it is; the returned status is the code
		*err = error;
		err->code = status;
	}
	else
	{
		BlueCErrorClear( &error );
		BlueCSetError( err, status, "callback failed with status %d", int( status ) );
	}
	return status;
}

void BlueCWrapCallback( BlueCallbackHandle* handle, BlueCValue& out )
{
	BlueCValueSetNone( out );
	if( handle )
	{
		BlueCCallbackAddRef( handle );
		out.kind = BLUE_VK_CALLBACK;
		out.u.callback = handle;
	}
}

bool BlueCExtractCallback( const BlueCValue& value, BlueCallbackHandle*& result, unsigned argIndex, BlueCError* err )
{
	switch( value.kind )
	{
	case BLUE_VK_NONE:
		result = nullptr;
		return true;
	case BLUE_VK_CALLBACK:
		if( !value.u.callback )
		{
			BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "callback value with a null pointer (use none)" );
			return false;
		}
		result = value.u.callback;
		return true;
	default:
		return BlueCTypeMismatch( err, argIndex, "callback", value );
	}
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

bool BlueCCheckFloatRange( const double* values, size_t count, unsigned argIndex, BlueCError* err )
{
	for( size_t i = 0; i < count; ++i )
	{
		// Finite only: inf and NaN convert to float unchanged; underflow to 0 is not an error
		const double v = values[i];
		if( std::isfinite( v ) && std::fabs( v ) > double( std::numeric_limits<float>::max() ) )
		{
			if( count == 1 )
			{
				BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "%g does not fit in float", v );
			}
			else
			{
				BlueCSetArgumentError( err, BLUE_E_OVERFLOW, argIndex, "component %zu (%g) does not fit in float", i, v );
			}
			return false;
		}
	}
	return true;
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

// Decode the code point at s[i] (i < length) into c and step i past it. Strict: overlong
// forms, surrogates, values past U+10FFFF and truncated sequences are rejected.
static bool BlueCDecodeUtf8( const unsigned char* s, size_t length, size_t& i, uint32_t& c )
{
	c = s[i];
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
	return true;
}

bool BlueCIsUtf8( const char* text, size_t length, size_t* invalidOffset )
{
	const unsigned char* s = reinterpret_cast<const unsigned char*>( text );
	size_t i = 0;
	while( i < length )
	{
		// Runs of ASCII are the common case
		if( s[i] < 0x80 )
		{
			++i;
			continue;
		}
		const size_t start = i;
		uint32_t c = 0;
		if( !BlueCDecodeUtf8( s, length, i, c ) )
		{
			if( invalidOffset )
			{
				*invalidOffset = start;
			}
			return false;
		}
	}
	return true;
}

bool BlueCUtf8ToWide( const char* utf8, size_t length, std::wstring& result )
{
	result.clear();
	result.reserve( length );
	const unsigned char* s = reinterpret_cast<const unsigned char*>( utf8 );
	size_t i = 0;
	while( i < length )
	{
		uint32_t c = 0;
		if( !BlueCDecodeUtf8( s, length, i, c ) )
		{
			return false;
		}

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

// out as a STRING or BYTES owning a buffer of length + 1 bytes, the last one NUL (bytes share
// the string storage). Returns the buffer, or null with out NONE and err filled.
static char* BlueCAllocateStringStorage( uint32_t kind, size_t length, BlueCValue& out, BlueCError* err )
{
	BlueCValueSetNone( out );
	char* buffer = length < std::numeric_limits<size_t>::max() ? static_cast<char*>( CCP_MALLOC( "BlueCValue/string", length + 1 ) ) : nullptr;
	if( !buffer )
	{
		BlueCSetError( err, BLUE_E_OUT_OF_MEMORY, "out of memory copying %s of %zu bytes", kind == BLUE_VK_BYTES ? "bytes" : "a string", length );
		return nullptr;
	}
	buffer[length] = 0;
	out.kind = kind;
	out.u.str.ptr = buffer;
	out.u.str.len = length;
	return buffer;
}

static bool BlueCCopyStringStorage( uint32_t kind, const char* data, size_t length, BlueCValue& out, BlueCError* err )
{
	char* buffer = BlueCAllocateStringStorage( kind, length, out, err );
	if( !buffer )
	{
		return false;
	}
	if( length )
	{
		memcpy( buffer, data, length );
	}
	return true;
}

// BlueCWrapString as a status: the C ABI's strings are UTF-8, so narrow C++ strings (which
// carry no encoding) are checked, as Python's str conversion checks them
static int32_t BlueCWrapUtf8( const char* value, size_t length, BlueCValue& out, BlueCError* err )
{
	size_t invalidOffset = 0;
	if( !BlueCIsUtf8( value, length, &invalidOffset ) )
	{
		BlueCValueSetNone( out );
		return BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "string is not valid UTF-8 (byte %zu of %zu)", invalidOffset, length );
	}
	return BlueCCopyStringStorage( BLUE_VK_STRING, value, length, out, err ) ? int32_t( BLUE_OK ) : int32_t( BLUE_E_OUT_OF_MEMORY );
}

bool BlueCWrapString( const char* value, size_t length, BlueCValue& out, BlueCError* err )
{
	return BlueCWrapUtf8( value, length, out, err ) == BLUE_OK;
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
	// Valid by construction
	return BlueCCopyStringStorage( BLUE_VK_STRING, utf8.data(), utf8.size(), out, err );
}


//--------------------------------------------------------------------
// Bytes
//--------------------------------------------------------------------

bool BlueCWrapBytes( const void* data, size_t length, BlueCValue& out, BlueCError* err )
{
	if( !data && length )
	{
		BlueCValueSetNone( out );
		BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "null data of %zu bytes", length );
		return false;
	}
	return BlueCCopyStringStorage( BLUE_VK_BYTES, static_cast<const char*>( data ), length, out, err );
}

char* BlueCAllocateBytes( size_t length, BlueCValue& out, BlueCError* err )
{
	return BlueCAllocateStringStorage( BLUE_VK_BYTES, length, out, err );
}

bool BlueCExtractBytes( const BlueCValue& value, const void*& data, size_t& length, unsigned argIndex, BlueCError* err )
{
	if( value.kind != BLUE_VK_BYTES )
	{
		return BlueCTypeMismatch( err, argIndex, "bytes", value );
	}
	if( !value.u.str.ptr && value.u.str.len )
	{
		BlueCSetArgumentError( err, BLUE_E_INVALID_ARGUMENT, argIndex, "bytes with a null pointer and length %zu", value.u.str.len );
		return false;
	}
	data = value.u.str.ptr;
	length = value.u.str.len;
	return true;
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

	if( !BlueCCheckFloatRange( components, 16, argIndex, err ) )
	{
		return false;
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
