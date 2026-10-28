// Copyright (c) 2026 CCP Games
//
// Helpers for the C-ABI tests: building argument values, and calling methods, module
// functions and members only through the C-ABI tables (ClassInfo::mCMethodTable,
// VarEntry::mCGet / mCSet, BlueRegistration::GetCFuncRegs).

#pragma once
#ifndef TestCAbiUtil_h
#define TestCAbiUtil_h

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

extern "C" size_t BlueCAbiTestCLayout( size_t* out, size_t capacity );
extern "C" void BlueCAbiTestCMakeArguments( BlueCValue* out, const char* text, size_t textLength, double number );

namespace CAbiTest
{

//--------------------------------------------------------------------
// Borrowed input values (the caller owns whatever they point to)
//--------------------------------------------------------------------

inline BlueCValue None()
{
	BlueCValue v;
	BlueCValueSetNone( v );
	return v;
}

inline BlueCValue Bool( bool b )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_BOOL;
	v.u.i = b ? 1 : 0;
	return v;
}

inline BlueCValue Int( int64_t i )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_INT;
	v.u.i = i;
	return v;
}

inline BlueCValue UInt( uint64_t u )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_UINT;
	v.u.u = u;
	return v;
}

inline BlueCValue Double( double d )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_DOUBLE;
	v.u.d = d;
	return v;
}

// s must outlive the value; it need not be NUL-terminated at length
inline BlueCValue Str( const char* s, size_t length )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_STRING;
	v.u.str.ptr = s;
	v.u.str.len = length;
	return v;
}

// s must outlive the value: a string literal, or a std::string that stays in scope
inline BlueCValue Str( const char* s )
{
	return Str( s, strlen( s ) );
}

inline BlueCValue Str( const std::string& s )
{
	return Str( s.data(), s.size() );
}

inline BlueCValue Vec( std::initializer_list<double> components, uint32_t elementAndTag = BLUE_VEC_ELEMENT_FLOAT )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_VECTOR;
	v.aux = uint32_t( components.size() ) | elementAndTag;
	size_t i = 0;
	for( double c : components )
	{
		v.u.vec.v[i++] = c;
	}
	return v;
}

// items must outlive the value
inline BlueCValue Array( std::vector<BlueCValue>& items )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_ARRAY;
	v.u.arr.items = items.empty() ? nullptr : items.data();
	v.u.arr.count = items.size();
	return v;
}

// kv (key, value, key, value, ...) must outlive the value
inline BlueCValue Map( std::vector<BlueCValue>& kv )
{
	BlueCValue v = None();
	v.kind = BLUE_VK_MAP;
	v.u.map.kv = kv.empty() ? nullptr : kv.data();
	v.u.map.count = kv.size() / 2;
	return v;
}

template< typename T >
BlueObject* AsC( T* obj )
{
	// The C ABI's view of an object, as BlueCWrap produces it
	return reinterpret_cast<BlueObject*>( reinterpret_cast<IRoot*>( obj ) );
}

inline BlueCValue Obj( IRoot* obj )
{
	BlueCValue v = None();
	if( obj )
	{
		v.kind = BLUE_VK_OBJECT;
		v.u.obj = reinterpret_cast<BlueObject*>( obj );
	}
	return v;
}

template< typename T >
BlueCValue Obj( T* obj )
{
	return Obj( reinterpret_cast<IRoot*>( obj ) );
}

inline BlueCValue Callback( BlueCallbackHandle* handle )
{
	BlueCValue v = None();
	if( handle )
	{
		v.kind = BLUE_VK_CALLBACK;
		v.u.callback = handle;
	}
	return v;
}

inline std::string AsString( const BlueCValue& v )
{
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_STRING ) );
	if( v.kind != BLUE_VK_STRING )
	{
		return std::string();
	}
	EXPECT_EQ( v.u.str.ptr[v.u.str.len], 0 ) << "strings blue produces are NUL-terminated";
	return std::string( v.u.str.ptr, v.u.str.len );
}

//--------------------------------------------------------------------
// The result of a call: status, owned output and error
//--------------------------------------------------------------------

struct Result
{
	int32_t status = BLUE_OK;
	BlueCValue out;
	BlueCError err = { BLUE_OK, nullptr, 0 };

