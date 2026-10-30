// Copyright (c) 2026 CCP Games
//
// C-ABI backend: conversion between C++ values and BlueCValue.
//
//   BlueCExtract( const BlueCValue&, T&, argIndex, BlueCError* )   value -> C++ (borrowed input)
//   BlueCWrap( const T&, BlueCValue&, BlueCError* )                 C++ -> value (owned output)
//
// Both forward to BlueCConvert<T>, which is specialised per type; a module adds a C++ type by
// specialising BlueCConvert. A type without a specialisation still compiles, so every exposed
// method gets a thunk, but converting it fails at run time with BLUE_E_UNSUPPORTED (PyObject*,
// for example). Conversions never throw on bad input: a kind or range mismatch fills the
// BlueCError and returns false.
//
// Mapping (C++ type -> kind / aux):
//   bool                               BOOL
//   signed integers, enums             INT    (range-checked on extract)
//   unsigned integers                  UINT   (range-checked on extract)
//   float, double                      DOUBLE (INT and UINT are accepted on extract; a finite
//                                      value beyond float's range is BLUE_E_OVERFLOW for float)
//   std::string, const char*,
//   BlueSharedString                   STRING (must be UTF-8 on wrap: BLUE_E_INVALID_ARGUMENT)
//   std::wstring, const wchar_t*,
//   BlueSharedStringW                  STRING (UTF-8 <-> wchar_t)
//   Vector2/3/4, Color, Quaternion     VECTOR count | FLOAT [| COLOR / QUATERNION]
//   Vector2d/3d/4d                     VECTOR count | DOUBLE
//   Vector3i                           VECTOR 3 | INT32
//   Matrix                             VECTOR 16 | FLOAT | MATRIX, components in u.arr
//   pointer to a Blue class/interface  OBJECT (+1 on wrap, borrowed on extract), NONE for null
//   BlueScriptCallback                 CALLBACK (+1 on wrap, blue's own reference on extract),
//                                      NONE for no callback; only with C-ABI as the only backend
//   std::vector, std::list, std::pair  ARRAY
//   std::map                           MAP
//   Be::Optional<T>                    as T (NONE when unassigned, on wrap)
//
// No C++ type maps to BYTES: binary data is wrapped and extracted explicitly with
// BlueCWrapBytes / BlueCAllocateBytes / BlueCExtractBytes (hand-written thunks, such as the
// IBlueStream methods). Extracting accepts only BYTES, and STRING only STRING.
//
// On extract a vector also accepts an ARRAY of numbers, and like the Python backend accepts
// fewer components than the type has (the rest are zero). A matrix also accepts an ARRAY of 16
// numbers or of four 4-component rows.

#pragma once
#ifndef BlueCAbiValue_h
#define BlueCAbiValue_h

#include "BlueCAbi.h"
#include "BlueTypes.h"
#include "BlueTypeTraits.h"
#include "BlueVectorTypes.h"
#include "BlueSharedString.h"
#include "BlueExtractArgument.h"
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

// Argument indices used in messages for the value of a property or attribute assignment, and
// for a returned value
#define BLUE_CABI_VALUE_INDEX 0xFFFFFFFFu
#define BLUE_CABI_RESULT_INDEX 0xFFFFFFFEu


//--------------------------------------------------------------------
// Errors and value lifetime
//--------------------------------------------------------------------

// Fill err (if not null) with code and a printf-style message, replacing any previous
// message. Returns code.
BLUEIMPORT int32_t BlueCSetError( BlueCError* err, int32_t code, const char* format, ... )
#if defined( __GNUC__ ) || defined( __clang__ )
	__attribute__( ( format( printf, 3, 4 ) ) )
#endif
	;

// As BlueCSetError, prefixed with "argument <n>: " ("value: " for BLUE_CABI_VALUE_INDEX,
// "result: " for BLUE_CABI_RESULT_INDEX)
BLUEIMPORT int32_t BlueCSetArgumentError( BlueCError* err, int32_t code, unsigned argIndex, const char* format, ... )
#if defined( __GNUC__ ) || defined( __clang__ )
	__attribute__( ( format( printf, 4, 5 ) ) )
#endif
	;

