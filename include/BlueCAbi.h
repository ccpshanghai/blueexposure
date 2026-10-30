/* Copyright (c) 2026 CCP Games
 *
 * Plain C types of the C-ABI exposure backend (BLUE_WITH_CABI). Every class exposed with the
 * EXPOSURE_BEGIN / MAP_* macros gets C thunks with the signatures below; values cross the
 * boundary as BlueCValue and failures as BlueCError. The Blue C API (blue's include/blue_c)
 * re-exports these types, so this header is the single source of truth for their layout and
 * must stay includable from C (C99 or later) as well as from C++.
 *
 * Ownership:
 *   - Inputs (thunk arguments, values passed to setters) are borrowed for the duration of the
 *     call; the caller's reference to an object or callback in an input value is neither
 *     taken nor released (the callee adds its own reference if it keeps one).
 *   - Outputs (a thunk's out value, BlueCError.message) belong to the caller and are freed
 *     with BlueCValueRelease / BlueCErrorClear. An object or callback in an output value carries
 *     one reference (+1).
 *   - A host callback's result (its out value and err.message) goes the other way: blue takes
 *     it over and frees it, so the host builds it with blue's allocation helpers (see Callbacks
 *     into the host below). Blue never frees memory the host allocated.
 *   - Strings are UTF-8 with an explicit length. Strings blue produces are additionally
 *     NUL-terminated; strings passed in need not be.
 *   - Bytes (BLUE_VK_BYTES) are binary data with an explicit length and no terminator.
 */

#pragma once
#ifndef BlueCAbi_h
#define BlueCAbi_h

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the layouts and conventions in this header: (major << 16) | minor. A major bump
 * is a breaking change. */
#define BLUE_CABI_VERSION_MAJOR 1
#define BLUE_CABI_VERSION_MINOR 0
#define BLUE_CABI_VERSION ( ( BLUE_CABI_VERSION_MAJOR << 16 ) | BLUE_CABI_VERSION_MINOR )

/* An exposed object. Opaque on the C side; it is an IRoot* of the object. */
typedef struct BlueObject BlueObject;

/* A host callback (BlueCCallback) wrapped for blue. Opaque and reference counted, like an
 * object: values borrow it, blue takes its own reference when it keeps it. */
typedef struct BlueCallbackHandle BlueCallbackHandle;

/*--------------------------------------------------------------------------------------------
 * Status codes. Every thunk returns one of these; BLUE_OK is 0 and every error is negative.
 *------------------------------------------------------------------------------------------*/
typedef enum BlueCStatus
{
	BLUE_OK = 0,
	BLUE_E_FAILED = -1,				/* The exposed function reported failure (Be::Result) */
	BLUE_E_ARGUMENT_COUNT = -2,		/* Too few or too many arguments */
	BLUE_E_TYPE = -3,				/* A value has the wrong kind for its C++ type */
	BLUE_E_OVERFLOW = -4,			/* A number does not fit the C++ type */
	BLUE_E_INVALID_ARGUMENT = -5,	/* Null where a value is required, bad UTF-8, ... */
	BLUE_E_SELF = -6,				/* Missing object, or not an instance of the method's class */
	BLUE_E_UNSUPPORTED = -7,		/* The C++ type has no C-ABI representation */
	BLUE_E_READ_ONLY = -8,			/* Member cannot be assigned */
	BLUE_E_NOT_FOUND = -9,			/* No member, method or class of that name */
	BLUE_E_EXCEPTION = -10,			/* A C++ exception was caught at the boundary */
	BLUE_E_OUT_OF_MEMORY = -11,
	BLUE_E_WRONG_THREAD = -12,		/* Reserved for the Blue C API */
	BLUE_E_NOT_INITIALIZED = -13	/* Reserved for the Blue C API */
} BlueCStatus;

/*--------------------------------------------------------------------------------------------
 * Values
 *------------------------------------------------------------------------------------------*/
typedef enum BlueCValueKind
{
	BLUE_VK_NONE = 0,		/* No value; also a null object */
	BLUE_VK_BOOL = 1,		/* u.i is 0 or 1 */
	BLUE_VK_INT = 2,		/* u.i */
	BLUE_VK_UINT = 3,		/* u.u */
	BLUE_VK_DOUBLE = 4,		/* u.d */
	BLUE_VK_STRING = 5,		/* u.str, UTF-8 */
	BLUE_VK_OBJECT = 6,		/* u.obj, never null (a null object is BLUE_VK_NONE) */
	BLUE_VK_VECTOR = 7,		/* aux describes it; u.vec for up to 4 components, u.arr otherwise */
	BLUE_VK_ARRAY = 8,		/* u.arr: std::vector, std::list, std::pair */
	BLUE_VK_MAP = 9,		/* u.map: std::map */
	BLUE_VK_CALLBACK = 10,	/* u.callback, never null (a null callback is BLUE_VK_NONE) */
	BLUE_VK_BYTES = 11		/* u.str, binary data (any bytes, not NUL-terminated) */
} BlueCValueKind;

