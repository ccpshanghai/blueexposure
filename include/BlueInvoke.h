// Copyright (c) 2026 CCP Games
//
// BlueInvoke.h: a typed, Python-free call table for every member wrapped by the exposure macros.
//
// The exposure macros (BlueExposureMacrosPython.h) and EnumTypeRegistration (BlueRegistration.h)
// each carry one BLUE_INVOKE_* statement. Per class they build two tables, registered in the
// process-wide registry of libblue_invoke under "<module>.<class>":
//   invoke.methods.v1   BlueInvokeClassTable of BlueInvokeMethod entries
//   invoke.props.v1     BlueInvokeClassTable of BlueInvokeProperty entries
// and per enum, under its exposed name:
//   invoke.enums.v1     BlueInvokeEnumTable of BlueInvokeEnumValue entries, copied from the chooser
// A table-format change adds a new capability name next to these; readers take the names they
// know. Nothing here includes or calls Python. Upstream structs (ClassInfo, VarEntry, ...) are
// untouched.
//
// Define BLUE_INVOKE_C_API_ONLY before including to get only the value/entry/table layouts and
// the registry API (no Blue headers): that is what libblue_invoke itself and readers use.

#pragma once
#ifndef BlueInvoke_h
#define BlueInvoke_h

#include "BlueInvokeConfig.h"

#include <cstddef>
#include <cstdint>
#include <typeinfo>

//--------------------------------------------------------------------------------------------
// Part 1: layouts and the registry API (no Blue headers)
//--------------------------------------------------------------------------------------------

#if defined( _WIN32 )
#	if defined( BLUE_INVOKE_BUILDING )
#		define BLUE_INVOKE_API __declspec( dllexport )
#	else
#		define BLUE_INVOKE_API __declspec( dllimport )
#	endif
#else
#	define BLUE_INVOKE_API __attribute__( ( visibility( "default" ) ) )
#endif

struct IRoot;

#define BLUE_INVOKE_MAGIC 0x31564942u // "BIV1": the table format of this header

#define BLUE_INVOKE_CAP_METHODS "invoke.methods.v1"
#define BLUE_INVOKE_CAP_PROPS "invoke.props.v1"
#define BLUE_INVOKE_CAP_ENUMS "invoke.enums.v1"

// Value tags. carbon-bind's cb_value uses the same numbers.
enum BlueInvokeTag : uint32_t
{
	BIV_NONE = 0,
	BIV_BOOL,   // v.i, 0 or 1
	BIV_INT,    // v.i
	BIV_UINT,   // v.u
	BIV_DOUBLE, // v.d
	BIV_STRING, // UTF-8 (v.s.p, v.s.n); wide strings converted
	BIV_OBJECT, // v.obj; outputs are owned (one Lock), inputs borrowed
	BIV_FLOATS, // v.f[0..count): Vector2/3/4, Color, Quaternion, Matrix
	BIV_ENUM,   // v.i, `type` = the C++ enum's compile-time name
	BIV_UNSUPPORTED = 0xffffffffu, // only in entries: this parameter/return has no conversion
};

// C layout, identical to carbon-bind's cb_value (which static_asserts size and offsets).
struct BlueInvokeValue
{
	uint32_t tag;
	uint32_t count; // float count for BIV_FLOATS
	union
	{
		int64_t i;
		uint64_t u;
		double d;
		struct
		{
			const char* p;
			size_t n;
		} s;
		struct IRoot* obj;
		float f[16];
	} v;
	void ( *release )( BlueInvokeValue* ); // non-null when the thunk allocated (output strings)
	const char* type;                      // static type name (enum / class), may be null
};

struct BlueInvokeError
{
	int code;
	char message[256];
};

// Thunk results; carbon-bind maps them to cb_status. No thunk throws.
enum BlueInvokeStatus : int
{
	BIS_OK = 0,
	BIS_ARGC,      // argument count outside [arity - numOptional, arity]
	BIS_TYPE,      // an argument's tag/value does not convert
	BIS_SELF,      // self is null or not an instance of the member's class
	BIS_FAILED,    // a Be::Result came back unsuccessful; message = BeGetErrorMessage
	BIS_EXCEPTION, // a C++ exception escaped the member; message = what()
	BIS_READONLY,  // for readers: the property has no setter
};

// `result` / `out` must be cleared (BIV_NONE, release == nullptr) on entry: a thunk overwrites it
// without releasing what it held. Use BlueInvokeValueClear between calls that reuse a value.
typedef int ( *BlueInvokeMethodFn )( IRoot* self, const BlueInvokeValue* args, uint32_t argc, BlueInvokeValue* result, BlueInvokeError* err );
typedef int ( *BlueInvokeGetFn )( IRoot* self, BlueInvokeValue* out, BlueInvokeError* err );
typedef int ( *BlueInvokeSetFn )( IRoot* self, const BlueInvokeValue* in, BlueInvokeError* err );

// BlueInvokeMethod::flags
enum : uint32_t
{
	BIM_BE_RESULT = 0x1,  // returns Be::Result; failure -> BIS_FAILED
	BIM_OUT_OBJECT = 0x2, // the result is the last T** parameter (owned), not passed by callers
	BIM_OUT_REF = 0x4,    // the result is the last T& parameter, not passed by callers
	BIM_REF_INPUT = 0x8,  // a non-const T& parameter is passed by value; writes are discarded
};

// BlueInvokeProperty::flags
enum : uint32_t
{
	BIP_READONLY = 0x1,  // MAP_PROPERTY_READONLY; set == nullptr
	BIP_BE_RESULT_GET = 0x2,
	BIP_BE_RESULT_SET = 0x4,
};

#define BLUE_INVOKE_MAX_ARGS 9

struct BlueInvokeMethod
{
	const char* name;
	BlueInvokeMethodFn fn; // nullptr: unsupported signature
	uint32_t arity;        // as callers see it (an out-parameter is not counted)
	uint32_t numOptional;
	uint64_t sigHash; // FNV-1a over tags and type hashes, computed by the compiler
	uint32_t returnTag;
	uint32_t flags;
	uint32_t argTags[BLUE_INVOKE_MAX_ARGS];
	const char* argTypeNames[BLUE_INVOKE_MAX_ARGS + 1];           // [0] return, [1..] args: enum/object names, else null
	const std::type_info* argEnumTypes[BLUE_INVOKE_MAX_ARGS + 1]; // same indexing: enum identity, else null
};

struct BlueInvokeProperty
{
	const char* name;
	BlueInvokeGetFn get; // nullptr: unsupported getter type
	BlueInvokeSetFn set; // nullptr: read-only (BIP_READONLY) or unsupported setter type
	uint64_t sigHash;
	uint32_t typeTag;
	uint32_t flags;
	const char* typeName;
	const std::type_info* enumType;
};

struct BlueInvokeClassTable
{
	uint32_t magic;       // BLUE_INVOKE_MAGIC
	uint32_t sizeofValue; // sizeof(BlueInvokeValue)
	uint32_t sizeofEntry; // sizeof(BlueInvokeMethod) or sizeof(BlueInvokeProperty), per capability
	uint32_t count;
	const char* buildId;         // BLUE_INVOKE_BUILD_ID as compiled into the registering DSO
	const void* const* entries;  // count pointers to BlueInvokeMethod / BlueInvokeProperty
	uint32_t unsupported;        // entries with fn/get == nullptr (or a writable property without set)
};

// One value of an exposed enum (capability invoke.enums.v1). Copied from upstream's chooser array
// at registration, so readers never depend on Be::VarChooser's layout. Strings point into the
// registering DSO's static data.
struct BlueInvokeEnumValue
{
	const char* name;
	int64_t value;
	const char* description; // may be null or empty
};

// The invoke.enums.v1 table. Its first 24 bytes match BlueInvokeClassTable (magic, sizeofValue,
// sizeofEntry, count, buildId), so one header check covers every capability.
struct BlueInvokeEnumTable
{
	uint32_t magic;       // BLUE_INVOKE_MAGIC
	uint32_t sizeofValue; // sizeof(BlueInvokeValue)
	uint32_t sizeofEntry; // sizeof(BlueInvokeEnumValue)
	uint32_t count;
	const char* buildId;                // BLUE_INVOKE_BUILD_ID as compiled into the registering DSO
	const BlueInvokeEnumValue* values;  // count values, in exposure order
	const char* typeName;               // the C++ enum's compile-time name (= BlueInvokeValue::type for BIV_ENUM)
	const std::type_info* enumType;     // = BlueInvokeMethod::argEnumTypes / BlueInvokeProperty::enumType
};

enum : int
{
	BLUE_INVOKE_REGISTER_OK = 0,
	BLUE_INVOKE_REGISTER_DUPLICATE = 1, // the first registration is kept by path; this one is recorded and
										// reachable through BlueInvokeForEachDuplicate (and FindByInfo, when
										// its classInfo is not already taken)
	BLUE_INVOKE_REGISTER_INVALID = 2,   // null classPath, capability or table
};

typedef void ( *BlueInvokeForEachFn )( void* user, const char* classPath, const void* classInfo, const char* capability, const void* table );