// Report that value has the wrong kind for the expected C++ type. Returns false.
BLUEIMPORT bool BlueCTypeMismatch( BlueCError* err, unsigned argIndex, const char* expected, const BlueCValue& value );

// Report that type (a typeid name, or any readable name) has no C-ABI conversion. Returns false.
BLUEIMPORT bool BlueCUnsupportedType( BlueCError* err, unsigned argIndex, const char* typeName );

// Readable form of a typeid name, for messages
BLUEIMPORT std::string BlueCTypeName( const char* typeidName );

// Free err's message and reset it to BLUE_OK
BLUEIMPORT void BlueCErrorClear( BlueCError* err );

// Free everything a value owns (strings, bytes, items, object references) and reset it to NONE.
// Only for values produced by blue (BlueCWrap, thunk outputs) or built with its allocation
// helpers (BlueCValueSetString / Bytes / Array / Map, as host callbacks return them).
BLUEIMPORT void BlueCValueRelease( BlueCValue* value );

// Name of a BlueCValueKind, for messages
BLUEIMPORT const char* BlueCKindName( uint32_t kind );

inline void BlueCValueSetNone( BlueCValue& value )
{
	memset( &value, 0, sizeof( value ) );
}

// Owns a BlueCValue until Detach(); releases it otherwise, including when an exception passes
class BlueCValueHolder
{
public:
	BlueCValueHolder() { BlueCValueSetNone( m_value ); }
	~BlueCValueHolder() { BlueCValueRelease( &m_value ); }
	BlueCValueHolder( const BlueCValueHolder& ) = delete;
	BlueCValueHolder& operator=( const BlueCValueHolder& ) = delete;

	BlueCValue& Get() { return m_value; }
	void Detach( BlueCValue& out )
	{
		out = m_value;
		BlueCValueSetNone( m_value );
	}

private:
	BlueCValue m_value;
};


//--------------------------------------------------------------------
// Non-template conversions (BlueCAbiValue.cpp)
//--------------------------------------------------------------------

