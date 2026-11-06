// Copyright (c) 2026 CCP Games
//
// Driver for the Invoke hook's tests. Initialises an embedded Python (exposure needs it at static
// init), dlopen()s the three test DSOs and checks the registry and every thunk through the C API
// only: this file sees the layouts, never the test classes.

#define BLUE_INVOKE_C_API_ONLY 1
#include "BlueInvoke.h"
#include "BlueTypes.h" // IRoot's interface (Lock/Unlock/GetRefCount) and Be::VarChooser; no Blue code is linked

#include <Python.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <typeinfo>
#include <vector>

namespace
{
int g_failures = 0;
int g_checks = 0;

#define CHECK( cond, ... ) \
	do { \
		++g_checks; \
		if( !( cond ) ) \
		{ \
			++g_failures; \
			std::fprintf( stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond ); \
			std::fprintf( stderr, __VA_ARGS__ ); \
			std::fprintf( stderr, "\n" ); \
		} \
	} while( 0 )

struct Lib
{
	void* handle = nullptr;
	IRoot* ( *create )( const char* ) = nullptr;
	const std::type_info* ( *modeType )() = nullptr;
	const std::type_info* ( *otherType )() = nullptr;
	int ( *direct )( char*, size_t ) = nullptr;
};

Lib Load( const char* path )
{
	Lib l;
	l.handle = dlopen( path, RTLD_NOW | RTLD_LOCAL );
	if( !l.handle )
	{
		std::fprintf( stderr, "dlopen %s: %s\n", path, dlerror() );
		return l;
	}
	l.create = reinterpret_cast<IRoot* ( * )( const char* )>( dlsym( l.handle, "invoke_test_create" ) );
	l.modeType = reinterpret_cast<const std::type_info* ( * )()>( dlsym( l.handle, "invoke_test_mode_type" ) );
	l.otherType = reinterpret_cast<const std::type_info* ( * )()>( dlsym( l.handle, "invoke_test_other_type" ) );
	l.direct = reinterpret_cast<int ( * )( char*, size_t )>( dlsym( l.handle, "invoke_test_direct" ) );
	return l;
}

struct Reg
{
	std::string path;
	const void* classInfo;
	std::string capability;
	const void* table;
};

std::vector<Reg> Registrations()
{
	std::vector<Reg> out;
	BlueInvokeForEach(
		[]( void* user, const char* path, const void* info, const char* cap, const void* table ) {
			static_cast<std::vector<Reg>*>( user )->push_back( Reg{ path, info, cap, table } );
		},
		&out );
	return out;
}

const BlueInvokeClassTable* Table( const std::string& path, const char* cap )
{
	return static_cast<const BlueInvokeClassTable*>( BlueInvokeFind( path.c_str(), cap ) );
}

const BlueInvokeMethod* Method( const BlueInvokeClassTable* t, const char* name )
{
	for( uint32_t i = 0; t && i < t->count; ++i )
	{
		const BlueInvokeMethod* m = static_cast<const BlueInvokeMethod*>( t->entries[i] );
		if( std::strcmp( m->name, name ) == 0 )
		{
			return m;
		}
	}
	return nullptr;
}

const BlueInvokeProperty* Property( const BlueInvokeClassTable* t, const char* name )
{
	for( uint32_t i = 0; t && i < t->count; ++i )
	{
		const BlueInvokeProperty* p = static_cast<const BlueInvokeProperty*>( t->entries[i] );
		if( std::strcmp( p->name, name ) == 0 )
		{
			return p;
		}
	}
	return nullptr;
}

BlueInvokeValue V()
{
	BlueInvokeValue v{};
	v.tag = BIV_NONE;
	return v;
}
BlueInvokeValue Int( int64_t x )
{
	BlueInvokeValue v = V();
	v.tag = BIV_INT;
	v.v.i = x;
	return v;
}
BlueInvokeValue UInt( uint64_t x )
{
	BlueInvokeValue v = V();
	v.tag = BIV_UINT;
	v.v.u = x;
	return v;
}
BlueInvokeValue Bool( bool x )
{
	BlueInvokeValue v = V();
	v.tag = BIV_BOOL;
	v.v.i = x ? 1 : 0;
	return v;
}
BlueInvokeValue Dbl( double x )
{
	BlueInvokeValue v = V();
	v.tag = BIV_DOUBLE;
	v.v.d = x;
	return v;
}
BlueInvokeValue Str( const char* s )
{
	BlueInvokeValue v = V();
	v.tag = BIV_STRING;
	v.v.s.p = s;
	v.v.s.n = std::strlen( s );
	return v;
}
BlueInvokeValue Obj( IRoot* o )
{
	BlueInvokeValue v = V();
	v.tag = BIV_OBJECT;
	v.v.obj = o;
	return v;
}
BlueInvokeValue Enum( int64_t x )
{
	BlueInvokeValue v = V();
	v.tag = BIV_ENUM;
	v.v.i = x;
	return v;
}
BlueInvokeValue Floats( std::initializer_list<float> f )
{
	BlueInvokeValue v = V();
	v.tag = BIV_FLOATS;
	v.count = static_cast<uint32_t>( f.size() );
	uint32_t i = 0;
	for( float x : f )
	{
		v.v.f[i++] = x;
	}
	return v;
}

struct Call
{
	int rc = -1;
	BlueInvokeValue r = V();
	BlueInvokeError e{};
	~Call() { BlueInvokeValueClear( &r ); }
};

void Invoke( Call& c, const BlueInvokeMethod* m, IRoot* self, std::vector<BlueInvokeValue> args )
{
	BlueInvokeValueClear( &c.r );
	if( !m || !m->fn )
	{
		c.rc = -1;
		return;
	}
	c.rc = m->fn( self, args.data(), static_cast<uint32_t>( args.size() ), &c.r, &c.e );
}

std::string AsString( const BlueInvokeValue& v )
{
	return v.tag == BIV_STRING ? std::string( v.v.s.p, v.v.s.n ) : std::string( "<not a string>" );
}

void InitPython()
{
	PyConfig config;
	PyConfig_InitIsolatedConfig( &config );
	config.site_import = 0;
	PyStatus st = PyConfig_SetBytesString( &config, &config.home, INVOKE_TEST_PYTHON_HOME );
	if( PyStatus_Exception( st ) )
	{
		Py_ExitStatusException( st );
	}
	st = Py_InitializeFromConfig( &config );
	PyConfig_Clear( &config );
	if( PyStatus_Exception( st ) )
	{
		Py_ExitStatusException( st );
	}
}

// ------------------------------------------------------------------------------------------
void TestTables( const std::string& obj, const std::string& derived )
{
	const BlueInvokeClassTable* m = Table( obj, BLUE_INVOKE_CAP_METHODS );
	const BlueInvokeClassTable* p = Table( obj, BLUE_INVOKE_CAP_PROPS );
	const BlueInvokeClassTable* dm = Table( derived, BLUE_INVOKE_CAP_METHODS );
	CHECK( m && p && dm, "tables for %s / %s", obj.c_str(), derived.c_str() );
	if( !m || !p || !dm )
	{
		return;
	}
	CHECK( m->magic == BLUE_INVOKE_MAGIC && m->sizeofValue == sizeof( BlueInvokeValue ) && m->sizeofEntry == sizeof( BlueInvokeMethod ), "methods header" );
	CHECK( p->magic == BLUE_INVOKE_MAGIC && p->sizeofEntry == sizeof( BlueInvokeProperty ), "props header" );
	CHECK( std::strcmp( m->buildId, BlueInvokeBuildId() ) == 0 && std::strcmp( m->buildId, BLUE_INVOKE_BUILD_ID ) == 0,
		   "build id %s vs lib %s vs driver %s", m->buildId, BlueInvokeBuildId(), BLUE_INVOKE_BUILD_ID );
	CHECK( m->count == 43, "method count %u", m->count );
	CHECK( m->unsupported == 6, "unsupported methods %u (PyEcho, EchoVector3d, ListSize, TakeFwd, GetFwd, GetFwdRef)", m->unsupported );
	for( const char* name : { "PyEcho", "EchoVector3d", "ListSize", "TakeFwd", "GetFwd", "GetFwdRef" } )
	{
		const BlueInvokeMethod* e = Method( m, name );
		CHECK( e && e->fn == nullptr, "%s must be unsupported", name );
	}
	CHECK( p->count == 5 && p->unsupported == 1, "props count %u unsupported %u", p->count, p->unsupported );
	CHECK( dm->count == 1 && Method( dm, "DerivedOnly" ) && !Method( dm, "EchoInt" ), "derived table has only its own method" );
	CHECK( Table( derived, BLUE_INVOKE_CAP_PROPS ) == nullptr, "no empty props table for the derived class" );

	// classInfo key: the cache path finds the same table
	for( const Reg& r : Registrations() )
	{
		if( r.path == obj && r.capability == BLUE_INVOKE_CAP_METHODS )
		{
			CHECK( r.classInfo && BlueInvokeFindByInfo( r.classInfo, BLUE_INVOKE_CAP_METHODS ) == m, "FindByInfo" );
			CHECK( BlueInvokeFindByInfo( r.classInfo, BLUE_INVOKE_CAP_PROPS ) == p, "FindByInfo props" );
		}
	}
}

void TestRoundTrips( const BlueInvokeClassTable* t, IRoot* self, IRoot* other )
{
	Call c;
	Invoke( c, Method( t, "EchoBool" ), self, { Bool( true ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_BOOL && c.r.v.i == 1, "bool rc %d", c.rc );
	Invoke( c, Method( t, "EchoInt" ), self, { Int( -123456 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_INT && c.r.v.i == -123456, "int" );
	Invoke( c, Method( t, "EchoInt" ), self, { UInt( 77 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 77, "int from uint" );
	Invoke( c, Method( t, "EchoUInt" ), self, { UInt( 4000000000u ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_UINT && c.r.v.u == 4000000000u, "uint32" );
	Invoke( c, Method( t, "EchoInt64" ), self, { Int( -( int64_t( 1 ) << 50 ) ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_INT && c.r.v.i == -( int64_t( 1 ) << 50 ), "int64" );
	Invoke( c, Method( t, "EchoUInt64" ), self, { UInt( 0xfedcba9876543210ull ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_UINT && c.r.v.u == 0xfedcba9876543210ull, "uint64" );
	Invoke( c, Method( t, "EchoSize" ), self, { UInt( 12345 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_UINT && c.r.v.u == 12345, "size_t" );
	Invoke( c, Method( t, "EchoFloat" ), self, { Dbl( 1.5 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_DOUBLE && c.r.v.d == 1.5, "float" );
	Invoke( c, Method( t, "EchoFloat" ), self, { Int( 3 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.d == 3.0, "float from int" );
	Invoke( c, Method( t, "EchoDouble" ), self, { Dbl( 0.1 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_DOUBLE && c.r.v.d == 0.1, "double" );

	const char* utf8 = "h\xc3\xa9llo \xe2\x82\xac \xf0\x9d\x84\x9e"; // héllo € 𝄞
	Invoke( c, Method( t, "EchoCStr" ), self, { Str( "const char" ) } );
	CHECK( c.rc == BIS_OK && AsString( c.r ) == "const char" && c.r.release, "const char*" );
	Invoke( c, Method( t, "EchoString" ), self, { Str( utf8 ) } );
	CHECK( c.rc == BIS_OK && AsString( c.r ) == utf8, "std::string" );
	Invoke( c, Method( t, "EchoWCStr" ), self, { Str( utf8 ) } );
	CHECK( c.rc == BIS_OK && AsString( c.r ) == utf8, "const wchar_t* round trip: %s", AsString( c.r ).c_str() );
	Invoke( c, Method( t, "EchoWString" ), self, { Str( utf8 ) } );
	CHECK( c.rc == BIS_OK && AsString( c.r ) == utf8, "std::wstring round trip" );

	struct
	{
		const char* name;
		uint32_t n;
	} math[] = { { "EchoVector2", 2 }, { "EchoVector3", 3 }, { "EchoVector4", 4 }, { "EchoColor", 4 }, { "EchoQuaternion", 4 }, { "EchoMatrix", 16 } };
	for( const auto& mt : math )
	{
		BlueInvokeValue in = V();
		in.tag = BIV_FLOATS;
		in.count = mt.n;
		for( uint32_t i = 0; i < mt.n; ++i )
		{
			in.v.f[i] = 0.5f * float( i + 1 );
		}
		Invoke( c, Method( t, mt.name ), self, { in } );
		bool same = c.rc == BIS_OK && c.r.tag == BIV_FLOATS && c.r.count == mt.n;
		for( uint32_t i = 0; same && i < mt.n; ++i )
		{
			same = c.r.v.f[i] == in.v.f[i];
		}
		CHECK( same, "%s rc %d", mt.name, c.rc );
	}

	Invoke( c, Method( t, "EchoMode" ), self, { Enum( 5 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_ENUM && c.r.v.i == 5 && c.r.type && std::strstr( c.r.type, "Mode" ), "enum out type %s",
		   c.r.type ? c.r.type : "(null)" );
	Invoke( c, Method( t, "EchoMode" ), self, { Int( 9 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 9, "enum from int" );

	// objects: inputs borrowed, outputs carry one lock
	const int before = other->GetRefCount();
	Invoke( c, Method( t, "EchoObj" ), self, { Obj( other ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_OBJECT && c.r.v.obj == other && other->GetRefCount() == before + 1, "T* out locks" );
	if( c.r.tag == BIV_OBJECT )
	{
		c.r.v.obj->Unlock();
	}
	Invoke( c, Method( t, "EchoObj" ), self, { V() } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_NONE, "null object -> none" );
	Invoke( c, Method( t, "EchoRoot" ), self, { Obj( other ) } );
	CHECK( c.rc == BIS_OK && c.r.v.obj == other && other->GetRefCount() == before + 1, "IRoot*" );
	if( c.r.tag == BIV_OBJECT )
	{
		c.r.v.obj->Unlock();
	}
	const int selfBefore = self->GetRefCount();
	Invoke( c, Method( t, "GetSelfRootPtr" ), self, {} );
	CHECK( c.rc == BIS_OK && c.r.v.obj == self && self->GetRefCount() == selfBefore + 1, "IRootPtr out: one lock net" );
	if( c.r.tag == BIV_OBJECT )
	{
		c.r.v.obj->Unlock();
	}
	Invoke( c, Method( t, "GetSelfPtr" ), self, {} );
	CHECK( c.rc == BIS_OK && c.r.v.obj == self && self->GetRefCount() == selfBefore + 1 && c.r.type && std::strstr( c.r.type, "InvokeTestObj" ),
		   "BluePtr<T> out" );
	if( c.r.tag == BIV_OBJECT )
	{
		c.r.v.obj->Unlock();
	}
}

void TestErrors( const BlueInvokeClassTable* t, const BlueInvokeClassTable* dt, IRoot* self, IRoot* derived )
{
	Call c;
	// argument count
	Invoke( c, Method( t, "EchoInt" ), self, {} );
	CHECK( c.rc == BIS_ARGC && c.e.code == BIS_ARGC && c.e.message[0], "argc low: %s", c.e.message );
	Invoke( c, Method( t, "EchoInt" ), self, { Int( 1 ), Int( 2 ) } );
	CHECK( c.rc == BIS_ARGC, "argc high" );
	// wrong tag, out of range
	Invoke( c, Method( t, "EchoInt" ), self, { Str( "1" ) } );
	CHECK( c.rc == BIS_TYPE && std::strstr( c.e.message, "argument 1" ), "type: %s", c.e.message );
	Invoke( c, Method( t, "EchoInt" ), self, { Int( int64_t( 1 ) << 40 ) } );
	CHECK( c.rc == BIS_TYPE, "int range" );
	Invoke( c, Method( t, "EchoUInt" ), self, { Int( -1 ) } );
	CHECK( c.rc == BIS_TYPE, "uint from negative" );
	Invoke( c, Method( t, "EchoVector3" ), self, { Floats( { 1, 2 } ) } );
	CHECK( c.rc == BIS_TYPE, "float count" );
	Invoke( c, Method( t, "IsDerived" ), self, { Obj( self ) } );
	CHECK( c.rc == BIS_TYPE && std::strstr( c.e.message, "InvokeTestDerived" ), "object type mismatch: %s", c.e.message );
	Invoke( c, Method( t, "IsDerived" ), self, { Obj( derived ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 1, "derived object accepted" );
	// self
	Invoke( c, Method( dt, "DerivedOnly" ), self, { Int( 1 ) } );
	CHECK( c.rc == BIS_SELF, "wrong self" );
	Invoke( c, Method( t, "EchoInt" ), nullptr, { Int( 1 ) } );
	CHECK( c.rc == BIS_SELF, "null self" );
	Invoke( c, Method( t, "EchoInt" ), derived, { Int( 8 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 8, "base method on derived self" );
	Invoke( c, Method( dt, "DerivedOnly" ), derived, { Int( 1 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 1001, "derived method" );
	// exception
	Invoke( c, Method( t, "Throw" ), self, { Int( 7 ) } );
	CHECK( c.rc == BIS_EXCEPTION && std::strstr( c.e.message, "boom 7" ) && c.r.tag == BIV_NONE, "exception: %s", c.e.message );
	// a null result pointer is allowed
	BlueInvokeError e{};
	BlueInvokeValue arg = Str( "discarded" );
	CHECK( Method( t, "EchoString" )->fn( self, &arg, 1, nullptr, &e ) == BIS_OK, "null result" );
}

void TestOptionalAndResults( const BlueInvokeClassTable* t, IRoot* self )
{
	Call c;
	const BlueInvokeMethod* sum = Method( t, "Sum3" );
	CHECK( sum && sum->arity == 3 && sum->numOptional == 2, "Sum3 shape" );
	Invoke( c, sum, self, { Int( 1 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 1, "optional args default to 0" );
	Invoke( c, sum, self, { Int( 1 ), Int( 2 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 3, "one optional given" );
	Invoke( c, sum, self, { Int( 1 ), Int( 2 ), Int( 3 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 6, "all given" );
	Invoke( c, sum, self, {} );
	CHECK( c.rc == BIS_ARGC, "below minimum" );
	Invoke( c, sum, self, { Int( 1 ), Int( 2 ), Int( 3 ), Int( 4 ) } );
	CHECK( c.rc == BIS_ARGC, "above arity" );
	Invoke( c, Method( t, "OptBox" ), self, {} );
	CHECK( c.rc == BIS_OK && c.r.v.i == -1, "Be::Optional unassigned" );
	Invoke( c, Method( t, "OptBox" ), self, { Int( 4 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 4, "Be::Optional assigned" );
	Invoke( c, Method( t, "OptDefault" ), self, {} );
	CHECK( c.rc == BIS_OK && c.r.v.i == 7, "OptionalWithDefaultValue default" );
	Invoke( c, Method( t, "OptDefault" ), self, { Int( 2 ) } );
	CHECK( c.rc == BIS_OK && c.r.v.i == 2, "OptionalWithDefaultValue given" );

	// Be::Result
	const BlueInvokeMethod* fail = Method( t, "Fail" );
	CHECK( fail && ( fail->flags & BIM_BE_RESULT ) && fail->returnTag == BIV_NONE && fail->arity == 1, "Fail shape" );
	Invoke( c, fail, self, { Int( 0 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_NONE, "Be::Result success -> none" );
	Invoke( c, fail, self, { Int( 3 ) } );
	CHECK( c.rc == BIS_FAILED && std::strcmp( c.e.message, "fail 3" ) == 0, "Be::Result failure: %s", c.e.message );

	const BlueInvokeMethod* child = Method( t, "MakeChild" );
	CHECK( child && child->arity == 1 && ( child->flags & BIM_OUT_OBJECT ) && child->returnTag == BIV_OBJECT, "MakeChild shape" );
	Invoke( c, child, self, { Int( 42 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_OBJECT && c.r.v.obj && c.r.v.obj->GetRefCount() == 1, "T** out-param owned, refcount %d",
		   c.r.tag == BIV_OBJECT && c.r.v.obj ? c.r.v.obj->GetRefCount() : -1 );
	if( c.r.tag == BIV_OBJECT && c.r.v.obj )
	{
		IRoot* kid = c.r.v.obj;
		Call g;
		Invoke( g, Method( t, "GetTag" ), kid, {} );
		CHECK( g.rc == BIS_OK && g.r.v.i == 42, "child tag" );
		kid->Unlock();
		c.r = V();
	}
	Invoke( c, child, self, { Int( -1 ) } );
	CHECK( c.rc == BIS_FAILED && std::strstr( c.e.message, "negative" ), "T** failure" );

	const BlueInvokeMethod* get = Method( t, "GetCount" );
	CHECK( get && get->arity == 0 && ( get->flags & BIM_OUT_REF ) && get->returnTag == BIV_INT, "GetCount shape" );
	Invoke( c, Method( t, "SetCount" ), self, { Int( 5 ) } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_NONE, "SetCount" );
	Invoke( c, get, self, {} );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_INT && c.r.v.i == 5, "T& out-param" );

	// non-Be::Result float&: by-value input, writes discarded
	const BlueInvokeMethod* scale = Method( t, "Scale" );
	CHECK( scale && scale->arity == 1 && ( scale->flags & BIM_REF_INPUT ) && !( scale->flags & BIM_OUT_REF ), "Scale shape" );
	BlueInvokeValue in = Dbl( 2.5 );
	Invoke( c, scale, self, { in } );
	CHECK( c.rc == BIS_OK && c.r.tag == BIV_NONE && in.v.d == 2.5, "float& no write-back" );
	Invoke( c, Method( t, "GetLastFloat" ), self, {} );
	CHECK( c.rc == BIS_OK && c.r.v.d == 5.0, "the member saw the value" );
}

void TestProperties( const BlueInvokeClassTable* p, IRoot* self )
{
	BlueInvokeValue out = V();
	BlueInvokeError e{};
	const BlueInvokeProperty* scale = Property( p, "scale" );
	CHECK( scale && scale->get && scale->set && scale->typeTag == BIV_DOUBLE && !( scale->flags & BIP_READONLY ), "scale shape" );
	BlueInvokeValue v = Dbl( 2.25 );
	CHECK( scale->set( self, &v, &e ) == BIS_OK && scale->get( self, &out, &e ) == BIS_OK && out.v.d == 2.25, "scale rw" );
	v = Str( "x" );
	CHECK( scale->set( self, &v, &e ) == BIS_TYPE, "scale wrong type" );

	const BlueInvokeProperty* counter = Property( p, "counter" );
	CHECK( counter && counter->get && !counter->set && ( counter->flags & BIP_READONLY ), "read-only shape" );
	CHECK( counter->get( self, &out, &e ) == BIS_OK && out.v.i == 3, "counter" );

	const BlueInvokeProperty* name = Property( p, "name" );
	CHECK( name && name->get && name->set && ( name->flags & BIP_BE_RESULT_GET ) && ( name->flags & BIP_BE_RESULT_SET ) && name->typeTag == BIV_STRING,
		   "Be::Result property shape" );
	v = Str( "abc" );
	CHECK( name->set( self, &v, &e ) == BIS_OK, "name set" );
	CHECK( name->get( self, &out, &e ) == BIS_OK && AsString( out ) == "abc", "name get" );
	BlueInvokeValueClear( &out );
	v = Str( "" );
	CHECK( name->set( self, &v, &e ) == BIS_FAILED && std::strstr( e.message, "empty" ), "name set failure: %s", e.message );

	const BlueInvokeProperty* mode = Property( p, "mode" );
	CHECK( mode && mode->typeTag == BIV_ENUM && mode->enumType && mode->typeName && std::strstr( mode->typeName, "Mode" ), "enum property" );
	v = Enum( 9 );
	CHECK( mode->set( self, &v, &e ) == BIS_OK && mode->get( self, &out, &e ) == BIS_OK && out.tag == BIV_ENUM && out.v.i == 9, "mode rw" );

	const BlueInvokeProperty* py = Property( p, "pyProp" );
	CHECK( py && !py->get && !py->set, "Python-typed property unsupported" );
	CHECK( scale->get( nullptr, &out, &e ) == BIS_SELF, "property null self" );
}

void TestEnumsAndHashes( const Lib& lib, const char* module, const BlueInvokeClassTable* t )
{
	const std::string enumName = std::string( module ) + "_MODE";
	const Be::VarChooser* c = static_cast<const Be::VarChooser*>( BlueInvokeFind( enumName.c_str(), BLUE_INVOKE_CAP_ENUMS ) );
	CHECK( c != nullptr, "enum %s registered", enumName.c_str() );
	if( c )
	{
		// read as EnumTypeRegistration does: mValue.mLong (BeCast copies the enum's bits)
		CHECK( std::strcmp( c[0].mKey, "MODE_A" ) == 0 && std::strcmp( c[1].mKey, "MODE_B" ) == 0 && std::strcmp( c[2].mKey, "MODE_C" ) == 0 &&
				   c[3].mKey == nullptr && c[0].mValue.mLong == 0 && c[1].mValue.mLong == 5 && c[2].mValue.mLong == 9,
			   "chooser keys and values" );
	}
	const std::type_info* modeType = lib.modeType();
	const std::type_info* otherType = lib.otherType();
	for( const Reg& r : Registrations() )
	{
		if( r.path == enumName && r.capability == BLUE_INVOKE_CAP_ENUMS )
		{
			CHECK( r.classInfo && *static_cast<const std::type_info*>( r.classInfo ) == *modeType, "enum classInfo is typeid(Mode)" );
		}
	}
	const BlueInvokeMethod* takeMode = Method( t, "TakeMode" );
	const BlueInvokeMethod* takeOther = Method( t, "TakeOther" );
	CHECK( takeMode && takeOther && takeMode->argTags[0] == BIV_ENUM && takeMode->argEnumTypes[1] && *takeMode->argEnumTypes[1] == *modeType &&
			   takeOther->argEnumTypes[1] && *takeOther->argEnumTypes[1] == *otherType,
		   "enum parameter type_info" );
	CHECK( takeMode && takeOther && takeMode->sigHash != takeOther->sigHash, "sigHash differs by enum type" );
	CHECK( takeMode && takeMode->argTypeNames[1] && std::strstr( takeMode->argTypeNames[1], "Mode" ), "enum parameter name %s",
		   takeMode && takeMode->argTypeNames[1] ? takeMode->argTypeNames[1] : "(null)" );
	const BlueInvokeMethod* echoInt = Method( t, "EchoInt" );
	const BlueInvokeMethod* thr = Method( t, "Throw" );
	CHECK( echoInt && thr && echoInt->sigHash == thr->sigHash && echoInt->sigHash != 0, "same signature, same hash" );
	CHECK( echoInt && Method( t, "EchoUInt" )->sigHash != echoInt->sigHash, "int vs uint hash" );
}

void TestDuplicates( const std::string& obj )
{
	const void* original = BlueInvokeFind( obj.c_str(), BLUE_INVOKE_CAP_METHODS );
	const uint32_t before = BlueInvokeDuplicateCount();
	static const int dummy = 0;
	CHECK( BlueInvokeRegister( obj.c_str(), &dummy, BLUE_INVOKE_CAP_METHODS, &dummy ) == BLUE_INVOKE_REGISTER_DUPLICATE, "duplicate returns 1" );
	CHECK( BlueInvokeDuplicateCount() == before + 1, "duplicate counted" );
	CHECK( BlueInvokeFind( obj.c_str(), BLUE_INVOKE_CAP_METHODS ) == original, "first registration kept" );
	CHECK( BlueInvokeRegister( nullptr, nullptr, BLUE_INVOKE_CAP_METHODS, &dummy ) == BLUE_INVOKE_REGISTER_INVALID, "invalid registration" );
}

} // namespace

int main()
{
	InitPython();
	std::printf( "libblue_invoke build ID %s, driver compiled against %s\n", BlueInvokeBuildId(), BLUE_INVOKE_BUILD_ID );
	CHECK( Registrations().empty(), "registry starts empty" );

	// hooked
	Lib lib = Load( INVOKE_TEST_LIB_CLASSES );
	CHECK( lib.handle && lib.create && lib.modeType && lib.direct, "load hooked library" );
	if( !lib.handle )
	{
		return 1;
	}
	const std::string obj = "invoke_test_classes.InvokeTestObj";
	const std::string derived = "invoke_test_classes.InvokeTestDerived";
	TestTables( obj, derived );
	const BlueInvokeClassTable* t = Table( obj, BLUE_INVOKE_CAP_METHODS );
	const BlueInvokeClassTable* p = Table( obj, BLUE_INVOKE_CAP_PROPS );
	const BlueInvokeClassTable* dt = Table( derived, BLUE_INVOKE_CAP_METHODS );
	IRoot* self = lib.create( "obj" );
	IRoot* other = lib.create( "obj" );
	IRoot* der = lib.create( "derived" );
	if( t && p && dt && self && other && der )
	{
		TestRoundTrips( t, self, other );
		TestErrors( t, dt, self, der );
		TestOptionalAndResults( t, self );
		TestProperties( p, self );
		TestEnumsAndHashes( lib, "invoke_test_classes", t );
		TestDuplicates( obj );
	}
	char msg[1024];
	CHECK( lib.direct( msg, sizeof( msg ) ) == 0, "direct conversions: %s", msg );
	const size_t hookedCount = Registrations().size();
	std::printf( "hooked library: %zu registrations\n", hookedCount );

	// unhooked: the same test sources with BLUE_INVOKE_DISABLE=1 register nothing. This DSO still
	// links the hooked BlueExposure.a of this build, whose own exposed classes (BlueClasses, ...)
	// register under the DSO's module; in the real unhooked twin the CMake option disables those too.
	Lib unhooked = Load( INVOKE_TEST_LIB_UNHOOKED );
	CHECK( unhooked.handle && unhooked.create, "load unhooked library" );
	size_t fromTestSources = 0;
	size_t fromBlueExposure = 0;
	const std::vector<Reg> afterUnhooked = Registrations();
	for( size_t i = hookedCount; i < afterUnhooked.size(); ++i )
	{
		const Reg& r = afterUnhooked[i];
		if( r.path.rfind( "invoke_test_unhooked.InvokeTest", 0 ) == 0 || r.path == "invoke_test_unhooked_MODE" )
		{
			++fromTestSources;
			std::fprintf( stderr, "unhooked entry %s %s\n", r.path.c_str(), r.capability.c_str() );
		}
		else if( r.path.rfind( "invoke_test_unhooked.", 0 ) == 0 )
		{
			++fromBlueExposure;
		}
	}
	CHECK( fromTestSources == 0, "unhooked test sources registered %zu entries", fromTestSources );
	CHECK( afterUnhooked.size() == hookedCount + fromBlueExposure, "only BlueExposure.a's own classes registered" );
	std::printf( "unhooked library: 0 test registrations, %zu from the hooked BlueExposure.a it links\n", fromBlueExposure );
	if( unhooked.create )
	{
		IRoot* u = unhooked.create( "obj" );
		CHECK( u && BlueInvokeFind( "invoke_test_unhooked.InvokeTestObj", BLUE_INVOKE_CAP_METHODS ) == nullptr, "unhooked class has no table" );
		if( u )
		{
			u->Unlock();
		}
	}

	// another build ID: registered, and distinguishable from libblue_invoke's
	Lib otherId = Load( INVOKE_TEST_LIB_OTHERID );
	CHECK( otherId.handle, "load otherid library" );
	const BlueInvokeClassTable* ot = Table( "invoke_test_otherid.InvokeTestObj", BLUE_INVOKE_CAP_METHODS );
	CHECK( ot && std::strcmp( ot->buildId, "other" ) == 0 && std::strcmp( ot->buildId, BlueInvokeBuildId() ) != 0, "other build ID %s",
		   ot ? ot->buildId : "(none)" );
	CHECK( BlueInvokeFind( "invoke_test_otherid_MODE", BLUE_INVOKE_CAP_ENUMS ) != nullptr, "otherid enum" );

	if( self )
	{
		self->Unlock();
	}
	if( other )
	{
		other->Unlock();
	}
	if( der )
	{
		der->Unlock();
	}
	std::printf( "%d checks, %d failures\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