	Result() { BlueCValueSetNone( out ); }
	~Result()
	{
		BlueCValueRelease( &out );
		BlueCErrorClear( &err );
	}
	Result( const Result& ) = delete;
	Result& operator=( const Result& ) = delete;

	bool Ok() const { return status == BLUE_OK; }
	std::string Message() const { return err.message ? std::string( err.message, err.message_len ) : std::string(); }
	uint32_t Kind() const { return out.kind; }
	int64_t I() const { return out.u.i; }
	uint64_t U() const { return out.u.u; }
	double D() const { return out.u.d; }
	std::string S() const { return AsString( out ); }
	IRoot* O() const { return out.kind == BLUE_VK_OBJECT ? reinterpret_cast<IRoot*>( out.u.obj ) : nullptr; }
};

// Status and message of a failed call, for gtest output
inline std::string Describe( const Result& r )
{
	return "status " + std::to_string( r.status ) + ": " + r.Message();
}

#define EXPECT_CABI_OK( r ) EXPECT_EQ( ( r ).status, int32_t( BLUE_OK ) ) << CAbiTest::Describe( r )
#define ASSERT_CABI_OK( r ) ASSERT_EQ( ( r ).status, int32_t( BLUE_OK ) ) << CAbiTest::Describe( r )
#define EXPECT_CABI_ERROR( r, expected ) \
	do \
	{ \
		EXPECT_EQ( ( r ).status, int32_t( expected ) ) << CAbiTest::Describe( r ); \
		EXPECT_EQ( ( r ).err.code, int32_t( expected ) ); \
		EXPECT_NE( ( r ).err.message, nullptr ); \
		EXPECT_EQ( ( r ).Kind(), uint32_t( BLUE_VK_NONE ) ); \
	} while( 0 )

//--------------------------------------------------------------------
// Calls through the C tables only
//--------------------------------------------------------------------

// Call method name of obj's class (or a parent), found through ClassInfo::mCMethodTable
inline void Call( Result& r, IRoot* obj, const char* name, std::vector<BlueCValue> args = {} )
{
	const BlueCMethodDef* def = BlueCFindMethod( obj->ClassType(), name, strlen( name ) );
	ASSERT_NE( def, nullptr ) << "no C method " << name;
	r.status = def->fn( reinterpret_cast<BlueObject*>( obj ), args.empty() ? nullptr : args.data(), uint32_t( args.size() ), &r.out, &r.err );
}

template< typename T >
void Call( Result& r, T* obj, const char* name, std::vector<BlueCValue> args = {} )
{
	Call( r, reinterpret_cast<IRoot*>( obj ), name, std::move( args ) );
}

// Call module function name, found in the C function list
inline void CallFunction( Result& r, const char* name, std::vector<BlueCValue> args = {} )
{
	const BlueCMethodDef* def = BlueCFindFunction( BlueRegistration::GetCFuncRegs(), name, strlen( name ) );
	ASSERT_NE( def, nullptr ) << "no C function " << name;
	r.status = def->fn( nullptr, args.empty() ? nullptr : args.data(), uint32_t( args.size() ), &r.out, &r.err );
}

template< typename T >
void Get( Result& r, T* obj, const char* name )
{
	r.status = BlueCGetMember( reinterpret_cast<IRoot*>( obj ), name, strlen( name ), &r.out, &r.err );
}

template< typename T >
void Set( Result& r, T* obj, const char* name, const BlueCValue& value )
{
	r.status = BlueCSetMember( reinterpret_cast<IRoot*>( obj ), name, strlen( name ), &value, &r.err );
}

// Set and expect success
template< typename T >
void SetOk( T* obj, const char* name, const BlueCValue& value )
{
	Result r;
	Set( r, obj, name, value );
	EXPECT_CABI_OK( r ) << " setting " << name;
}

// Reference count of a Blue object (through IRoot, which some test classes inherit twice)
template< typename T >
int RefCount( T* obj )
{
	return reinterpret_cast<IRoot*>( obj )->GetRefCount();
}

// Live instances of a class, for leak checks
template< typename T >
uint32_t LiveCount()
{
	return T::ClassType_()->mLiveCount;
}

} // namespace CAbiTest

#endif // TestCAbiUtil_h