BLUEIMPORT bool BlueCExtractBool( const BlueCValue& value, bool& result, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractSigned( const BlueCValue& value, int64_t& result, int64_t minValue, int64_t maxValue, const char* typeName, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractUnsigned( const BlueCValue& value, uint64_t& result, uint64_t maxValue, const char* typeName, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractDouble( const BlueCValue& value, double& result, unsigned argIndex, BlueCError* err );

// Whether values[0, count) can be narrowed to float: a finite value beyond +-FLT_MAX fails with
// BLUE_E_OVERFLOW (the Python backend's cast is undefined there); inf and NaN pass unchanged
BLUEIMPORT bool BlueCCheckFloatRange( const double* values, size_t count, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractString( const BlueCValue& value, std::string& result, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractWString( const BlueCValue& value, std::wstring& result, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractSharedString( const BlueCValue& value, BlueSharedString& result, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractSharedString( const BlueCValue& value, BlueSharedStringW& result, unsigned argIndex, BlueCError* err );

// Extract up to count components into elements (zero-filling the rest). With integral set,
// every component must be a whole number within int32 range.
BLUEIMPORT bool BlueCExtractComponents( const BlueCValue& value, double* elements, size_t count, bool integral, const char* typeName, unsigned argIndex, BlueCError* err );
BLUEIMPORT bool BlueCExtractMatrix( const BlueCValue& value, float* elements16, unsigned argIndex, BlueCError* err );

// IRoot* from OBJECT, or null from NONE
BLUEIMPORT bool BlueCExtractObject( const BlueCValue& value, IRoot*& result, unsigned argIndex, BlueCError* err );

BLUEIMPORT void BlueCWrapBool( bool value, BlueCValue& out );
BLUEIMPORT void BlueCWrapInt( int64_t value, BlueCValue& out );
BLUEIMPORT void BlueCWrapUInt( uint64_t value, BlueCValue& out );
BLUEIMPORT void BlueCWrapDouble( double value, BlueCValue& out );
// STRING holding a NUL-terminated copy of value[0, length); fails with BLUE_E_INVALID_ARGUMENT
// unless it is valid UTF-8 (narrow C++ strings carry no encoding; binary data is BYTES)
BLUEIMPORT bool BlueCWrapString( const char* value, size_t length, BlueCValue& out, BlueCError* err );
BLUEIMPORT bool BlueCWrapWString( const wchar_t* value, size_t length, BlueCValue& out, BlueCError* err );

// BYTES holding a copy of data[0, length) (data may be null when length is 0)
BLUEIMPORT bool BlueCWrapBytes( const void* data, size_t length, BlueCValue& out, BlueCError* err );

// BYTES of length uninitialised bytes, for the caller to fill; returns the buffer, or null (out
// is then NONE, err filled). The caller may lower out.u.str.len afterwards (a short read), but
// never raise it.
BLUEIMPORT char* BlueCAllocateBytes( size_t length, BlueCValue& out, BlueCError* err );

// The data of a BYTES value, borrowed from it (null when its length is 0 and it has no pointer).
// Only BYTES is accepted; a STRING is a type error, as text is not binary data.
BLUEIMPORT bool BlueCExtractBytes( const BlueCValue& value, const void*& data, size_t& length, unsigned argIndex, BlueCError* err );
BLUEIMPORT void BlueCWrapComponents( const double* elements, uint32_t count, uint32_t elementAndTag, BlueCValue& out );
BLUEIMPORT bool BlueCWrapMatrix( const float* elements16, BlueCValue& out, BlueCError* err );

// OBJECT holding one reference to obj (obj is locked), or NONE for null
BLUEIMPORT void BlueCWrapObject( IRoot* obj, BlueCValue& out );

// CALLBACK holding one reference to handle (added), or NONE for null
BLUEIMPORT void BlueCWrapCallback( BlueCallbackHandle* handle, BlueCValue& out );

// The handle of a CALLBACK (borrowed), or null from NONE
BLUEIMPORT bool BlueCExtractCallback( const BlueCValue& value, BlueCallbackHandle*& result, unsigned argIndex, BlueCError* err );

// count NONE values, or null with BLUE_E_OUT_OF_MEMORY in err. Freed with BlueCFreeValues
// (the holder of an ARRAY / MAP does this in BlueCValueRelease).
BLUEIMPORT BlueCValue* BlueCAllocateValues( size_t count, BlueCError* err );
BLUEIMPORT void BlueCFreeValues( BlueCValue* values );

//--------------------------------------------------------------------
// Callback handles (BlueCAbi.h, BlueCCallback)
//--------------------------------------------------------------------

// A handle holding one reference, around a copy of callback. Fails (null, err filled) when
// callback or its fn is null, or out of memory; destroy is then not called and the host still
// owns user_data.
BLUEIMPORT BlueCallbackHandle* BlueCCallbackCreate( const BlueCCallback* callback, BlueCError* err );
BLUEIMPORT void BlueCCallbackAddRef( BlueCallbackHandle* handle );
// Drops one reference; the last one calls destroy( user_data ) and frees the handle
BLUEIMPORT void BlueCCallbackRelease( BlueCallbackHandle* handle );
// The callback the handle was created from, so a host can recognise its own
BLUEIMPORT const BlueCCallback* BlueCCallbackGet( const BlueCallbackHandle* handle );
// Calls fn( user_data, args, argc, out, err ) as BlueCAbi.h describes; args stay owned by the
// caller. Returns fn's status (a positive one as BLUE_E_FAILED, a null handle as
// BLUE_E_INVALID_ARGUMENT, an exception escaping fn as BLUE_E_EXCEPTION). On success out (when
// not null) holds the returned value, owned by the caller; a null out releases it. On failure
// out is NONE, the returned value has been released, and err (when not null) holds fn's message,
// or one naming the status when fn gave none.
BLUEIMPORT int32_t BlueCCallbackInvoke( BlueCallbackHandle* handle, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err );

//--------------------------------------------------------------------
// Building values and errors for blue (host callbacks, BlueCAbi.h)
//
// What a host callback returns is freed by blue, so its allocations must be blue's. These
// overwrite out / err without releasing what was there; the Blue C API re-exports them.
//--------------------------------------------------------------------

// Set err to code with a copy of message[0, length) (UTF-8, need not be NUL-terminated); a null
// message sets the code alone. Returns code. Out of memory keeps the code and drops the message.
BLUEIMPORT int32_t BlueCSetErrorMessage( BlueCError* err, int32_t code, const char* message, size_t length );

// STRING holding a NUL-terminated copy of utf8[0, length). Returns BLUE_OK, or an error (out is
// then NONE): BLUE_E_INVALID_ARGUMENT for a null out, a null utf8 with a length, or text that is
// not valid UTF-8; BLUE_E_OUT_OF_MEMORY.
BLUEIMPORT int32_t BlueCValueSetString( BlueCValue* out, const char* utf8, size_t length, BlueCError* err );

// BYTES holding a copy of data[0, length) (binary, not NUL-terminated by contract). Errors as
// BlueCValueSetString, for a null data with a length too.
BLUEIMPORT int32_t BlueCValueSetBytes( BlueCValue* out, const void* data, size_t length, BlueCError* err );

// ARRAY of count NONE items, for the caller to fill in place. Errors as BlueCValueSetString.
BLUEIMPORT int32_t BlueCValueSetArray( BlueCValue* out, size_t count, BlueCError* err );

// MAP of pairCount (NONE, NONE) pairs, for the caller to fill in place (kv[2n] key, kv[2n + 1]
// value). Errors as BlueCValueSetString.
BLUEIMPORT int32_t BlueCValueSetMap( BlueCValue* out, size_t pairCount, BlueCError* err );

// Whether text[0, length) is valid UTF-8 (strict, as below; NUL is allowed). On failure,
// invalidOffset (optional) receives the offset of the first invalid sequence.
BLUEIMPORT bool BlueCIsUtf8( const char* text, size_t length, size_t* invalidOffset );

// Strict UTF-8 <-> wchar_t (UTF-32, or UTF-16 where wchar_t is 16 bits)
BLUEIMPORT bool BlueCUtf8ToWide( const char* utf8, size_t length, std::wstring& result );
BLUEIMPORT bool BlueCWideToUtf8( const wchar_t* wide, size_t length, std::string& result );


//--------------------------------------------------------------------
// Argument storage
//--------------------------------------------------------------------

// Storage for a const char* argument: a value's string is not NUL-terminated, so it is copied
class BlueCStringArg : public std::string
{
public:
	BlueCStringArg() {}
	operator const char*() const { return c_str(); }
};

// BlueGetNullValue for an omitted optional const char* argument
inline void BlueGetNullValue( BlueCStringArg& resultRef )
{
	resultRef = BlueCStringArg();
}

// The type a thunk extracts an argument of type T into: T without const and reference, with
// C strings held by a string object (wide strings as in the Python backend)
template< typename T >
struct BlueCArgStorageImpl
{
	typedef typename remap_wchar_to_string<T>::type type;
};

template<>
struct BlueCArgStorageImpl< const char* >
{
	typedef BlueCStringArg type;
};

template< typename T >
struct BlueCArgStorage
{
	typedef typename BlueCArgStorageImpl<typename remove_const_and_reference<T>::type>::type type;
};


//--------------------------------------------------------------------
// BlueCConvert
//--------------------------------------------------------------------

// Fallback: no C-ABI representation
template< typename T, typename Enable = void >
struct BlueCConvert
{
	static const bool supported = false;

	static bool Extract( const BlueCValue&, T&, unsigned argIndex, BlueCError* err )
	{
		return BlueCUnsupportedType( err, argIndex, typeid( T ).name() );
	}

	static bool Wrap( const T&, BlueCValue&, BlueCError* err )
	{
		return BlueCUnsupportedType( err, BLUE_CABI_RESULT_INDEX, typeid( T ).name() );
	}
};

template< typename T >
bool BlueCExtract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
{
	return BlueCConvert<T>::Extract( value, result, argIndex, err );
}

template< typename T >
bool BlueCWrap( const T& value, BlueCValue& out, BlueCError* err )
{
	return BlueCConvert<T>::Wrap( value, out, err );
}

template< typename T >
struct BlueCIsSupported : std::integral_constant<bool, BlueCConvert<T>::supported>
{
};

template<>
struct BlueCConvert< bool >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, bool& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractBool( value, result, argIndex, err );
	}

	static bool Wrap( bool value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapBool( value, out );
		return true;
	}
};

// All signed integer types, char and long included. Being one template, it has none of the
// int64_t / long overload clashes the Python backend has to guard against.
template< typename T >
struct BlueCConvert< T, typename std::enable_if< std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value >::type >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
	{
		int64_t v = 0;
		if( !BlueCExtractSigned( value, v, std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), "integer", argIndex, err ) )
		{
			return false;
		}
		result = T( v );
		return true;
	}

	static bool Wrap( T value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapInt( int64_t( value ), out );
		return true;
	}
};

template< typename T >
struct BlueCConvert< T, typename std::enable_if< std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value >::type >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
	{
		uint64_t v = 0;
		if( !BlueCExtractUnsigned( value, v, std::numeric_limits<T>::max(), "unsigned integer", argIndex, err ) )
		{
			return false;
		}
		result = T( v );
		return true;
	}

	static bool Wrap( T value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapUInt( uint64_t( value ), out );
		return true;
	}
};

template< typename T >
struct BlueCConvert< T, typename std::enable_if< std::is_enum<T>::value >::type >
{
	static const bool supported = true;
	typedef typename std::underlying_type<T>::type Underlying;

	static bool Extract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
	{
		Underlying v{};
		if( !BlueCConvert<Underlying>::Extract( value, v, argIndex, err ) )
		{
			return false;
		}
		result = T( v );
		return true;
	}

	// Enums are signed integers on the script side, whatever the underlying type
	static bool Wrap( T value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapInt( int64_t( value ), out );
		return true;
	}
};

template< typename T >
struct BlueCConvert< T, typename std::enable_if< std::is_floating_point<T>::value >::type >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
	{
		double v = 0;
		if( !BlueCExtractDouble( value, v, argIndex, err ) )
		{
			return false;
		}
		if constexpr( sizeof( T ) < sizeof( double ) )
		{
			if( !BlueCCheckFloatRange( &v, 1, argIndex, err ) )
			{
				return false;
			}
		}
		result = T( v );
		return true;
	}

	static bool Wrap( T value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapDouble( double( value ), out );
		return true;
	}
};


//--------------------------------------------------------------------
// Strings
//--------------------------------------------------------------------

template<>
struct BlueCConvert< std::string >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, std::string& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractString( value, result, argIndex, err );
	}

	static bool Wrap( const std::string& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapString( value.data(), value.size(), out, err );
	}
};