extern "C" {
BLUE_INVOKE_API int BlueInvokeRegister( const char* classPath, const void* classInfo, const char* capability, const void* table );
BLUE_INVOKE_API const void* BlueInvokeFind( const char* classPath, const char* capability );
BLUE_INVOKE_API const void* BlueInvokeFindByInfo( const void* classInfo, const char* capability );
BLUE_INVOKE_API void BlueInvokeForEach( BlueInvokeForEachFn fn, void* user );
BLUE_INVOKE_API const char* BlueInvokeBuildId( void );
BLUE_INVOKE_API uint32_t BlueInvokeDuplicateCount( void );
// Same callback as ForEach, over the registrations refused as duplicates, in registration order.
BLUE_INVOKE_API void BlueInvokeForEachDuplicate( BlueInvokeForEachFn fn, void* user );
}

// Releases what a thunk allocated in *v and resets it to BIV_NONE.
inline void BlueInvokeValueClear( BlueInvokeValue* v )
{
	if( v && v->release )
	{
		v->release( v );
	}
	if( v )
	{
		v->tag = BIV_NONE;
		v->count = 0;
		v->v.u = 0;
		v->release = nullptr;
		v->type = nullptr;
	}
}

static_assert( sizeof( float ) == 4, "BIV_FLOATS assumes 32-bit floats" );
#if UINTPTR_MAX == 0xffffffffffffffffu
static_assert( sizeof( BlueInvokeValue ) == 88, "BlueInvokeValue layout changed; cb_value must follow" );
static_assert( offsetof( BlueInvokeValue, tag ) == 0 && offsetof( BlueInvokeValue, count ) == 4 && offsetof( BlueInvokeValue, v ) == 8 &&
				   offsetof( BlueInvokeValue, release ) == 72 && offsetof( BlueInvokeValue, type ) == 80,
			   "BlueInvokeValue layout changed; cb_value must follow" );
static_assert( sizeof( BlueInvokeEnumValue ) == 24 && sizeof( BlueInvokeEnumTable ) == 48 &&
				   offsetof( BlueInvokeEnumTable, buildId ) == offsetof( BlueInvokeClassTable, buildId ) &&
				   offsetof( BlueInvokeEnumTable, values ) == 24 && offsetof( BlueInvokeEnumTable, typeName ) == 32 &&
				   offsetof( BlueInvokeEnumTable, enumType ) == 40,
			   "BlueInvokeEnumTable layout changed; the C ABI reader must follow" );
#endif

#if !defined( BLUE_INVOKE_C_API_ONLY )

//--------------------------------------------------------------------------------------------
// Part 2: conversions, thunks, entry builders and the hook statements (header-only)
//--------------------------------------------------------------------------------------------

#include "BlueTypes.h"
#include "BlueSharedString.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

struct Vector2;
struct Vector2d;
struct Vector3;
struct Vector3d;
struct Vector3i;
struct Vector4;
struct Vector4d;
struct Matrix;
struct Quaternion;
struct Color;

// Everything below is DSO-local: each hooked DSO keeps its own thunks and type names, so an
// identical instantiation in another DSO can never be interposed.
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC visibility push( hidden )
#endif

//--------------------------------------------------------------------------------------------
// Errors, hashes and compile-time type names
//--------------------------------------------------------------------------------------------

#if defined( __GNUC__ ) || defined( __clang__ )
__attribute__( ( format( printf, 3, 4 ) ) )
#endif
inline int BlueInvokeFail( BlueInvokeError* e, int code, const char* fmt, ... )
{
	if( e )
	{
		e->code = code;
		va_list ap;
		va_start( ap, fmt );
		std::vsnprintf( e->message, sizeof( e->message ), fmt, ap );
		va_end( ap );
	}
	return code;
}

inline const char* BlueInvokeTagName( uint32_t tag )
{
	switch( tag )
	{
	case BIV_NONE: return "none";
	case BIV_BOOL: return "bool";
	case BIV_INT: return "int";
	case BIV_UINT: return "uint";
	case BIV_DOUBLE: return "double";
	case BIV_STRING: return "string";
	case BIV_OBJECT: return "object";
	case BIV_FLOATS: return "floats";
	case BIV_ENUM: return "enum";
	default: return "unknown tag";
	}
}

inline int BlueInvokeTypeError( BlueInvokeError* e, int arg, const char* expected, uint32_t got )
{
	return BlueInvokeFail( e, BIS_TYPE, "argument %d: expected %s, got %s", arg, expected, BlueInvokeTagName( got ) );
}

inline void BlueInvokeReset( BlueInvokeValue* v )
{
	v->tag = BIV_NONE;
	v->count = 0;
	v->v.u = 0;
	v->release = nullptr;
	v->type = nullptr;
}

inline void BlueInvokeBegin( BlueInvokeValue* r, BlueInvokeError* e )
{
	if( r )
	{
		BlueInvokeReset( r );
	}
	if( e )
	{
		e->code = BIS_OK;
		e->message[0] = '\0';
	}
}

constexpr uint64_t kBlueInvokeFnvOffset = 14695981039346656037ull;
constexpr uint64_t kBlueInvokeFnvPrime = 1099511628211ull;

constexpr uint64_t BlueInvokeFnv( std::string_view s, uint64_t h = kBlueInvokeFnvOffset )
{
	for( char c : s )
	{
		h ^= static_cast<uint8_t>( c );
		h *= kBlueInvokeFnvPrime;
	}
	return h;
}

constexpr uint64_t BlueInvokeFnvMix( uint64_t h, uint64_t v )
{
	for( int i = 0; i < 8; ++i )
	{
		h ^= ( v >> ( 8 * i ) ) & 0xffu;
		h *= kBlueInvokeFnvPrime;
	}
	return h;
}

template <typename T>
constexpr const char* BlueInvokeRawTypeName()
{
#if defined( _MSC_VER ) && !defined( __clang__ )
	return __FUNCSIG__; // not verified on MSVC
#else
	return __PRETTY_FUNCTION__;
#endif
}

constexpr std::string_view BlueInvokeExtractTypeName( std::string_view f )
{
#if defined( _MSC_VER ) && !defined( __clang__ )
	// "const char *__cdecl BlueInvokeRawTypeName<struct Foo>(void)"
	const size_t b = f.find( "BlueInvokeRawTypeName<" ) + 22;
	const size_t e = f.rfind( ">(void)" );
#else
	// GCC: "constexpr const char* BlueInvokeRawTypeName() [with T = Foo]"; clang: "... [T = Foo]"
	const size_t b = f.find( "T = " ) + 4;
	const size_t e = f.rfind( ']' );
#endif
	return f.substr( b, e - b );
}

template <typename T>
struct BlueInvokeTypeNameView
{
	static constexpr std::string_view view = BlueInvokeExtractTypeName( BlueInvokeRawTypeName<T>() );
};

template <typename T, size_t... I>
constexpr std::array<char, sizeof...( I ) + 1> BlueInvokeMakeTypeName( std::index_sequence<I...> )
{
	return { { BlueInvokeTypeNameView<T>::view[I]..., '\0' } };
}

// A NUL-terminated compile-time name of T and its FNV-1a hash: the per-type input of sigHash.
template <typename T>
struct BlueInvokeTypeName
{
	static constexpr std::array<char, BlueInvokeTypeNameView<T>::view.size() + 1> storage =
		BlueInvokeMakeTypeName<T>( std::make_index_sequence<BlueInvokeTypeNameView<T>::view.size()>() );
	static constexpr const char* value = storage.data();
	static constexpr uint64_t hash = BlueInvokeFnv( BlueInvokeTypeNameView<T>::view );
};

//--------------------------------------------------------------------------------------------
// Traits. Completeness is checked before std::is_base_of ever sees a type (an incomplete T is a
// hard error there); anything not recognised degrades to "unsupported".
//--------------------------------------------------------------------------------------------

template <typename T>
using BlueInvokeNorm = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T, typename = void>
struct BlueInvokeIsComplete : std::false_type
{
};
template <typename T>
struct BlueInvokeIsComplete<T, std::void_t<decltype( sizeof( T ) )>> : std::true_type
{
};

template <typename T, bool = std::is_class<T>::value && BlueInvokeIsComplete<T>::value>
struct BlueInvokeIsBlueClass : std::false_type
{
};
template <typename T>
struct BlueInvokeIsBlueClass<T, true> : std::bool_constant<std::is_base_of<IRoot, T>::value>
{
};

template <typename T>
struct BlueInvokeIsBeResult : std::false_type
{
};
template <typename T>
struct BlueInvokeIsBeResult<Be::Result<T>> : std::true_type
{
};

template <typename T>
struct BlueInvokeIsCharLike : std::bool_constant<std::is_same<T, char>::value || std::is_same<T, wchar_t>::value ||
												 std::is_same<T, char16_t>::value || std::is_same<T, char32_t>::value
#if defined( __cpp_char8_t )
												 || std::is_same<T, char8_t>::value
#endif
												 >
{
};

//--------------------------------------------------------------------------------------------
// BlueInvokeConv<T>: the conversion of one normalised type (const and reference removed, as the
// Python path's remove_const_and_reference). A specialisation provides
//   in, out              which directions are supported
//   tag, typeHash        feed sigHash; typeName, enumType feed the entry
//   Storage              where a converted input lives during the call
//   In(value, storage&, err, argIndex) -> BlueInvokeStatus
//   Null(storage&)       a missing optional argument (Python's BlueGetNullValue)
//   Get(storage&)        what is passed to the member
//   Out(const T&, BlueInvokeValue*)
// The primary template means "unsupported".
//--------------------------------------------------------------------------------------------

struct BlueInvokeUnsupportedConv
{
	static constexpr bool in = false;
	static constexpr bool out = false;
	static constexpr uint32_t tag = BIV_UNSUPPORTED;
	static constexpr uint64_t typeHash = 0;
	static constexpr const char* typeName = nullptr;
	static constexpr const std::type_info* enumType = nullptr;
};