/* aux of a BLUE_VK_VECTOR: component count | element type | tag. Components are always stored
 * as doubles; the element type says what the C++ side holds. A vector with more than four
 * components (only Matrix, 16 components in row-major order) stores them as BLUE_VK_DOUBLE
 * values in u.arr instead of u.vec. Every other kind has aux 0. */
#define BLUE_VEC_COUNT_MASK		0x000000FFu
#define BLUE_VEC_ELEMENT_MASK	0x00000F00u
#define BLUE_VEC_ELEMENT_FLOAT	0x00000100u		/* float components (Vector2/3/4, Color, ...) */
#define BLUE_VEC_ELEMENT_DOUBLE	0x00000200u		/* double components (Vector2d/3d/4d) */
#define BLUE_VEC_ELEMENT_INT32	0x00000300u		/* int components (Vector3i) */
#define BLUE_VEC_TAG_MASK		0x0000F000u
#define BLUE_VEC_TAG_NONE		0x00000000u
#define BLUE_VEC_TAG_COLOR		0x00001000u
#define BLUE_VEC_TAG_QUATERNION	0x00002000u
#define BLUE_VEC_TAG_MATRIX		0x00003000u

typedef struct BlueCValue BlueCValue;

struct BlueCValue
{
	uint32_t kind;		/* BlueCValueKind */
	uint32_t aux;		/* BLUE_VK_VECTOR: BLUE_VEC_* bits; otherwise 0 */
	union
	{
		int64_t i;
		uint64_t u;
		double d;
		struct
		{
			const char* ptr;
			size_t len;
		} str;					/* BLUE_VK_STRING and BLUE_VK_BYTES */
		BlueObject* obj;
		BlueCallbackHandle* callback;
		struct
		{
			double v[4];
		} vec;
		struct
		{
			BlueCValue* items;
			size_t count;
		} arr;
		struct
		{
			BlueCValue* kv;		/* kv[2n] is a key, kv[2n + 1] its value */
			size_t count;		/* number of pairs */
		} map;
	} u;
};

/*--------------------------------------------------------------------------------------------
 * Errors
 *------------------------------------------------------------------------------------------*/
typedef struct BlueCError
{
	int32_t code;			/* BlueCStatus */
	char* message;			/* UTF-8, NUL-terminated, or null; owned by the caller */
	size_t message_len;
} BlueCError;

/*--------------------------------------------------------------------------------------------
 * Thunks
 *
 * Each returns BLUE_OK or a negative BlueCStatus; on error it also fills err (when err is not
 * null) and leaves out as BLUE_VK_NONE. out may be null to discard the result. Module
 * functions use BlueCMethodFn and ignore self.
 *
 * out is overwritten without being released. err must be initialised ({ BLUE_OK, null, 0 })
 * or hold an earlier error: filling it frees the message it holds. On success err is left as
 * it was, so the returned status, not err.code, says whether a call failed.
 *------------------------------------------------------------------------------------------*/
typedef int32_t ( *BlueCMethodFn )( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err );
typedef int32_t ( *BlueCGetFn )( BlueObject* self, BlueCValue* out, BlueCError* err );
typedef int32_t ( *BlueCSetFn )( BlueObject* self, const BlueCValue* in, BlueCError* err );

/* One exposed method or module function. Tables end with an entry whose name is null. */
typedef struct BlueCMethodDef
{
	const char* name;
	BlueCMethodFn fn;
	const char* doc;
	uint32_t optionalCount;		/* how many trailing arguments may be omitted */
} BlueCMethodDef;