template<>
struct BlueCConvert< BlueCStringArg >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, BlueCStringArg& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractString( value, result, argIndex, err );
	}

	static bool Wrap( const BlueCStringArg& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapString( value.data(), value.size(), out, err );
	}
};

// Returned C strings are copied; null becomes NONE. A const char* that is not an argument
// (inside a container, say) has no storage to point into, so it cannot be extracted.
template< typename CharPointer >
struct BlueCConvertCString
{
	static const bool supported = true;

	static bool Extract( const BlueCValue&, CharPointer&, unsigned argIndex, BlueCError* err )
	{
		return BlueCUnsupportedType( err, argIndex, "const char* (only as a direct argument)" );
	}

	static bool Wrap( const char* value, BlueCValue& out, BlueCError* err )
	{
		if( !value )
		{
			BlueCValueSetNone( out );
			return true;
		}
		return BlueCWrapString( value, strlen( value ), out, err );
	}
};

template<> struct BlueCConvert< const char* > : BlueCConvertCString< const char* > {};
template<> struct BlueCConvert< char* > : BlueCConvertCString< char* > {};

template<>
struct BlueCConvert< std::wstring >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, std::wstring& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractWString( value, result, argIndex, err );
	}

	static bool Wrap( const std::wstring& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapWString( value.data(), value.size(), out, err );
	}
};