struct BlueInvokeVoidConv
{
	static constexpr bool in = false;
	static constexpr bool out = true;
	static constexpr uint32_t tag = BIV_NONE;
	static constexpr uint64_t typeHash = 0;
	static constexpr const char* typeName = nullptr;
	static constexpr const std::type_info* enumType = nullptr;
};

template <typename T, typename = void>
struct BlueInvokeConv : BlueInvokeUnsupportedConv
{
};

template <typename S>
struct BlueInvokeConvBase
{
	static constexpr bool in = true;
	static constexpr bool out = true;
	static constexpr uint64_t typeHash = 0;
	static constexpr const char* typeName = nullptr;
	static constexpr const std::type_info* enumType = nullptr;
	using Storage = S;
	static void Null( S& s ) { s = S(); }
	static S& Get( S& s ) { return s; }
};

// bool
template <>
struct BlueInvokeConv<bool> : BlueInvokeConvBase<bool>
{
	static constexpr uint32_t tag = BIV_BOOL;
	static int In( const BlueInvokeValue& v, bool& s, BlueInvokeError* e, int arg )
	{
		switch( v.tag )
		{
		case BIV_BOOL:
		case BIV_INT: s = v.v.i != 0; return BIS_OK;
		case BIV_UINT: s = v.v.u != 0; return BIS_OK;
		default: return BlueInvokeTypeError( e, arg, "bool", v.tag );
		}
	}
	static void Out( bool x, BlueInvokeValue* r )
	{
		r->tag = BIV_BOOL;
		r->v.i = x ? 1 : 0;
	}
};

// signed integers (int, long, int64_t, short, signed char)
template <typename T>
struct BlueInvokeConv<T, std::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value && !BlueInvokeIsCharLike<T>::value>>
	: BlueInvokeConvBase<T>
{
	static constexpr uint32_t tag = BIV_INT;
	static int In( const BlueInvokeValue& v, T& s, BlueInvokeError* e, int arg )
	{
		if( v.tag == BIV_INT || v.tag == BIV_BOOL ) // bool: Python's True/False are ints
		{
			if( v.v.i < static_cast<int64_t>( std::numeric_limits<T>::min() ) || v.v.i > static_cast<int64_t>( std::numeric_limits<T>::max() ) )
			{
				return BlueInvokeFail( e, BIS_TYPE, "argument %d: %lld is out of range", arg, static_cast<long long>( v.v.i ) );
			}
			s = static_cast<T>( v.v.i );
			return BIS_OK;
		}
		if( v.tag == BIV_UINT )
		{
			if( v.v.u > static_cast<uint64_t>( std::numeric_limits<T>::max() ) )
			{
				return BlueInvokeFail( e, BIS_TYPE, "argument %d: %llu is out of range", arg, static_cast<unsigned long long>( v.v.u ) );
			}
			s = static_cast<T>( v.v.u );
			return BIS_OK;
		}
		return BlueInvokeTypeError( e, arg, "int", v.tag );
	}
	static void Out( T x, BlueInvokeValue* r )
	{
		r->tag = BIV_INT;
		r->v.i = static_cast<int64_t>( x );
	}
};

// unsigned integers (unsigned, uint64_t, size_t, uint8_t, uint16_t)
template <typename T>
struct BlueInvokeConv<T, std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value &&
										  !BlueInvokeIsCharLike<T>::value>>
	: BlueInvokeConvBase<T>
{
	static constexpr uint32_t tag = BIV_UINT;
	static int In( const BlueInvokeValue& v, T& s, BlueInvokeError* e, int arg )
	{
		if( v.tag == BIV_UINT )
		{
			if( v.v.u > static_cast<uint64_t>( std::numeric_limits<T>::max() ) )
			{
				return BlueInvokeFail( e, BIS_TYPE, "argument %d: %llu is out of range", arg, static_cast<unsigned long long>( v.v.u ) );
			}
			s = static_cast<T>( v.v.u );
			return BIS_OK;
		}
		if( v.tag == BIV_INT || v.tag == BIV_BOOL ) // bool: Python's True/False are ints
		{
			if( v.v.i < 0 || static_cast<uint64_t>( v.v.i ) > static_cast<uint64_t>( std::numeric_limits<T>::max() ) )
			{
				return BlueInvokeFail( e, BIS_TYPE, "argument %d: %lld is out of range", arg, static_cast<long long>( v.v.i ) );
			}
			s = static_cast<T>( v.v.i );
			return BIS_OK;
		}
		return BlueInvokeTypeError( e, arg, "uint", v.tag );
	}
	static void Out( T x, BlueInvokeValue* r )
	{
		r->tag = BIV_UINT;
		r->v.u = static_cast<uint64_t>( x );
	}
};

// float, double
template <typename T>
struct BlueInvokeConv<T, std::enable_if_t<std::is_floating_point<T>::value>> : BlueInvokeConvBase<T>
{
	static constexpr uint32_t tag = BIV_DOUBLE;
	static int In( const BlueInvokeValue& v, T& s, BlueInvokeError* e, int arg )
	{
		switch( v.tag )
		{
		case BIV_DOUBLE: s = static_cast<T>( v.v.d ); return BIS_OK;
		case BIV_BOOL: // Python's True/False are ints
		case BIV_INT: s = static_cast<T>( v.v.i ); return BIS_OK;
		case BIV_UINT: s = static_cast<T>( v.v.u ); return BIS_OK;
		default: return BlueInvokeTypeError( e, arg, "double", v.tag );
		}
	}
	static void Out( T x, BlueInvokeValue* r )
	{
		r->tag = BIV_DOUBLE;
		r->v.d = static_cast<double>( x );
	}
};

// enums: identity by type_info (equality is valid across DSOs), hash by compile-time name
template <typename T>
struct BlueInvokeConv<T, std::enable_if_t<std::is_enum<T>::value>> : BlueInvokeConvBase<T>
{
	static constexpr uint32_t tag = BIV_ENUM;
	static constexpr uint64_t typeHash = BlueInvokeTypeName<T>::hash;
	static constexpr const char* typeName = BlueInvokeTypeName<T>::value;
	static constexpr const std::type_info* enumType = &typeid( T );
	static int In( const BlueInvokeValue& v, T& s, BlueInvokeError* e, int arg )
	{
		switch( v.tag )
		{
		case BIV_ENUM:
		case BIV_BOOL: // Python's True/False are ints
		case BIV_INT: s = static_cast<T>( v.v.i ); return BIS_OK;
		case BIV_UINT: s = static_cast<T>( v.v.u ); return BIS_OK;
		default: return BlueInvokeTypeError( e, arg, "enum", v.tag );
		}
	}
	static void Out( T x, BlueInvokeValue* r )
	{
		r->tag = BIV_ENUM;
		r->v.i = static_cast<int64_t>( x );
		r->type = typeName;
	}
};

// strings: outputs are owned copies, inputs are converted into Storage for the call
inline void BlueInvokeReleaseString( BlueInvokeValue* v )
{
	std::free( const_cast<char*>( v->v.s.p ) );
	v->v.s.p = nullptr;
	v->v.s.n = 0;
	v->release = nullptr;
}

inline void BlueInvokeOutString( const char* p, size_t n, BlueInvokeValue* r )
{
	char* c = static_cast<char*>( std::malloc( n + 1 ) );
	if( !c )
	{
		throw std::bad_alloc();
	}
	if( n )
	{
		std::memcpy( c, p, n );
	}
	c[n] = '\0';
	r->tag = BIV_STRING;
	r->v.s.p = c;
	r->v.s.n = n;
	r->release = &BlueInvokeReleaseString;
}

// UTF-8 -> wchar_t (UTF-32, or UTF-16 with surrogates where wchar_t is 16 bits). Rejects invalid UTF-8.
inline bool BlueInvokeUtf8ToWide( const char* p, size_t n, std::wstring& out )
{
	static const uint32_t kMin[4] = { 0, 0x80, 0x800, 0x10000 };
	out.clear();
	out.reserve( n );
	size_t i = 0;
	while( i < n )
	{
		uint32_t c = static_cast<uint8_t>( p[i] );
		size_t extra;
		if( c < 0x80 )
		{
			extra = 0;
		}
		else if( ( c >> 5 ) == 0x6 )
		{
			c &= 0x1f;
			extra = 1;
		}
		else if( ( c >> 4 ) == 0xe )
		{
			c &= 0x0f;
			extra = 2;
		}
		else if( ( c >> 3 ) == 0x1e )
		{
			c &= 0x07;
			extra = 3;
		}
		else
		{
			return false;
		}
		if( n - i - 1 < extra )
		{
			return false;
		}
		for( size_t k = 1; k <= extra; ++k )
		{
			const uint8_t b = static_cast<uint8_t>( p[i + k] );
			if( ( b & 0xc0 ) != 0x80 )
			{
				return false;
			}
			c = ( c << 6 ) | ( b & 0x3f );
		}
		if( c < kMin[extra] || c > 0x10ffff || ( c >= 0xd800 && c <= 0xdfff ) )
		{
			return false;
		}
		i += extra + 1;
		if( sizeof( wchar_t ) == 2 && c >= 0x10000 )
		{
			c -= 0x10000;
			out.push_back( static_cast<wchar_t>( 0xd800 + ( c >> 10 ) ) );
			out.push_back( static_cast<wchar_t>( 0xdc00 + ( c & 0x3ff ) ) );
		}
		else
		{
			out.push_back( static_cast<wchar_t>( c ) );
		}
	}
	return true;
}