/*--------------------------------------------------------------------------------------------
 * Callbacks into the host. The host wraps one in a BlueCallbackHandle (BlueCCallbackCreate,
 * holding one reference) and passes the handle in BLUE_VK_CALLBACK values. destroy (may be
 * null) is called exactly once, when the last reference - the host's or blue's - is released.
 *
 * fn is called on the thread blue calls it from, with:
 *   - args borrowed for the duration of the call;
 *   - out and err never null. out is BLUE_VK_NONE and err is { BLUE_OK, null, 0 } on entry.
 * It returns BLUE_OK or a negative BlueCStatus. Any status other than BLUE_OK is a failure; a
 * positive one is reported as BLUE_E_FAILED.
 *
 * Returning a value: fn writes it to out, and gives it to blue. Blue converts what it needs and
 * then frees the value with BlueCValueRelease - also when the call failed, or the value has the
 * wrong kind - so everything in it must be freeable by blue:
 *   - strings, bytes, arrays and maps are allocated with blue's helpers (BlueCValueSetString,
 *     BlueCValueSetBytes, BlueCValueSetArray, BlueCValueSetMap; the Blue C API re-exports them),
 *     never with the host's allocator. Items of an array or map are filled in place, the same way;
 *   - an object carries one reference that blue takes over (add one first to keep your own);
 *   - a callback handle likewise carries one reference (BlueCCallbackCreate, or an added one);
 *   - a matrix is returned as an ARRAY of 16 numbers (u.vec holds at most four components).
 *
 * Reporting failure: fn returns the status and may describe it in err, with a message allocated
 * by blue (BlueCSetErrorMessage, re-exported by the Blue C API), never by the host. Without a
 * message blue reports the status alone. err is ignored when fn returns BLUE_OK.
 *------------------------------------------------------------------------------------------*/
typedef int32_t ( *BlueCCallbackFn )( void* user_data, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err );

typedef struct BlueCCallback
{
	BlueCCallbackFn fn;
	void* user_data;
	void ( *destroy )( void* user_data );
} BlueCCallback;

#ifdef __cplusplus
}
#endif

/*--------------------------------------------------------------------------------------------
 * Layout pins (x64 / LP64). The .NET mirror (DotBlue.Interop) asserts the same numbers.
 *------------------------------------------------------------------------------------------*/
#if defined( __cplusplus )
#define BLUE_CABI_STATIC_ASSERT( cond, msg ) static_assert( cond, msg )
#elif defined( __STDC_VERSION__ ) && __STDC_VERSION__ >= 201112L
#define BLUE_CABI_STATIC_ASSERT( cond, msg ) _Static_assert( cond, msg )
#else
/* C99: an array of negative size fails to compile; a declaration, so the ';' after it is valid */
#define BLUE_CABI_CONCAT_( a, b ) a##b
#define BLUE_CABI_CONCAT( a, b ) BLUE_CABI_CONCAT_( a, b )
#define BLUE_CABI_STATIC_ASSERT( cond, msg ) typedef char BLUE_CABI_CONCAT( BlueCAbiStaticAssert, __LINE__ )[( cond ) ? 1 : -1]
#endif

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
BLUE_CABI_STATIC_ASSERT( sizeof( BlueCValue ) == 40, "BlueCValue must be 40 bytes" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, kind ) == 0, "BlueCValue.kind offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, aux ) == 4, "BlueCValue.aux offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u ) == 8, "BlueCValue.u offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.str.ptr ) == 8, "BlueCValue.u.str.ptr offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.callback ) == 8, "BlueCValue.u.callback offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.str.len ) == 16, "BlueCValue.u.str.len offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.vec.v ) == 8, "BlueCValue.u.vec offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.arr.items ) == 8, "BlueCValue.u.arr.items offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.arr.count ) == 16, "BlueCValue.u.arr.count offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.map.kv ) == 8, "BlueCValue.u.map.kv offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCValue, u.map.count ) == 16, "BlueCValue.u.map.count offset" );
BLUE_CABI_STATIC_ASSERT( sizeof( BlueCError ) == 24, "BlueCError must be 24 bytes" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCError, code ) == 0, "BlueCError.code offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCError, message ) == 8, "BlueCError.message offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCError, message_len ) == 16, "BlueCError.message_len offset" );
BLUE_CABI_STATIC_ASSERT( sizeof( BlueCMethodDef ) == 32, "BlueCMethodDef must be 32 bytes" );
BLUE_CABI_STATIC_ASSERT( sizeof( BlueCCallback ) == 24, "BlueCCallback must be 24 bytes" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCCallback, fn ) == 0, "BlueCCallback.fn offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCCallback, user_data ) == 8, "BlueCCallback.user_data offset" );
BLUE_CABI_STATIC_ASSERT( offsetof( BlueCCallback, destroy ) == 16, "BlueCCallback.destroy offset" );
#endif

#endif /* BlueCAbi_h */