template<>
struct BlueCConvert< CastableStdWstring >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, CastableStdWstring& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractWString( value, result, argIndex, err );
	}

	static bool Wrap( const CastableStdWstring& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapWString( value.data(), value.size(), out, err );
	}
};

template< typename WCharPointer >
struct BlueCConvertWString
{
	static const bool supported = true;

	static bool Extract( const BlueCValue&, WCharPointer&, unsigned argIndex, BlueCError* err )
	{
		return BlueCUnsupportedType( err, argIndex, "const wchar_t* (only as a direct argument)" );
	}

	static bool Wrap( const wchar_t* value, BlueCValue& out, BlueCError* err )
	{
		if( !value )
		{
			BlueCValueSetNone( out );
			return true;
		}
		return BlueCWrapWString( value, wcslen( value ), out, err );
	}
};

template<> struct BlueCConvert< const wchar_t* > : BlueCConvertWString< const wchar_t* > {};
template<> struct BlueCConvert< wchar_t* > : BlueCConvertWString< wchar_t* > {};

template<>
struct BlueCConvert< BlueSharedString >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, BlueSharedString& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractSharedString( value, result, argIndex, err );
	}

	static bool Wrap( const BlueSharedString& value, BlueCValue& out, BlueCError* err )
	{
		const char* s = value.c_str();
		return BlueCWrapString( s ? s : "", s ? strlen( s ) : 0, out, err );
	}
};