// wchar_t -> UTF-8; invalid code points and lone surrogates become U+FFFD.
inline void BlueInvokeWideToUtf8( const wchar_t* p, size_t n, std::string& out )
{
	out.clear();
	out.reserve( n );
	for( size_t i = 0; i < n; ++i )
	{
		uint32_t c = static_cast<uint32_t>( p[i] ) & ( sizeof( wchar_t ) == 2 ? 0xffffu : 0xffffffffu );
		if( sizeof( wchar_t ) == 2 && c >= 0xd800 && c <= 0xdbff && i + 1 < n )
		{
			const uint32_t lo = static_cast<uint32_t>( p[i + 1] ) & 0xffffu;
			if( lo >= 0xdc00 && lo <= 0xdfff )
			{
				c = 0x10000 + ( ( c - 0xd800 ) << 10 ) + ( lo - 0xdc00 );
				++i;
			}
		}
		if( c > 0x10ffff || ( c >= 0xd800 && c <= 0xdfff ) )
		{
			c = 0xfffd;
		}
		if( c < 0x80 )
		{
			out.push_back( static_cast<char>( c ) );
		}
		else if( c < 0x800 )
		{
			out.push_back( static_cast<char>( 0xc0 | ( c >> 6 ) ) );
			out.push_back( static_cast<char>( 0x80 | ( c & 0x3f ) ) );
		}
		else if( c < 0x10000 )
		{
			out.push_back( static_cast<char>( 0xe0 | ( c >> 12 ) ) );
			out.push_back( static_cast<char>( 0x80 | ( ( c >> 6 ) & 0x3f ) ) );
			out.push_back( static_cast<char>( 0x80 | ( c & 0x3f ) ) );
		}
		else
		{
			out.push_back( static_cast<char>( 0xf0 | ( c >> 18 ) ) );
			out.push_back( static_cast<char>( 0x80 | ( ( c >> 12 ) & 0x3f ) ) );
			out.push_back( static_cast<char>( 0x80 | ( ( c >> 6 ) & 0x3f ) ) );
			out.push_back( static_cast<char>( 0x80 | ( c & 0x3f ) ) );
		}
	}
}

inline int BlueInvokeInUtf8( const BlueInvokeValue& v, std::string& s, BlueInvokeError* e, int arg )
{
	if( v.tag != BIV_STRING )
	{
		return BlueInvokeTypeError( e, arg, "string", v.tag );
	}
	s.assign( v.v.s.p ? v.v.s.p : "", v.v.s.p ? v.v.s.n : 0 );
	return BIS_OK;
}

inline int BlueInvokeInWide( const BlueInvokeValue& v, std::wstring& s, BlueInvokeError* e, int arg )
{
	if( v.tag != BIV_STRING )
	{
		return BlueInvokeTypeError( e, arg, "string", v.tag );
	}
	if( !BlueInvokeUtf8ToWide( v.v.s.p ? v.v.s.p : "", v.v.s.p ? v.v.s.n : 0, s ) )
	{
		return BlueInvokeFail( e, BIS_TYPE, "argument %d: invalid UTF-8", arg );
	}
	return BIS_OK;
}

inline void BlueInvokeOutWide( const wchar_t* p, size_t n, BlueInvokeValue* r )
{
	std::string u;
	BlueInvokeWideToUtf8( p, n, u );
	BlueInvokeOutString( u.data(), u.size(), r );
}

template <>
struct BlueInvokeConv<std::string> : BlueInvokeConvBase<std::string>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, std::string& s, BlueInvokeError* e, int arg ) { return BlueInvokeInUtf8( v, s, e, arg ); }
	static void Out( const std::string& x, BlueInvokeValue* r ) { BlueInvokeOutString( x.data(), x.size(), r ); }
};

template <>
struct BlueInvokeConv<const char*> : BlueInvokeConvBase<std::string>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, std::string& s, BlueInvokeError* e, int arg ) { return BlueInvokeInUtf8( v, s, e, arg ); }
	static const char* Get( std::string& s ) { return s.c_str(); }
	static void Out( const char* x, BlueInvokeValue* r )
	{
		if( x )
		{
			BlueInvokeOutString( x, std::strlen( x ), r );
		}
	}
};

template <>
struct BlueInvokeConv<std::wstring> : BlueInvokeConvBase<std::wstring>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, std::wstring& s, BlueInvokeError* e, int arg ) { return BlueInvokeInWide( v, s, e, arg ); }
	static void Out( const std::wstring& x, BlueInvokeValue* r ) { BlueInvokeOutWide( x.data(), x.size(), r ); }
};

template <>
struct BlueInvokeConv<const wchar_t*> : BlueInvokeConvBase<std::wstring>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, std::wstring& s, BlueInvokeError* e, int arg ) { return BlueInvokeInWide( v, s, e, arg ); }
	static const wchar_t* Get( std::wstring& s ) { return s.c_str(); }
	static void Out( const wchar_t* x, BlueInvokeValue* r )
	{
		if( x )
		{
			BlueInvokeOutWide( x, std::wcslen( x ), r );
		}
	}
};

template <>
struct BlueInvokeConv<BlueSharedString> : BlueInvokeConvBase<BlueSharedString>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, BlueSharedString& s, BlueInvokeError* e, int arg )
	{
		std::string u;
		const int rc = BlueInvokeInUtf8( v, u, e, arg );
		if( rc == BIS_OK )
		{
			s = BlueSharedString( u );
		}
		return rc;
	}
	static void Out( const BlueSharedString& x, BlueInvokeValue* r )
	{
		const char* c = x.c_str();
		BlueInvokeOutString( c ? c : "", c ? std::strlen( c ) : 0, r );
	}
};

template <>
struct BlueInvokeConv<BlueSharedStringW> : BlueInvokeConvBase<BlueSharedStringW>
{
	static constexpr uint32_t tag = BIV_STRING;
	static int In( const BlueInvokeValue& v, BlueSharedStringW& s, BlueInvokeError* e, int arg )
	{
		std::wstring w;
		const int rc = BlueInvokeInWide( v, w, e, arg );
		if( rc == BIS_OK )
		{
			s = BlueSharedStringW( w );
		}
		return rc;
	}
	static void Out( const BlueSharedStringW& x, BlueInvokeValue* r )
	{
		const wchar_t* c = x.c_str();
		BlueInvokeOutWide( c ? c : L"", c ? std::wcslen( c ) : 0, r );
	}
};

// Blue objects: T*, BlueBasicPtr<T> (IRootPtr), BluePtr<T> for a complete T derived from IRoot.
// Inputs are borrowed and checked with dynamic_cast (as BluePythonCast); outputs carry one Lock.
// IRoot is the first base in every branch (the Python wrapper's assumption), hence reinterpret_cast.
template <typename T>
inline T* BlueInvokeCastObject( IRoot* o )
{
	if constexpr( std::is_same<std::remove_cv_t<T>, IRoot>::value )
	{
		return o;
	}
	else
	{
		return dynamic_cast<T*>( o );
	}
}

template <typename T>
struct BlueInvokeObjectConv
{
	using U = std::remove_cv_t<T>;
	static constexpr bool in = true;
	static constexpr bool out = true;
	static constexpr uint32_t tag = BIV_OBJECT;
	static constexpr uint64_t typeHash = BlueInvokeTypeName<U>::hash;
	static constexpr const char* typeName = BlueInvokeTypeName<U>::value;
	static constexpr const std::type_info* enumType = nullptr;

	static int InPtr( const BlueInvokeValue& v, T*& s, BlueInvokeError* e, int arg )
	{
		if( v.tag == BIV_NONE || ( v.tag == BIV_OBJECT && !v.v.obj ) )
		{
			s = nullptr;
			return BIS_OK;
		}
		if( v.tag != BIV_OBJECT )
		{
			return BlueInvokeTypeError( e, arg, "object", v.tag );
		}
		s = BlueInvokeCastObject<T>( v.v.obj );
		if( !s )
		{
			return BlueInvokeFail( e, BIS_TYPE, "argument %d: object is not a %s", arg, typeName );
		}
		return BIS_OK;
	}

	// `owned`: the pointer already carries the caller's lock (a Be::Result T** out-parameter).
	static void OutPtr( T* p, BlueInvokeValue* r, bool owned )
	{
		if( !p )
		{
			return; // None, as Python returns
		}
		IRoot* root = reinterpret_cast<IRoot*>( const_cast<U*>( p ) );
		if( !owned )
		{
			root->Lock();
		}
		r->tag = BIV_OBJECT;
		r->v.obj = root;
		r->type = typeName;
	}
};

template <typename T>
struct BlueInvokeConv<T*, std::enable_if_t<BlueInvokeIsBlueClass<std::remove_cv_t<T>>::value>> : BlueInvokeObjectConv<T>
{
	using Storage = T*;
	static int In( const BlueInvokeValue& v, T*& s, BlueInvokeError* e, int arg ) { return BlueInvokeObjectConv<T>::InPtr( v, s, e, arg ); }
	static void Null( T*& s ) { s = nullptr; }
	static T*& Get( T*& s ) { return s; }
	static void Out( T* p, BlueInvokeValue* r ) { BlueInvokeObjectConv<T>::OutPtr( p, r, false ); }
	static void OutOwned( T* p, BlueInvokeValue* r ) { BlueInvokeObjectConv<T>::OutPtr( p, r, true ); }
};

template <typename P, typename T>
struct BlueInvokeSmartPtrConv : BlueInvokeObjectConv<T>
{
	using Storage = P;
	static int In( const BlueInvokeValue& v, P& s, BlueInvokeError* e, int arg )
	{
		T* p = nullptr;
		const int rc = BlueInvokeObjectConv<T>::InPtr( v, p, e, arg );
		if( rc == BIS_OK )
		{
			s = p;
		}
		return rc;
	}
	static void Null( P& s ) { s = static_cast<T*>( nullptr ); }
	static P& Get( P& s ) { return s; }
	static void Out( const P& p, BlueInvokeValue* r ) { BlueInvokeObjectConv<T>::OutPtr( static_cast<T*>( p ), r, false ); }
};

template <typename T>
struct BlueInvokeConv<BlueBasicPtr<T>, std::enable_if_t<BlueInvokeIsBlueClass<T>::value>> : BlueInvokeSmartPtrConv<BlueBasicPtr<T>, T>
{
};

template <typename T>
struct BlueInvokeConv<BluePtr<T>, std::enable_if_t<BlueInvokeIsBlueClass<T>::value>> : BlueInvokeSmartPtrConv<BluePtr<T>, T>
{
};

// Math types -> BIV_FLOATS. Copied as N floats, as Blue does (BLUE_OVERRIDE_VECTOR_TYPES keeps the layout).
template <typename T, uint32_t N>
struct BlueInvokeFloatsConv : BlueInvokeConvBase<T>
{
	static constexpr uint32_t tag = BIV_FLOATS;
	static int In( const BlueInvokeValue& v, T& s, BlueInvokeError* e, int arg )
	{
		static_assert( sizeof( T ) >= N * sizeof( float ), "math type smaller than its float count" );
		if( v.tag != BIV_FLOATS || v.count != N )
		{
			return BlueInvokeFail( e, BIS_TYPE, "argument %d: expected %u floats, got %s/%u", arg, N, BlueInvokeTagName( v.tag ), v.count );
		}
		std::memcpy( static_cast<void*>( &s ), v.v.f, N * sizeof( float ) );
		return BIS_OK;
	}
	static void Null( T& s ) { std::memset( static_cast<void*>( &s ), 0, N * sizeof( float ) ); }
	static void Out( const T& x, BlueInvokeValue* r )
	{
		r->tag = BIV_FLOATS;
		r->count = N;
		std::memcpy( r->v.f, static_cast<const void*>( &x ), N * sizeof( float ) );
	}
};

template <> struct BlueInvokeConv<Vector2> : BlueInvokeFloatsConv<Vector2, 2> {};
template <> struct BlueInvokeConv<Vector3> : BlueInvokeFloatsConv<Vector3, 3> {};
template <> struct BlueInvokeConv<Vector4> : BlueInvokeFloatsConv<Vector4, 4> {};
template <> struct BlueInvokeConv<Color> : BlueInvokeFloatsConv<Color, 4> {};
template <> struct BlueInvokeConv<Quaternion> : BlueInvokeFloatsConv<Quaternion, 4> {};
template <> struct BlueInvokeConv<Matrix> : BlueInvokeFloatsConv<Matrix, 16> {};

// Explicitly unsupported in v1 (reported, not silently absent).
template <> struct BlueInvokeConv<Vector2d> : BlueInvokeUnsupportedConv {};
template <> struct BlueInvokeConv<Vector3d> : BlueInvokeUnsupportedConv {};
template <> struct BlueInvokeConv<Vector3i> : BlueInvokeUnsupportedConv {};
template <> struct BlueInvokeConv<Vector4d> : BlueInvokeUnsupportedConv {};

// Be::Optional<T> / OptionalWithDefaultValue<T, d>: arguments only, for a T stored by value.
template <typename C, typename = void>
struct BlueInvokeStorageOf
{
	using type = void;
};
template <typename C>
struct BlueInvokeStorageOf<C, std::void_t<typename C::Storage>>
{
	using type = typename C::Storage;
};

template <typename T>
struct BlueInvokeOptionalOk
	: std::bool_constant<BlueInvokeConv<T>::in && std::is_same<typename BlueInvokeStorageOf<BlueInvokeConv<T>>::type, T>::value>
{
};

template <typename O, typename T>
struct BlueInvokeOptionalConv
{
	using Inner = BlueInvokeConv<T>;
	static constexpr bool in = true;
	static constexpr bool out = false;
	static constexpr uint32_t tag = Inner::tag;
	static constexpr uint64_t typeHash = Inner::typeHash;
	static constexpr const char* typeName = Inner::typeName;
	static constexpr const std::type_info* enumType = Inner::enumType;
	using Storage = O;
	static int In( const BlueInvokeValue& v, O& s, BlueInvokeError* e, int arg )
	{
		T tmp{};
		const int rc = Inner::In( v, tmp, e, arg );
		if( rc == BIS_OK )
		{
			s = O( tmp ); // assigned
		}
		return rc;
	}
	static void Null( O& s ) { s = O(); } // unassigned; OptionalWithDefaultValue carries its default
	static O& Get( O& s ) { return s; }
};

template <typename T>
struct BlueInvokeConv<Be::Optional<T>, std::enable_if_t<BlueInvokeOptionalOk<T>::value>> : BlueInvokeOptionalConv<Be::Optional<T>, T>
{
};

template <typename T, T d>
struct BlueInvokeConv<Be::OptionalWithDefaultValue<T, d>, std::enable_if_t<BlueInvokeOptionalOk<T>::value>>
	: BlueInvokeOptionalConv<Be::OptionalWithDefaultValue<T, d>, T>
{
};

template <typename C>
using BlueInvokeGetType = decltype( C::Get( std::declval<typename C::Storage&>() ) );

//--------------------------------------------------------------------------------------------
// Member function signatures
//--------------------------------------------------------------------------------------------

template <typename MemFn>
struct BlueInvokeMemFn
{
	static constexpr bool valid = false;
};

#define BLUE_INVOKE_MEMFN_TRAITS( QUAL ) \
	template <typename R, typename C, typename... A> \
	struct BlueInvokeMemFn<R ( C::* )( A... ) QUAL> \
	{ \
		static constexpr bool valid = true; \
		using Ret = R; \
		using Class = C; \
		using Args = std::tuple<A...>; \
		static constexpr size_t N = sizeof...( A ); \
	};
BLUE_INVOKE_MEMFN_TRAITS()
BLUE_INVOKE_MEMFN_TRAITS( const )
BLUE_INVOKE_MEMFN_TRAITS( noexcept )
BLUE_INVOKE_MEMFN_TRAITS( const noexcept )
#undef BLUE_INVOKE_MEMFN_TRAITS

// Python's rule for a Be::Result method's last parameter (BlueCallFunctionWithBeResult.h):
// T** with T derived from IRoot -> returned object (owned); any reference -> returned value;
// otherwise an ordinary argument. A T** whose T is incomplete cannot be classified: unsupported.
enum BlueInvokeOutKind : int
{
	BIO_NONE = 0,
	BIO_OBJECT,
	BIO_REF,
	BIO_INVALID,
};

template <typename L>
constexpr int BlueInvokeClassifyLast()
{
	using L0 = std::remove_cv_t<L>;
	if constexpr( std::is_pointer<L0>::value && std::is_pointer<std::remove_cv_t<std::remove_pointer_t<L0>>>::value )
	{
		using U = std::remove_cv_t<std::remove_pointer_t<std::remove_cv_t<std::remove_pointer_t<L0>>>>;
		if constexpr( std::is_class<U>::value && !BlueInvokeIsComplete<U>::value )
		{
			return BIO_INVALID;
		}
		else if constexpr( BlueInvokeIsBlueClass<U>::value )
		{
			return BIO_OBJECT;
		}
		else
		{
			return BIO_NONE;
		}
	}
	else if constexpr( std::is_reference<L>::value )
	{
		return BIO_REF;
	}
	else
	{
		return BIO_NONE;
	}
}

template <typename Args, bool Check>
struct BlueInvokeLastParam
{
	using type = void;
	static constexpr int kind = BIO_NONE;
};
template <typename Args>
struct BlueInvokeLastParam<Args, true>
{
	using type = std::tuple_element_t<std::tuple_size<Args>::value - 1, Args>;
	static constexpr int kind = BlueInvokeClassifyLast<type>();
};

template <typename R, bool BeRes, int Kind, typename Last>
struct BlueInvokeRetConvSel
{
	using type = std::conditional_t<std::is_void<R>::value, BlueInvokeVoidConv, BlueInvokeConv<BlueInvokeNorm<R>>>;
};
template <typename R, typename Last>
struct BlueInvokeRetConvSel<R, true, BIO_NONE, Last>
{
	using type = BlueInvokeVoidConv;
};
template <typename R, typename Last>
struct BlueInvokeRetConvSel<R, true, BIO_OBJECT, Last>
{
	using type = BlueInvokeConv<std::remove_pointer_t<BlueInvokeNorm<Last>>>;
};
template <typename R, typename Last>
struct BlueInvokeRetConvSel<R, true, BIO_REF, Last>
{
	using type = BlueInvokeConv<BlueInvokeNorm<Last>>;
};
template <typename R, typename Last>
struct BlueInvokeRetConvSel<R, true, BIO_INVALID, Last>
{
	using type = BlueInvokeUnsupportedConv;
};

template <typename MemFn_, int NOpt, bool = BlueInvokeMemFn<MemFn_>::valid>
struct BlueInvokeMethodSig
{
	static constexpr bool valid = false;
};