template<>
struct BlueCConvert< BlueSharedStringW >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, BlueSharedStringW& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractSharedString( value, result, argIndex, err );
	}

	static bool Wrap( const BlueSharedStringW& value, BlueCValue& out, BlueCError* err )
	{
		const wchar_t* s = value.c_str();
		return BlueCWrapWString( s ? s : L"", s ? wcslen( s ) : 0, out, err );
	}
};


//--------------------------------------------------------------------
// Vector types
//--------------------------------------------------------------------

// A struct of N components of type Element, laid out as an array (BlueVectorTypes.h requires
// this of overridden vector types too)
template< typename Vector, typename Element, uint32_t N, uint32_t ElementAndTag >
struct BlueCConvertVector
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, Vector& result, unsigned argIndex, BlueCError* err )
	{
		double components[N];
		const bool integral = std::is_integral<Element>::value;
		if( !BlueCExtractComponents( value, components, N, integral, "vector", argIndex, err ) )
		{
			return false;
		}
		if constexpr( std::is_floating_point<Element>::value && sizeof( Element ) < sizeof( double ) )
		{
			if( !BlueCCheckFloatRange( components, N, argIndex, err ) )
			{
				return false;
			}
		}
		Element* elements = reinterpret_cast<Element*>( &result );
		for( uint32_t i = 0; i < N; ++i )
		{
			elements[i] = Element( components[i] );
		}
		return true;
	}

	static bool Wrap( const Vector& value, BlueCValue& out, BlueCError* )
	{
		const Element* elements = reinterpret_cast<const Element*>( &value );
		double components[N];
		for( uint32_t i = 0; i < N; ++i )
		{
			components[i] = double( elements[i] );
		}
		BlueCWrapComponents( components, N, ElementAndTag, out );
		return true;
	}
};

template<> struct BlueCConvert< Vector2 > : BlueCConvertVector< Vector2, float, 2, BLUE_VEC_ELEMENT_FLOAT > {};
template<> struct BlueCConvert< Vector3 > : BlueCConvertVector< Vector3, float, 3, BLUE_VEC_ELEMENT_FLOAT > {};
template<> struct BlueCConvert< Vector4 > : BlueCConvertVector< Vector4, float, 4, BLUE_VEC_ELEMENT_FLOAT > {};
template<> struct BlueCConvert< Vector2d > : BlueCConvertVector< Vector2d, double, 2, BLUE_VEC_ELEMENT_DOUBLE > {};
template<> struct BlueCConvert< Vector3d > : BlueCConvertVector< Vector3d, double, 3, BLUE_VEC_ELEMENT_DOUBLE > {};
template<> struct BlueCConvert< Vector4d > : BlueCConvertVector< Vector4d, double, 4, BLUE_VEC_ELEMENT_DOUBLE > {};
template<> struct BlueCConvert< Vector3i > : BlueCConvertVector< Vector3i, int, 3, BLUE_VEC_ELEMENT_INT32 > {};
template<> struct BlueCConvert< Color > : BlueCConvertVector< Color, float, 4, BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_COLOR > {};
template<> struct BlueCConvert< Quaternion > : BlueCConvertVector< Quaternion, float, 4, BLUE_VEC_ELEMENT_FLOAT | BLUE_VEC_TAG_QUATERNION > {};