template <typename MemFn_, int NOpt>
struct BlueInvokeMethodSig<MemFn_, NOpt, true>
{
	static constexpr bool valid = true;
	static constexpr int nOpt = NOpt;
	using MemFn = MemFn_;
	using Tr = BlueInvokeMemFn<MemFn>;
	using R = typename Tr::Ret;
	using C = typename Tr::Class;
	using Args = typename Tr::Args;
	static constexpr size_t N = Tr::N;
	static constexpr bool beResult = BlueInvokeIsBeResult<BlueInvokeNorm<R>>::value;
	using LastInfo = BlueInvokeLastParam<Args, beResult && ( N > 0 )>;
	using Last = typename LastInfo::type;
	static constexpr int outKind = LastInfo::kind;
	static constexpr size_t visible = N - ( ( outKind == BIO_OBJECT || outKind == BIO_REF ) ? 1 : 0 );
	using RetConv = typename BlueInvokeRetConvSel<R, beResult, outKind, Last>::type;
};

template <typename S, size_t I>
using BlueInvokeArg = std::tuple_element_t<I, typename S::Args>;
template <typename S, size_t I>
using BlueInvokeArgConv = BlueInvokeConv<BlueInvokeNorm<BlueInvokeArg<S, I>>>;

template <typename S, size_t... I>
constexpr bool BlueInvokeAllIn( std::index_sequence<I...> )
{
	return ( true && ... && BlueInvokeArgConv<S, I>::in );
}

template <typename S, size_t... I>
constexpr bool BlueInvokeMethodCallable( std::index_sequence<I...> )
{
	using MemFn = typename S::MemFn;
	using C = typename S::C;
	if constexpr( S::outKind == BIO_OBJECT )
	{
		return std::is_invocable<MemFn, C*, BlueInvokeGetType<BlueInvokeArgConv<S, I>>..., std::remove_pointer_t<BlueInvokeNorm<typename S::Last>>*>::value;
	}
	else if constexpr( S::outKind == BIO_REF )
	{
		return std::is_invocable<MemFn, C*, BlueInvokeGetType<BlueInvokeArgConv<S, I>>..., BlueInvokeNorm<typename S::Last>&>::value;
	}
	else
	{
		return std::is_invocable<MemFn, C*, BlueInvokeGetType<BlueInvokeArgConv<S, I>>...>::value;
	}
}

template <typename S>
constexpr bool BlueInvokeMethodSupported()
{
	if constexpr( !S::valid )
	{
		return false;
	}
	else if constexpr( S::N > BLUE_INVOKE_MAX_ARGS || S::outKind == BIO_INVALID || S::nOpt < 0 || static_cast<size_t>( S::nOpt ) > S::visible )
	{
		return false;
	}
	else if constexpr( !S::RetConv::out )
	{
		return false;
	}
	else if constexpr( !BlueInvokeAllIn<S>( std::make_index_sequence<S::visible>() ) )
	{
		return false;
	}
	else
	{
		return BlueInvokeMethodCallable<S>( std::make_index_sequence<S::visible>() );
	}
}

template <typename S, size_t... I>
constexpr void BlueInvokeFillArgs( BlueInvokeMethod& m, std::index_sequence<I...> )
{
	uint64_t h = kBlueInvokeFnvOffset;
	h = BlueInvokeFnvMix( h, S::RetConv::tag );
	h = BlueInvokeFnvMix( h, S::RetConv::typeHash );
	h = BlueInvokeFnvMix( h, m.arity );
	h = BlueInvokeFnvMix( h, m.numOptional );
	( ( m.argTags[I] = BlueInvokeArgConv<S, I>::tag, m.argTypeNames[I + 1] = BlueInvokeArgConv<S, I>::typeName,
		m.argEnumTypes[I + 1] = BlueInvokeArgConv<S, I>::enumType, h = BlueInvokeFnvMix( h, BlueInvokeArgConv<S, I>::tag ),
		h = BlueInvokeFnvMix( h, BlueInvokeArgConv<S, I>::typeHash ) ),
	  ... );
	m.sigHash = h;
}

template <typename S, size_t... I>
constexpr uint32_t BlueInvokeRefInputFlag( std::index_sequence<I...> )
{
	return ( false || ... ||
			 ( std::is_lvalue_reference<BlueInvokeArg<S, I>>::value && !std::is_const<std::remove_reference_t<BlueInvokeArg<S, I>>>::value ) )
			   ? BIM_REF_INPUT
			   : 0u;
}

//--------------------------------------------------------------------------------------------
// Method thunk
//--------------------------------------------------------------------------------------------

template <typename Conv>
inline int BlueInvokeConvertArg( typename Conv::Storage& s, const BlueInvokeValue* a, uint32_t n, size_t i, BlueInvokeError* e )
{
	if( i < n )
	{
		return Conv::In( a[i], s, e, static_cast<int>( i ) + 1 );
	}
	Conv::Null( s ); // a missing optional argument
	return BIS_OK;
}

template <typename Res>
inline int BlueInvokeFailResult( BlueInvokeError* e, const Res& res )
{
	const char* m = BeGetErrorMessage( res );
	return BlueInvokeFail( e, BIS_FAILED, "%s", ( m && *m ) ? m : "Be::Result failed" );
}

// The caller may pass result == nullptr; a scratch value then takes the output and is released.
struct BlueInvokeScratch
{
	BlueInvokeValue value;
	BlueInvokeValue* target;
	explicit BlueInvokeScratch( BlueInvokeValue* r ) : target( r ? r : &value ) { BlueInvokeReset( &value ); }
	~BlueInvokeScratch()
	{
		if( target == &value )
		{
			if( value.tag == BIV_OBJECT && value.v.obj )
			{
				value.v.obj->Unlock();
			}
			BlueInvokeValueClear( &value );
		}
	}
};

template <typename MemFn, MemFn fn, int NOpt, bool = BlueInvokeMethodSupported<BlueInvokeMethodSig<MemFn, NOpt>>()>
struct BlueInvokeThunk
{
	static constexpr BlueInvokeMethodFn value = nullptr;
};

template <typename MemFn, MemFn fn, int NOpt>
struct BlueInvokeThunk<MemFn, fn, NOpt, true>
{
	using S = BlueInvokeMethodSig<MemFn, NOpt>;
	using C = typename S::C;
	using R = typename S::R;

	template <size_t I>
	using Conv = BlueInvokeArgConv<S, I>;

	template <size_t... I>
	static int Invoke( C* p, const BlueInvokeValue* a, uint32_t n, BlueInvokeValue* r, BlueInvokeError* e, std::index_sequence<I...> )
	{
		std::tuple<typename Conv<I>::Storage...> st;
		int rc = BIS_OK;
		const bool ok = ( true && ... && ( ( rc = BlueInvokeConvertArg<Conv<I>>( std::get<I>( st ), a, n, I, e ) ) == BIS_OK ) );
		if( !ok )
		{
			return rc;
		}
		if constexpr( !S::beResult )
		{
			if constexpr( std::is_void<R>::value )
			{
				( p->*fn )( Conv<I>::Get( std::get<I>( st ) )... );
			}
			else
			{
				decltype( auto ) ret = ( p->*fn )( Conv<I>::Get( std::get<I>( st ) )... );
				S::RetConv::Out( ret, r );
			}
		}
		else if constexpr( S::outKind == BIO_OBJECT )
		{
			std::remove_pointer_t<BlueInvokeNorm<typename S::Last>> out = nullptr;
			const BlueInvokeNorm<R> res = ( p->*fn )( Conv<I>::Get( std::get<I>( st ) )..., &out );
			if( !BeIsSuccess( res ) )
			{
				if( out )
				{
					reinterpret_cast<IRoot*>( out )->Unlock();
				}
				return BlueInvokeFailResult( e, res );
			}
			S::RetConv::OutOwned( out, r );
		}
		else if constexpr( S::outKind == BIO_REF )
		{
			BlueInvokeNorm<typename S::Last> out{};
			const BlueInvokeNorm<R> res = ( p->*fn )( Conv<I>::Get( std::get<I>( st ) )..., out );
			if( !BeIsSuccess( res ) )
			{
				return BlueInvokeFailResult( e, res );
			}
			S::RetConv::Out( out, r );
		}
		else
		{
			const BlueInvokeNorm<R> res = ( p->*fn )( Conv<I>::Get( std::get<I>( st ) )... );
			if( !BeIsSuccess( res ) )
			{
				return BlueInvokeFailResult( e, res );
			}
		}
		return BIS_OK;
	}

	static int Call( IRoot* self, const BlueInvokeValue* a, uint32_t n, BlueInvokeValue* result, BlueInvokeError* e )
	{
		BlueInvokeBegin( result, e );
		BlueInvokeScratch scratch( result );
		try
		{
			C* p = self ? dynamic_cast<C*>( self ) : nullptr;
			if( !p )
			{
				return BlueInvokeFail( e, BIS_SELF, "self is not an instance of the method's class" );
			}
			constexpr uint32_t maxArgs = static_cast<uint32_t>( S::visible );
			constexpr uint32_t minArgs = maxArgs - static_cast<uint32_t>( NOpt );
			if( n < minArgs || n > maxArgs || ( n && !a ) )
			{
				return BlueInvokeFail( e, BIS_ARGC, "expects %u to %u arguments, got %u", minArgs, maxArgs, n );
			}
			const int rc = Invoke( p, a, n, scratch.target, e, std::make_index_sequence<S::visible>() );
			if( rc != BIS_OK && result )
			{
				BlueInvokeValueClear( result );
			}
			return rc;
		}
		catch( const std::exception& x )
		{
			if( result )
			{
				BlueInvokeValueClear( result );
			}
			return BlueInvokeFail( e, BIS_EXCEPTION, "C++ exception: %s", x.what() );
		}
		catch( ... )
		{
			if( result )
			{
				BlueInvokeValueClear( result );
			}
			return BlueInvokeFail( e, BIS_EXCEPTION, "unknown C++ exception" );
		}
	}