template<>
struct BlueCConvert< Matrix >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, Matrix& result, unsigned argIndex, BlueCError* err )
	{
		return BlueCExtractMatrix( value, &result._11, argIndex, err );
	}

	static bool Wrap( const Matrix& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapMatrix( &value._11, out, err );
	}
};


//--------------------------------------------------------------------
// Blue objects
//--------------------------------------------------------------------

// Name of obj's class, for messages
BLUEIMPORT const char* BlueCClassNameOf( IRoot* obj );

// Pointer to a Blue class or interface. Extract casts the IRoot* to the expected type the way
// BluePythonCast does (dynamic_cast); wrap reinterprets the pointer as IRoot*, as the Python
// backend does (IRoot is first in every branch of the inheritance graph), and adds a reference.
template< typename T >
struct BlueCConvert< T, typename std::enable_if< is_pointer_to_blue<T>::type::value >::type >
{
	static const bool supported = true;
	typedef typename std::remove_pointer<T>::type Pointee;
	typedef typename std::remove_cv<Pointee>::type MutablePointee;

	static bool Extract( const BlueCValue& value, T& result, unsigned argIndex, BlueCError* err )
	{
		IRoot* obj = nullptr;
		if( !BlueCExtractObject( value, obj, argIndex, err ) )
		{
			return false;
		}
		if( !obj )
		{
			result = nullptr;
			return true;
		}
		T p = dynamic_cast<T>( obj );
		if( !p )
		{
			BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "object of class %s is not a %s",
								   BlueCClassNameOf( obj ), BlueCTypeName( typeid( Pointee ).name() ).c_str() );
			return false;
		}
		result = p;
		return true;
	}

	static bool Wrap( T value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapObject( reinterpret_cast<IRoot*>( const_cast<MutablePointee*>( value ) ), out );
		return true;
	}
};


//--------------------------------------------------------------------
// Containers
//--------------------------------------------------------------------

BLUEIMPORT bool BlueCExpectKind( const BlueCValue& value, uint32_t kind, const char* expected, unsigned argIndex, BlueCError* err );

// Wrap count items produced by wrapItem( i, BlueCValue& ) into an ARRAY
template< typename WrapItem >
bool BlueCWrapArray( size_t count, BlueCValue& out, BlueCError* err, WrapItem wrapItem )
{
	BlueCValueHolder holder;
	BlueCValue& result = holder.Get();
	result.kind = BLUE_VK_ARRAY;
	if( count )
	{
		result.u.arr.items = BlueCAllocateValues( count, err );
		if( !result.u.arr.items )
		{
			result.kind = BLUE_VK_NONE;
			return false;
		}
	}
	for( size_t i = 0; i < count; ++i )
	{
		// count is the number of initialised items, so a failure releases exactly those
		if( !wrapItem( i, result.u.arr.items[i] ) )
		{
			return false;
		}
		result.u.arr.count = i + 1;
	}
	holder.Detach( out );
	return true;
}

template< typename Container >
struct BlueCConvertSequence
{
	static const bool supported = true;
	typedef typename Container::value_type Item;

	static bool Extract( const BlueCValue& value, Container& result, unsigned argIndex, BlueCError* err )
	{
		if( !BlueCExpectKind( value, BLUE_VK_ARRAY, "array", argIndex, err ) )
		{
			return false;
		}
		result.clear();
		for( size_t i = 0; i < value.u.arr.count; ++i )
		{
			Item item{};
			if( !BlueCConvert<Item>::Extract( value.u.arr.items[i], item, argIndex, err ) )
			{
				return false;
			}
			result.push_back( item );
		}
		return true;
	}

	static bool Wrap( const Container& value, BlueCValue& out, BlueCError* err )
	{
		auto it = value.begin();
		return BlueCWrapArray( value.size(), out, err, [&]( size_t, BlueCValue& item ) {
			return BlueCConvert<Item>::Wrap( *it++, item, err );
		} );
	}
};

template< typename T, typename A > struct BlueCConvert< std::vector<T, A> > : BlueCConvertSequence< std::vector<T, A> > {};
template< typename T, typename A > struct BlueCConvert< std::list<T, A> > : BlueCConvertSequence< std::list<T, A> > {};