	static constexpr BlueInvokeMethodFn value = &Call;
};

template <typename MemFn, MemFn fn, int NOpt>
constexpr BlueInvokeMethod BlueInvokeMakeMethod( const char* name )
{
	using S = BlueInvokeMethodSig<MemFn, NOpt>;
	BlueInvokeMethod m{};
	m.name = name;
	m.fn = BlueInvokeThunk<MemFn, fn, NOpt>::value;
	m.numOptional = NOpt > 0 ? static_cast<uint32_t>( NOpt ) : 0u;
	m.returnTag = BIV_UNSUPPORTED;
	if constexpr( S::valid )
	{
		if constexpr( S::N <= BLUE_INVOKE_MAX_ARGS )
		{
			m.arity = static_cast<uint32_t>( S::visible );
			m.returnTag = S::RetConv::tag;
			m.argTypeNames[0] = S::RetConv::typeName;
			m.argEnumTypes[0] = S::RetConv::enumType;
			m.flags = ( S::beResult ? BIM_BE_RESULT : 0u ) | ( S::outKind == BIO_OBJECT ? BIM_OUT_OBJECT : 0u ) |
					  ( S::outKind == BIO_REF ? BIM_OUT_REF : 0u ) | BlueInvokeRefInputFlag<S>( std::make_index_sequence<S::visible>() );
			BlueInvokeFillArgs<S>( m, std::make_index_sequence<S::visible>() );
		}
	}
	return m;
}

//--------------------------------------------------------------------------------------------
// Property thunks (MAP_PROPERTY*: self is cast to the exposing class, as BluePropertyGetter does)
//--------------------------------------------------------------------------------------------

template <typename Tr, bool BeRes, size_t N = Tr::N>
struct BlueInvokeGetterValue
{
	using type = void; // wrong shape
};
template <typename Tr>
struct BlueInvokeGetterValue<Tr, false, 0>
{
	using type = std::conditional_t<std::is_void<typename Tr::Ret>::value, void, BlueInvokeNorm<typename Tr::Ret>>;
};
template <typename Tr>
struct BlueInvokeGetterValue<Tr, true, 1>
{
	using A = std::tuple_element_t<0, typename Tr::Args>;
	using type = std::conditional_t<std::is_reference<A>::value, BlueInvokeNorm<A>, void>;
};

template <typename GetFn, bool = BlueInvokeMemFn<GetFn>::valid>
struct BlueInvokeGetterSig
{
	static constexpr bool beResult = false;
	using V = void;
	using VConv = BlueInvokeUnsupportedConv;
};
template <typename GetFn>
struct BlueInvokeGetterSig<GetFn, true>
{
	using Tr = BlueInvokeMemFn<GetFn>;
	static constexpr bool beResult = BlueInvokeIsBeResult<BlueInvokeNorm<typename Tr::Ret>>::value;
	using V = typename BlueInvokeGetterValue<Tr, beResult>::type;
	using VConv = std::conditional_t<std::is_void<V>::value, BlueInvokeUnsupportedConv, BlueInvokeConv<V>>;
};

template <typename Class, typename GetFn>
constexpr bool BlueInvokeGetterSupported()
{
	using G = BlueInvokeGetterSig<GetFn>;
	if constexpr( std::is_void<typename G::V>::value || !G::VConv::out )
	{
		return false;
	}
	else if constexpr( G::beResult )
	{
		return std::is_invocable<GetFn, Class*, typename G::V&>::value;
	}
	else
	{
		return std::is_invocable<GetFn, Class*>::value;
	}
}

template <typename SetFn, bool = BlueInvokeMemFn<SetFn>::valid>
struct BlueInvokeSetterSig
{
	static constexpr bool beResult = false;
};
template <typename SetFn>
struct BlueInvokeSetterSig<SetFn, true>
{
	using Tr = BlueInvokeMemFn<SetFn>;
	static constexpr bool beResult = BlueInvokeIsBeResult<BlueInvokeNorm<typename Tr::Ret>>::value;
};

template <typename SetFn, bool = BlueInvokeMemFn<SetFn>::valid>
struct BlueInvokeSetterConv
{
	using type = BlueInvokeUnsupportedConv;
};
template <typename SetFn>
struct BlueInvokeSetterConv<SetFn, true>
{
	using Tr = BlueInvokeMemFn<SetFn>;
	using A = typename BlueInvokeLastParam<typename Tr::Args, Tr::N == 1>::type;
	using type = std::conditional_t<std::is_void<A>::value, BlueInvokeUnsupportedConv, BlueInvokeConv<BlueInvokeNorm<A>>>;
};

template <typename Class, typename SetFn>
constexpr bool BlueInvokeSetterSupported()
{
	using AC = typename BlueInvokeSetterConv<SetFn>::type;
	if constexpr( !AC::in )
	{
		return false;
	}
	else
	{
		return std::is_invocable<SetFn, Class*, BlueInvokeGetType<AC>>::value;
	}
}

template <typename Class, typename GetFn, GetFn g, bool = BlueInvokeGetterSupported<Class, GetFn>()>
struct BlueInvokeGetThunk
{
	static constexpr BlueInvokeGetFn value = nullptr;
};

template <typename Class, typename GetFn, GetFn g>
struct BlueInvokeGetThunk<Class, GetFn, g, true>
{
	using G = BlueInvokeGetterSig<GetFn>;
	static int Call( IRoot* self, BlueInvokeValue* out, BlueInvokeError* e )
	{
		BlueInvokeBegin( out, e );
		if( !out )
		{
			return BlueInvokeFail( e, BIS_ARGC, "getter needs an output value" );
		}
		try
		{
			Class* p = self ? dynamic_cast<Class*>( self ) : nullptr;
			if( !p )
			{
				return BlueInvokeFail( e, BIS_SELF, "self is not an instance of the property's class" );
			}
			if constexpr( G::beResult )
			{
				typename G::V v{};
				const auto res = ( p->*g )( v );
				if( !BeIsSuccess( res ) )
				{
					return BlueInvokeFailResult( e, res );
				}
				G::VConv::Out( v, out );
			}
			else
			{
				decltype( auto ) v = ( p->*g )();
				G::VConv::Out( v, out );
			}
			return BIS_OK;
		}
		catch( const std::exception& x )
		{
			BlueInvokeValueClear( out );
			return BlueInvokeFail( e, BIS_EXCEPTION, "C++ exception: %s", x.what() );
		}
		catch( ... )
		{
			BlueInvokeValueClear( out );
			return BlueInvokeFail( e, BIS_EXCEPTION, "unknown C++ exception" );
		}
	}
	static constexpr BlueInvokeGetFn value = &Call;
};

template <typename Class, typename SetFn, SetFn s, bool = BlueInvokeSetterSupported<Class, SetFn>()>
struct BlueInvokeSetThunk
{
	static constexpr BlueInvokeSetFn value = nullptr;
};

template <typename Class, typename SetFn, SetFn s>
struct BlueInvokeSetThunk<Class, SetFn, s, true>
{
	using AC = typename BlueInvokeSetterConv<SetFn>::type;
	static constexpr bool beResult = BlueInvokeIsBeResult<BlueInvokeNorm<typename BlueInvokeMemFn<SetFn>::Ret>>::value;
	static int Call( IRoot* self, const BlueInvokeValue* in, BlueInvokeError* e )
	{
		BlueInvokeBegin( nullptr, e );
		if( !in )
		{
			return BlueInvokeFail( e, BIS_ARGC, "setter needs an input value" );
		}
		try
		{
			Class* p = self ? dynamic_cast<Class*>( self ) : nullptr;
			if( !p )
			{
				return BlueInvokeFail( e, BIS_SELF, "self is not an instance of the property's class" );
			}
			typename AC::Storage st{};
			const int rc = AC::In( *in, st, e, 1 );
			if( rc != BIS_OK )
			{
				return rc;
			}
			if constexpr( beResult )
			{
				const auto res = ( p->*s )( AC::Get( st ) );
				if( !BeIsSuccess( res ) )
				{
					return BlueInvokeFailResult( e, res );
				}
			}
			else
			{
				( p->*s )( AC::Get( st ) );
			}
			return BIS_OK;
		}
		catch( const std::exception& x )
		{
			return BlueInvokeFail( e, BIS_EXCEPTION, "C++ exception: %s", x.what() );
		}
		catch( ... )
		{
			return BlueInvokeFail( e, BIS_EXCEPTION, "unknown C++ exception" );
		}
	}
	static constexpr BlueInvokeSetFn value = &Call;
};

template <typename Class, typename GetFn, GetFn g>
constexpr BlueInvokeProperty BlueInvokeMakePropertyRO( const char* name )
{
	using G = BlueInvokeGetterSig<GetFn>;
	BlueInvokeProperty p{};
	p.name = name;
	p.get = BlueInvokeGetThunk<Class, GetFn, g>::value;
	p.set = nullptr;
	p.typeTag = G::VConv::tag;
	p.typeName = G::VConv::typeName;
	p.enumType = G::VConv::enumType;
	p.flags = BIP_READONLY | ( G::beResult ? BIP_BE_RESULT_GET : 0u );
	p.sigHash = BlueInvokeFnvMix( BlueInvokeFnvMix( kBlueInvokeFnvOffset, G::VConv::tag ), G::VConv::typeHash );
	return p;
}