template< typename T1, typename T2 >
struct BlueCConvert< std::pair<T1, T2> >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, std::pair<T1, T2>& result, unsigned argIndex, BlueCError* err )
	{
		if( !BlueCExpectKind( value, BLUE_VK_ARRAY, "array of two items", argIndex, err ) )
		{
			return false;
		}
		if( value.u.arr.count != 2 )
		{
			BlueCSetArgumentError( err, BLUE_E_TYPE, argIndex, "expected an array of two items, got %zu", value.u.arr.count );
			return false;
		}
		return BlueCConvert<T1>::Extract( value.u.arr.items[0], result.first, argIndex, err ) &&
			   BlueCConvert<T2>::Extract( value.u.arr.items[1], result.second, argIndex, err );
	}

	static bool Wrap( const std::pair<T1, T2>& value, BlueCValue& out, BlueCError* err )
	{
		return BlueCWrapArray( 2, out, err, [&]( size_t i, BlueCValue& item ) {
			return i == 0 ? BlueCConvert<T1>::Wrap( value.first, item, err ) : BlueCConvert<T2>::Wrap( value.second, item, err );
		} );
	}
};

template< typename K, typename V, typename C, typename A >
struct BlueCConvert< std::map<K, V, C, A> >
{
	static const bool supported = true;
	typedef std::map<K, V, C, A> Map;

	static bool Extract( const BlueCValue& value, Map& result, unsigned argIndex, BlueCError* err )
	{
		if( !BlueCExpectKind( value, BLUE_VK_MAP, "map", argIndex, err ) )
		{
			return false;
		}
		result.clear();
		for( size_t i = 0; i < value.u.map.count; ++i )
		{
			K key{};
			if( !BlueCConvert<K>::Extract( value.u.map.kv[2 * i], key, argIndex, err ) )
			{
				return false;
			}
			V item{};
			if( !BlueCConvert<V>::Extract( value.u.map.kv[2 * i + 1], item, argIndex, err ) )
			{
				return false;
			}
			result[key] = item;
		}
		return true;
	}

	static bool Wrap( const Map& value, BlueCValue& out, BlueCError* err )
	{
		BlueCValueHolder holder;
		BlueCValue& result = holder.Get();
		result.kind = BLUE_VK_MAP;
		if( !value.empty() )
		{
			result.u.map.kv = BlueCAllocateValues( 2 * value.size(), err );
			if( !result.u.map.kv )
			{
				result.kind = BLUE_VK_NONE;
				return false;
			}
		}
		size_t i = 0;
		for( auto it = value.begin(); it != value.end(); ++it, ++i )
		{
			// A pair counts once both halves are set; release frees 2 * count values, so a
			// half-written pair is released by hand
			if( !BlueCConvert<K>::Wrap( it->first, result.u.map.kv[2 * i], err ) )
			{
				return false;
			}
			if( !BlueCConvert<V>::Wrap( it->second, result.u.map.kv[2 * i + 1], err ) )
			{
				BlueCValueRelease( &result.u.map.kv[2 * i] );
				return false;
			}
			result.u.map.count = i + 1;
		}
		holder.Detach( out );
		return true;
	}
};


//--------------------------------------------------------------------
// Be::Optional
//--------------------------------------------------------------------

template< typename Optional, typename T >
struct BlueCConvertOptional
{
	static const bool supported = BlueCConvert<T>::supported;

	static bool Extract( const BlueCValue& value, Optional& result, unsigned argIndex, BlueCError* err )
	{
		result.SetAssigned( true );
		return BlueCConvert<T>::Extract( value, result.GetValue(), argIndex, err );
	}

	static bool Wrap( const Optional& value, BlueCValue& out, BlueCError* err )
	{
		if( !value.IsAssigned() )
		{
			BlueCValueSetNone( out );
			return true;
		}
		return BlueCConvert<T>::Wrap( value.GetValue(), out, err );
	}
};

template< typename T > struct BlueCConvert< Be::Optional<T> > : BlueCConvertOptional< Be::Optional<T>, T > {};
template< typename T, T defaultValue > struct BlueCConvert< Be::OptionalWithDefaultValue<T, defaultValue> > : BlueCConvertOptional< Be::OptionalWithDefaultValue<T, defaultValue>, T > {};

#endif // BlueCAbiValue_h