template <typename Class, typename GetFn, GetFn g, typename SetFn, SetFn s>
constexpr BlueInvokeProperty BlueInvokeMakeProperty( const char* name )
{
	BlueInvokeProperty p = BlueInvokeMakePropertyRO<Class, GetFn, g>( name );
	p.set = BlueInvokeSetThunk<Class, SetFn, s>::value;
	p.flags = ( p.flags & ~BIP_READONLY ) | ( BlueInvokeSetterSig<SetFn>::beResult ? BIP_BE_RESULT_SET : 0u );
	return p;
}

// EnumTypeRegistration's hook: copies upstream's chooser array (terminated by mKey == nullptr,
// value in mValue.mLong) into a leaked BlueInvokeEnumTable. Runs once per exposed enum and DSO.
template <typename T>
inline void BlueInvokeRegisterEnum( const char* typeName, const Be::VarChooser* chooser, const char* buildId )
{
	if( !typeName || !chooser )
	{
		return;
	}
	uint32_t n = 0;
	while( chooser[n].mKey )
	{
		++n;
	}
	BlueInvokeEnumValue* values = new BlueInvokeEnumValue[n ? n : 1];
	for( uint32_t i = 0; i < n; ++i )
	{
		values[i] = BlueInvokeEnumValue{ chooser[i].mKey, static_cast<int64_t>( chooser[i].mValue.mLong ), chooser[i].mDescription };
	}
	const BlueInvokeEnumTable* t = new BlueInvokeEnumTable{ BLUE_INVOKE_MAGIC,
															static_cast<uint32_t>( sizeof( BlueInvokeValue ) ),
															static_cast<uint32_t>( sizeof( BlueInvokeEnumValue ) ),
															n,
															buildId,
															values,
															BlueInvokeTypeName<T>::value,
															&typeid( T ) };
	BlueInvokeRegister( typeName, &typeid( T ), BLUE_INVOKE_CAP_ENUMS, t );
}

#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC visibility pop
#endif

//--------------------------------------------------------------------------------------------
// Per-class builder and registration. Outside the hidden region on purpose: the builder is a
// function-local static of ExposeToBlue, and for a class template's ExposeToBlue the compiler makes
// its guard process-unique (STB_GNU_UNIQUE), like upstream's own statics there; the variable must
// then be unique too, which a hidden type would prevent.
//--------------------------------------------------------------------------------------------

// The type is default-visible, its functions are not: each DSO uses the Add/FinishClass compiled
// against its own copy of this header, and none can be interposed by another DSO's copy.
#if defined( __GNUC__ ) || defined( __clang__ )
#define BLUE_INVOKE_VISIBLE_TYPE __attribute__( ( visibility( "default" ) ) )
#define BLUE_INVOKE_HIDDEN_FN __attribute__( ( visibility( "hidden" ) ) )
#else
#define BLUE_INVOKE_VISIBLE_TYPE
#define BLUE_INVOKE_HIDDEN_FN
#endif

struct BLUE_INVOKE_VISIBLE_TYPE BlueInvokeClassBuilder
{
	std::vector<const void*> methods;
	std::vector<const void*> props;
	uint32_t methodsUnsupported = 0;
	uint32_t propsUnsupported = 0;

	BLUE_INVOKE_HIDDEN_FN BlueInvokeClassBuilder() {}
	BLUE_INVOKE_HIDDEN_FN void Add( const BlueInvokeMethod* m )
	{
		methods.push_back( m );
		if( !m->fn )
		{
			++methodsUnsupported;
		}
	}
	BLUE_INVOKE_HIDDEN_FN void Add( const BlueInvokeProperty* p )
	{
		props.push_back( p );
		if( !p->get || ( !( p->flags & BIP_READONLY ) && !p->set ) )
		{
			++propsUnsupported;
		}
	}
};

// Called once per class at the end of ExposeToBlue. Tables and the path are leaked on purpose:
// they live as long as the process (N8). The builder's vectors are never touched again.
BLUE_INVOKE_HIDDEN_FN inline void BlueInvokeFinishClass( BlueInvokeClassBuilder& b, const char* module, const char* name, const void* classInfo, const char* buildId )
{
	if( b.methods.empty() && b.props.empty() )
	{
		return;
	}
	std::string* path = new std::string( module ? module : "" );
	path->push_back( '.' );
	path->append( name ? name : "" );
	if( !b.methods.empty() )
	{
		BlueInvokeClassTable* t = new BlueInvokeClassTable{ BLUE_INVOKE_MAGIC,
															static_cast<uint32_t>( sizeof( BlueInvokeValue ) ),
															static_cast<uint32_t>( sizeof( BlueInvokeMethod ) ),
															static_cast<uint32_t>( b.methods.size() ),
															buildId,
															b.methods.data(),
															b.methodsUnsupported };
		BlueInvokeRegister( path->c_str(), classInfo, BLUE_INVOKE_CAP_METHODS, t );
	}
	if( !b.props.empty() )
	{
		BlueInvokeClassTable* t = new BlueInvokeClassTable{ BLUE_INVOKE_MAGIC,
															static_cast<uint32_t>( sizeof( BlueInvokeValue ) ),
															static_cast<uint32_t>( sizeof( BlueInvokeProperty ) ),
															static_cast<uint32_t>( b.props.size() ),
															buildId,
															b.props.data(),
															b.propsUnsupported };
		BlueInvokeRegister( path->c_str(), classInfo, BLUE_INVOKE_CAP_PROPS, t );
	}
}


//--------------------------------------------------------------------------------------------
// The hook statements. One per exposure macro, one in EnumTypeRegistration.
//--------------------------------------------------------------------------------------------

#if BLUE_INVOKE_DISABLE

#define BLUE_INVOKE_BEGIN_CLASS()
#define BLUE_INVOKE_ADD_METHOD( name, functionName, numOptional ) do {} while( 0 )
#define BLUE_INVOKE_ADD_PROPERTY( nameString, getterFunction, setterFunction ) do {} while( 0 )
#define BLUE_INVOKE_ADD_PROPERTY_RO( nameString, getterFunction ) do {} while( 0 )
#define BLUE_INVOKE_END_CLASS( clsid, classInfo ) do {} while( 0 )
#define BLUE_INVOKE_REGISTER_ENUM( typeName, T, chooser ) do {} while( 0 )

#else

// EXPOSURE_BEGIN_IMP: the per-class builder, leaked like every other exposure static.
#define BLUE_INVOKE_BEGIN_CLASS() \
	static BlueInvokeClassBuilder& s_blueInvoke = *new BlueInvokeClassBuilder

// MAP_METHOD_AND_WRAP / MAP_METHOD_AND_WRAP_OPTIONAL_ARGS. `static const`, not constexpr: a
// non-literal name then falls back to a guarded initialisation instead of a hard error.
#define BLUE_INVOKE_ADD_METHOD( name, functionName, numOptional ) \
	do { \
		static const BlueInvokeMethod s_blueInvokeEntry = \
			BlueInvokeMakeMethod<decltype( &_Class::functionName ), &_Class::functionName, ( numOptional )>( name ); \
		s_blueInvoke.Add( &s_blueInvokeEntry ); \
	} while( 0 )

// MAP_PROPERTY / MAP_PROPERTY_PERSISTED
#define BLUE_INVOKE_ADD_PROPERTY( nameString, getterFunction, setterFunction ) \
	do { \
		static const BlueInvokeProperty s_blueInvokeEntry = BlueInvokeMakeProperty<_Class, \
			decltype( &_Class::getterFunction ), &_Class::getterFunction, \
			decltype( &_Class::setterFunction ), &_Class::setterFunction>( nameString ); \
		s_blueInvoke.Add( &s_blueInvokeEntry ); \
	} while( 0 )

// MAP_PROPERTY_READONLY
#define BLUE_INVOKE_ADD_PROPERTY_RO( nameString, getterFunction ) \
	do { \
		static const BlueInvokeProperty s_blueInvokeEntry = BlueInvokeMakePropertyRO<_Class, \
			decltype( &_Class::getterFunction ), &_Class::getterFunction>( nameString ); \
		s_blueInvoke.Add( &s_blueInvokeEntry ); \
	} while( 0 )

// EXPOSURE_END_IMPL, before `s_classInfo = &_classinfo;`. BLUE_INVOKE_BUILD_ID expands here, in
// the consumer's TU, so each table carries the ID its own DSO was compiled against.
#define BLUE_INVOKE_END_CLASS( clsid, classInfo ) \
	BlueInvokeFinishClass( s_blueInvoke, ( clsid ).GetModule(), ( clsid ).GetName(), ( classInfo ), BLUE_INVOKE_BUILD_ID )

// EnumTypeRegistration's constructor, before `while( c->mKey )` advances the chooser pointer.
#define BLUE_INVOKE_REGISTER_ENUM( typeName, T, chooser ) \
	BlueInvokeRegisterEnum<T>( ( typeName ), ( chooser ), BLUE_INVOKE_BUILD_ID )

#endif // BLUE_INVOKE_DISABLE

#endif // !BLUE_INVOKE_C_API_ONLY

#endif // BlueInvoke_h
