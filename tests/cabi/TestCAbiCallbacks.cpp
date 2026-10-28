// Copyright (c) 2026 CCP Games
//
// Host callbacks: BlueCallbackHandle lifetime and BLUE_VK_CALLBACK values in every C-ABI flavour,
// and, with C-ABI as the only backend, BlueScriptCallback holding a host callback - set and read
// through TestCallbacks' C thunks, property and attribute, and called with wrapped arguments.

#include "TestCAbiUtil.h"
#include "TestCallbacks.h"
#include "TestMethods.h"

#include <functional>
#include <set>
#include <stdexcept>

using namespace CAbiTest;

namespace
{

// What the host saw. Arguments are copied: they are only borrowed for the call.
struct Recorder
{
	int calls = 0;
	int destroyed = 0;
	std::vector<uint32_t> kinds;
	std::vector<int64_t> ints;
	std::vector<double> doubles;
	std::vector<std::string> strings;
	std::vector<IRoot*> objects;
	// Whether every call got a non-null out preset to NONE and a non-null, clear err
	bool cleanOutAndErr = true;

	// What the callback returns; by default no value and BLUE_OK
	std::function<int32_t( BlueCValue* out, BlueCError* err )> respond;
};

int32_t RecordCall( void* userData, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	Recorder& recorder = *static_cast<Recorder*>( userData );
	++recorder.calls;
	recorder.kinds.clear();
	for( uint32_t i = 0; i < argc; ++i )
	{
		const BlueCValue& arg = args[i];
		recorder.kinds.push_back( arg.kind );
		switch( arg.kind )
		{
		case BLUE_VK_INT:
			recorder.ints.push_back( arg.u.i );
			break;
		case BLUE_VK_DOUBLE:
			recorder.doubles.push_back( arg.u.d );
			break;
		case BLUE_VK_STRING:
			recorder.strings.push_back( std::string( arg.u.str.ptr, arg.u.str.len ) );
			break;
		case BLUE_VK_OBJECT:
			recorder.objects.push_back( reinterpret_cast<IRoot*>( arg.u.obj ) );
			break;
		default:
			break;
		}
	}

	const BlueCValue none = None();
	if( !out || !err || memcmp( out, &none, sizeof( none ) ) != 0 || err->code != BLUE_OK || err->message || err->message_len )
	{
		recorder.cleanOutAndErr = false;
	}
	return recorder.respond ? recorder.respond( out, err ) : int32_t( BLUE_OK );
}

void RecordDestroy( void* userData )
{
	++static_cast<Recorder*>( userData )->destroyed;
}

// A host callback: the handle holds the host's reference until Release()
struct HostCallback
{
	Recorder recorder;
	BlueCallbackHandle* handle = nullptr;

	HostCallback()
	{
		const BlueCCallback callback = { &RecordCall, &recorder, &RecordDestroy };
		BlueCError err = { BLUE_OK, nullptr, 0 };
		handle = BlueCCallbackCreate( &callback, &err );
		EXPECT_NE( handle, nullptr );
		BlueCErrorClear( &err );
	}
	~HostCallback() { Release(); }
	HostCallback( const HostCallback& ) = delete;
	HostCallback& operator=( const HostCallback& ) = delete;

	void Release()
	{
		BlueCCallbackRelease( handle );
		handle = nullptr;
	}
};

// A host's way of returning a value: blue's string allocation
int32_t ReturnString( BlueCValue* out, const char* s )
{
	return BlueCValueSetString( out, s, strlen( s ), nullptr );
}

// A host's way of returning an object: give blue a reference
int32_t ReturnObject( BlueCValue* out, IRoot* obj )
{
	obj->Lock();
	*out = Obj( obj );
	return BLUE_OK;
}

// A host's way of failing: blue's message allocation
int32_t Fail( BlueCError* err, int32_t code, const char* message )
{
	return BlueCSetErrorMessage( err, code, message, strlen( message ) );
}

} // namespace


//--------------------------------------------------------------------
// Handles and values (every C-ABI flavour)
//--------------------------------------------------------------------

TEST( CAbiCallbackHandle, DestroyedOnceByTheLastRelease )
{
	Recorder recorder;
	const BlueCCallback callback = { &RecordCall, &recorder, &RecordDestroy };
	BlueCallbackHandle* handle = BlueCCallbackCreate( &callback, nullptr );
	ASSERT_NE( handle, nullptr );

	const BlueCCallback* stored = BlueCCallbackGet( handle );
	ASSERT_NE( stored, nullptr );
	EXPECT_EQ( stored->fn, &RecordCall );
	EXPECT_EQ( stored->user_data, &recorder );
	EXPECT_EQ( stored->destroy, &RecordDestroy );

	EXPECT_EQ( BlueCCallbackInvoke( handle, nullptr, 0, nullptr, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( recorder.calls, 1 );
	EXPECT_TRUE( recorder.cleanOutAndErr );

	BlueCCallbackAddRef( handle );
	BlueCCallbackRelease( handle );
	EXPECT_EQ( recorder.destroyed, 0 );
	BlueCCallbackRelease( handle );
	EXPECT_EQ( recorder.destroyed, 1 );

	// Null handles are ignored
	BlueCCallbackAddRef( nullptr );
	BlueCCallbackRelease( nullptr );
	EXPECT_EQ( BlueCCallbackGet( nullptr ), nullptr );

	// Invoking a null handle is an error
	Result invalid;
	invalid.out = Int( 5 );
	invalid.status = BlueCCallbackInvoke( nullptr, nullptr, 0, &invalid.out, &invalid.err );
	EXPECT_CABI_ERROR( invalid, BLUE_E_INVALID_ARGUMENT );
	EXPECT_EQ( BlueCCallbackInvoke( nullptr, nullptr, 0, nullptr, nullptr ), int32_t( BLUE_E_INVALID_ARGUMENT ) );
}

TEST( CAbiCallbackHandle, NoDestroyFunction )
{
	Recorder recorder;
	const BlueCCallback callback = { &RecordCall, &recorder, nullptr };
	BlueCallbackHandle* handle = BlueCCallbackCreate( &callback, nullptr );
	ASSERT_NE( handle, nullptr );
	BlueCCallbackRelease( handle );
	EXPECT_EQ( recorder.destroyed, 0 );
}

TEST( CAbiCallbackHandle, CreateWithoutFunctionFails )
{
	Recorder recorder;
	const BlueCCallback callback = { nullptr, &recorder, &RecordDestroy };

	Result r;
	EXPECT_EQ( BlueCCallbackCreate( &callback, &r.err ), nullptr );
	EXPECT_EQ( r.err.code, int32_t( BLUE_E_INVALID_ARGUMENT ) );
	EXPECT_EQ( BlueCCallbackCreate( nullptr, nullptr ), nullptr );

	// The host still owns user_data
	EXPECT_EQ( recorder.destroyed, 0 );
}

TEST( CAbiCallbackHandle, ValueHoldsAReference )
{
	HostCallback host;

	BlueCValue value;
	BlueCWrapCallback( host.handle, value );
	EXPECT_EQ( value.kind, uint32_t( BLUE_VK_CALLBACK ) );
	EXPECT_EQ( value.aux, 0u );
	EXPECT_EQ( value.u.callback, host.handle );

	host.Release();
	EXPECT_EQ( host.recorder.destroyed, 0 );
	BlueCValueRelease( &value );
	EXPECT_EQ( host.recorder.destroyed, 1 );
	EXPECT_EQ( value.kind, uint32_t( BLUE_VK_NONE ) );

	BlueCWrapCallback( nullptr, value );
	EXPECT_EQ( value.kind, uint32_t( BLUE_VK_NONE ) );
}

TEST( CAbiCallbackHandle, Extract )
{
	HostCallback host;

	Result ok, none, wrongKind, nullPointer;
	BlueCallbackHandle* handle = nullptr;
	EXPECT_TRUE( BlueCExtractCallback( Callback( host.handle ), handle, 0, &ok.err ) );
	EXPECT_EQ( handle, host.handle );
	EXPECT_TRUE( BlueCExtractCallback( None(), handle, 0, &none.err ) );
	EXPECT_EQ( handle, nullptr );

	EXPECT_FALSE( BlueCExtractCallback( Int( 1 ), handle, 2, &wrongKind.err ) );
	EXPECT_EQ( wrongKind.err.code, int32_t( BLUE_E_TYPE ) );
	EXPECT_EQ( wrongKind.Message(), "argument 2: expected callback, got int" );

	BlueCValue bad = None();
	bad.kind = BLUE_VK_CALLBACK;
	EXPECT_FALSE( BlueCExtractCallback( bad, handle, 0, &nullPointer.err ) );
	EXPECT_EQ( nullPointer.err.code, int32_t( BLUE_E_INVALID_ARGUMENT ) );

	// Extracting borrows: no reference taken
	host.Release();
	EXPECT_EQ( host.recorder.destroyed, 1 );
}


//--------------------------------------------------------------------
// Invoking a handle: returned values and errors (every C-ABI flavour)
//--------------------------------------------------------------------

TEST( CAbiCallbackInvoke, ArgumentsAreBorrowed )
{
	HostCallback host;
	const std::string text = "not NUL-terminated here";
	BlueCValue args[] = { Int( 3 ), Str( text.data(), 3 ) };

	Result r;
	r.status = BlueCCallbackInvoke( host.handle, args, 2, &r.out, &r.err );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.Kind(), uint32_t( BLUE_VK_NONE ) );
	EXPECT_EQ( host.recorder.kinds, ( std::vector<uint32_t>{ BLUE_VK_INT, BLUE_VK_STRING } ) );
	EXPECT_EQ( host.recorder.strings, std::vector<std::string>{ "not" } );
	EXPECT_TRUE( host.recorder.cleanOutAndErr );
	// Untouched: still the caller's
	EXPECT_EQ( args[1].u.str.ptr, text.data() );
}

TEST( CAbiCallbackInvoke, ReturnsTheValueToTheCaller )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue* out, BlueCError* ) { return ReturnString( out, "returned" ); };

	Result r;
	r.out = Int( 5 );	// overwritten, not released: the caller owns nothing in it
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.S(), "returned" );
	EXPECT_EQ( r.err.message, nullptr );
	EXPECT_TRUE( host.recorder.cleanOutAndErr );
}

TEST( CAbiCallbackInvoke, ValueIsReleasedWithoutOut )
{
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	IRoot* obj = reinterpret_cast<IRoot*>( (TestMethods*)object );
	const int refCount = obj->GetRefCount();

	HostCallback host;
	host.recorder.respond = [obj]( BlueCValue* out, BlueCError* ) { return ReturnObject( out, obj ); };
	EXPECT_EQ( BlueCCallbackInvoke( host.handle, nullptr, 0, nullptr, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// With an out the caller gets the host's reference
	Result r;
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.O(), obj );
	EXPECT_EQ( obj->GetRefCount(), refCount + 1 );
	BlueCValueRelease( &r.out );
	EXPECT_EQ( obj->GetRefCount(), refCount );
}

TEST( CAbiCallbackInvoke, ErrorCarriesTheCallbacksMessage )
{
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	IRoot* obj = reinterpret_cast<IRoot*>( (TestMethods*)object );
	const int refCount = obj->GetRefCount();

	// A value written before failing is still released, once
	HostCallback host;
	host.recorder.respond = [obj]( BlueCValue* out, BlueCError* err ) {
		ReturnObject( out, obj );
		return Fail( err, BLUE_E_NOT_FOUND, "no such thing" );
	};

	Result r;
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_ERROR( r, BLUE_E_NOT_FOUND );
	EXPECT_EQ( r.Message(), "no such thing" );
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Without an err only the status comes back
	EXPECT_EQ( BlueCCallbackInvoke( host.handle, nullptr, 0, nullptr, nullptr ), int32_t( BLUE_E_NOT_FOUND ) );
	EXPECT_EQ( obj->GetRefCount(), refCount );
}

TEST( CAbiCallbackInvoke, StatusIsTheCode )
{
	// The returned status wins over a different code in err
	HostCallback host;
	host.recorder.respond = []( BlueCValue*, BlueCError* err ) {
		Fail( err, BLUE_E_TYPE, "mismatch" );
		return int32_t( BLUE_E_OVERFLOW );
	};
	Result r;
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_ERROR( r, BLUE_E_OVERFLOW );
	EXPECT_EQ( r.Message(), "mismatch" );
}

TEST( CAbiCallbackInvoke, ErrorWithoutMessage )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue*, BlueCError* ) { return int32_t( BLUE_E_FAILED ); };
	Result failed;
	failed.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &failed.out, &failed.err );
	EXPECT_CABI_ERROR( failed, BLUE_E_FAILED );
	EXPECT_EQ( failed.Message(), "callback failed with status -1" );

	// A positive status is a failure too
	host.recorder.respond = []( BlueCValue*, BlueCError* ) { return int32_t( 3 ); };
	Result positive;
	positive.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &positive.out, &positive.err );
	EXPECT_CABI_ERROR( positive, BLUE_E_FAILED );
	EXPECT_EQ( positive.Message(), "callback failed with status -1" );
}

TEST( CAbiCallbackInvoke, ErrIsIgnoredOnSuccess )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue* out, BlueCError* err ) {
		Fail( err, BLUE_E_FAILED, "ignored" );
		BlueCValueSetNone( *out );
		out->kind = BLUE_VK_INT;
		out->u.i = 9;
		return int32_t( BLUE_OK );
	};
	Result r;
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.err.code, int32_t( BLUE_OK ) );
	EXPECT_EQ( r.err.message, nullptr );
	EXPECT_EQ( r.I(), 9 );
}

TEST( CAbiCallbackInvoke, ExceptionFromACppHost )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue*, BlueCError* ) -> int32_t { throw std::runtime_error( "host bug" ); };
	Result r;
	r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
	EXPECT_CABI_ERROR( r, BLUE_E_EXCEPTION );
}

TEST( CAbiCallbackInvoke, NestedValuesAreReleasedOnce )
{
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	IRoot* obj = reinterpret_cast<IRoot*>( (TestMethods*)object );
	const int refCount = obj->GetRefCount();
	HostCallback inner;

	// { "k": [ object, callback, "s" ] }, every allocation blue's, every reference given to blue
	HostCallback host;
	BlueCallbackHandle* innerHandle = inner.handle;
	host.recorder.respond = [obj, innerHandle]( BlueCValue* out, BlueCError* err ) {
		int32_t status = BlueCValueSetMap( out, 1, err );
		if( status != BLUE_OK )
		{
			return status;
		}
		BlueCValue* kv = out->u.map.kv;
		ReturnString( &kv[0], "k" );
		status = BlueCValueSetArray( &kv[1], 3, err );
		if( status != BLUE_OK )
		{
			return status;
		}
		BlueCValue* items = kv[1].u.arr.items;
		ReturnObject( &items[0], obj );
		BlueCCallbackAddRef( innerHandle );
		items[1] = Callback( innerHandle );
		ReturnString( &items[2], "s" );
		return int32_t( BLUE_OK );
	};

	// Discarded
	EXPECT_EQ( BlueCCallbackInvoke( host.handle, nullptr, 0, nullptr, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Returned, then released by the caller
	{
		Result r;
		r.status = BlueCCallbackInvoke( host.handle, nullptr, 0, &r.out, &r.err );
		ASSERT_CABI_OK( r );
		ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_MAP ) );
		ASSERT_EQ( r.out.u.map.count, size_t( 1 ) );
		EXPECT_EQ( AsString( r.out.u.map.kv[0] ), "k" );
		const BlueCValue& list = r.out.u.map.kv[1];
		ASSERT_EQ( list.kind, uint32_t( BLUE_VK_ARRAY ) );
		ASSERT_EQ( list.u.arr.count, size_t( 3 ) );
		EXPECT_EQ( list.u.arr.items[0].u.obj, Obj( obj ).u.obj );
		EXPECT_EQ( list.u.arr.items[1].u.callback, innerHandle );
		EXPECT_EQ( AsString( list.u.arr.items[2] ), "s" );
		EXPECT_EQ( obj->GetRefCount(), refCount + 1 );
	}
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Only the host's own reference is left: one release destroys the callback, once
	inner.Release();
	EXPECT_EQ( inner.recorder.destroyed, 1 );
}

TEST( CAbiValueBuilders, StringArrayMap )
{
	BlueCValue v;
	BlueCValueSetNone( v );

	const char text[] = { 'a', 'b', 'c' };
	EXPECT_EQ( BlueCValueSetString( &v, text, 2, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( AsString( v ), "ab" );
	BlueCValueRelease( &v );

	EXPECT_EQ( BlueCValueSetString( &v, nullptr, 0, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( AsString( v ), "" );
	BlueCValueRelease( &v );

	EXPECT_EQ( BlueCValueSetArray( &v, 2, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_ARRAY ) );
	ASSERT_EQ( v.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( v.u.arr.items[0].kind, uint32_t( BLUE_VK_NONE ) );
	EXPECT_EQ( v.u.arr.items[1].kind, uint32_t( BLUE_VK_NONE ) );
	BlueCValueRelease( &v );

	EXPECT_EQ( BlueCValueSetArray( &v, 0, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_ARRAY ) );
	EXPECT_EQ( v.u.arr.count, size_t( 0 ) );
	BlueCValueRelease( &v );

	EXPECT_EQ( BlueCValueSetMap( &v, 2, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( v.kind, uint32_t( BLUE_VK_MAP ) );
	ASSERT_EQ( v.u.map.count, size_t( 2 ) );
	for( size_t i = 0; i < 4; ++i )
	{
		EXPECT_EQ( v.u.map.kv[i].kind, uint32_t( BLUE_VK_NONE ) );
	}
	BlueCValueRelease( &v );
}

TEST( CAbiValueBuilders, Errors )
{
	Result nullOut, nullString, tooMany;
	nullOut.status = BlueCValueSetString( nullptr, "x", 1, &nullOut.err );
	EXPECT_EQ( nullOut.status, int32_t( BLUE_E_INVALID_ARGUMENT ) );
	EXPECT_EQ( BlueCValueSetArray( nullptr, 1, nullptr ), int32_t( BLUE_E_INVALID_ARGUMENT ) );
	EXPECT_EQ( BlueCValueSetMap( nullptr, 1, nullptr ), int32_t( BLUE_E_INVALID_ARGUMENT ) );

	nullString.status = BlueCValueSetString( &nullString.out, nullptr, 4, &nullString.err );
	EXPECT_CABI_ERROR( nullString, BLUE_E_INVALID_ARGUMENT );

	tooMany.status = BlueCValueSetMap( &tooMany.out, std::numeric_limits<size_t>::max(), &tooMany.err );
	EXPECT_CABI_ERROR( tooMany, BLUE_E_OUT_OF_MEMORY );
}

TEST( CAbiValueBuilders, ErrorMessage )
{
	Result r;
	const char text[] = { 'b', 'a', 'd', '!' };
	EXPECT_EQ( BlueCSetErrorMessage( &r.err, BLUE_E_FAILED, text, 3 ), int32_t( BLUE_E_FAILED ) );
	EXPECT_EQ( r.err.code, int32_t( BLUE_E_FAILED ) );
	EXPECT_EQ( r.Message(), "bad" );
	EXPECT_EQ( r.err.message[3], 0 );

	// Replaces the previous message; a null message sets the code alone
	EXPECT_EQ( BlueCSetErrorMessage( &r.err, BLUE_E_TYPE, nullptr, 5 ), int32_t( BLUE_E_TYPE ) );
	EXPECT_EQ( r.err.code, int32_t( BLUE_E_TYPE ) );
	EXPECT_EQ( r.err.message, nullptr );
	EXPECT_EQ( r.err.message_len, size_t( 0 ) );

	EXPECT_EQ( BlueCSetErrorMessage( nullptr, BLUE_E_TYPE, "x", 1 ), int32_t( BLUE_E_TYPE ) );
}


#if BLUE_CABI_CALLBACKS

//--------------------------------------------------------------------
// BlueScriptCallback through TestCallbacks' thunks
//--------------------------------------------------------------------

TEST( CAbiCallbacks, EmptyByDefault )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );

	Result valid, call, get, property, attribute;
	Call( valid, (TestCallbacks*)callbacks, "HasValidCallback" );
	Call( call, (TestCallbacks*)callbacks, "CallCallbackVoid" );
	Call( get, (TestCallbacks*)callbacks, "GetCallback" );
	Get( property, (TestCallbacks*)callbacks, "callback" );
	Get( attribute, (TestCallbacks*)callbacks, "callbackAttribute" );

	EXPECT_CABI_OK( valid );
	EXPECT_EQ( valid.I(), 0 );
	// Calling an empty callback fails, as in Python
	EXPECT_CABI_OK( call );
	EXPECT_EQ( call.Kind(), uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( call.I(), 0 );
	for( Result* r : { &get, &property, &attribute } )
	{
		EXPECT_CABI_OK( *r );
		EXPECT_EQ( r->Kind(), uint32_t( BLUE_VK_NONE ) );
	}
}

TEST( CAbiCallbacks, SetAndCallVoid )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );
	HostCallback host;

	Result set;
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
	ASSERT_CABI_OK( set );

	// Blue keeps its own reference
	host.Release();
	EXPECT_EQ( host.recorder.destroyed, 0 );

	Result valid, call;
	Call( valid, (TestCallbacks*)callbacks, "HasValidCallback" );
	Call( call, (TestCallbacks*)callbacks, "CallCallbackVoid" );
	EXPECT_EQ( valid.I(), 1 );
	EXPECT_CABI_OK( call );
	EXPECT_EQ( call.I(), 1 );
	EXPECT_EQ( host.recorder.calls, 1 );
	EXPECT_TRUE( host.recorder.kinds.empty() );

	// Clearing it drops blue's reference: destroy runs once
	Result clear;
	Call( clear, (TestCallbacks*)callbacks, "SetCallback", { None() } );
	EXPECT_CABI_OK( clear );
	EXPECT_EQ( host.recorder.destroyed, 1 );

	Result after;
	Call( after, (TestCallbacks*)callbacks, "CallCallbackVoid" );
	EXPECT_EQ( after.I(), 0 );
	EXPECT_EQ( host.recorder.calls, 1 );
}

TEST( CAbiCallbacks, GetReturnsTheSameHandle )
{
	HostCallback host;
	{
		Result get;
		{
			TestCallbacksPtr callbacks;
			ASSERT_TRUE( callbacks.CreateInstance() );

			Result set;
			Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
			ASSERT_CABI_OK( set );
			host.Release();

			Call( get, (TestCallbacks*)callbacks, "GetCallback" );
			ASSERT_CABI_OK( get );
			ASSERT_EQ( get.Kind(), uint32_t( BLUE_VK_CALLBACK ) );

			// The host recognises its own callback
			const BlueCCallback* callback = BlueCCallbackGet( get.out.u.callback );
			ASSERT_NE( callback, nullptr );
			EXPECT_EQ( callback->user_data, &host.recorder );
		}
		// The returned value holds a reference of its own
		EXPECT_EQ( host.recorder.destroyed, 0 );
	}
	EXPECT_EQ( host.recorder.destroyed, 1 );
}

TEST( CAbiCallbacks, DestroyedWithTheObject )
{
	HostCallback host;
	{
		TestCallbacksPtr callbacks;
		ASSERT_TRUE( callbacks.CreateInstance() );
		Result set;
		Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
		ASSERT_CABI_OK( set );
	}
	EXPECT_EQ( host.recorder.destroyed, 0 );
	host.Release();
	EXPECT_EQ( host.recorder.destroyed, 1 );
	EXPECT_EQ( LiveCount<TestCallbacks>(), 0u );
}

TEST( CAbiCallbacks, ReturnValuesThroughThunks )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );
	HostCallback host;
	host.recorder.respond = [&host]( BlueCValue* out, BlueCError* ) {
		// CallCallbackInt has no arguments; CallCallbackStringInt passes one int
		if( host.recorder.kinds.empty() )
		{
			*out = Int( 42 );
			return int32_t( BLUE_OK );
		}
		return ReturnString( out, "five" );
	};

	Result set;
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
	ASSERT_CABI_OK( set );

	Result pairInt, pairString;
	Call( pairInt, (TestCallbacks*)callbacks, "CallCallbackInt" );
	Call( pairString, (TestCallbacks*)callbacks, "CallCallbackStringInt", { Int( 5 ) } );
	ASSERT_CABI_OK( pairInt );
	ASSERT_EQ( pairInt.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( pairInt.out.u.arr.items[0].u.i, 1 );
	EXPECT_EQ( pairInt.out.u.arr.items[1].u.i, 42 );
	ASSERT_CABI_OK( pairString );
	ASSERT_EQ( pairString.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( pairString.out.u.arr.items[0].u.i, 1 );
	EXPECT_EQ( AsString( pairString.out.u.arr.items[1] ), "five" );
	EXPECT_EQ( host.recorder.ints, std::vector<int64_t>{ 5 } );
	EXPECT_EQ( host.recorder.calls, 2 );
}

TEST( CAbiCallbacks, FailuresThroughThunks )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );
	HostCallback host;
	Result set;
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
	ASSERT_CABI_OK( set );

	// The callback fails: the exposed method sees a false status and keeps its preset result
	host.recorder.respond = []( BlueCValue*, BlueCError* err ) { return Fail( err, BLUE_E_FAILED, "host says no" ); };
	Result failedVoid, failedInt;
	Call( failedVoid, (TestCallbacks*)callbacks, "CallCallbackVoid" );
	Call( failedInt, (TestCallbacks*)callbacks, "CallCallbackInt" );
	EXPECT_CABI_OK( failedVoid );
	EXPECT_EQ( failedVoid.I(), 0 );
	ASSERT_CABI_OK( failedInt );
	EXPECT_EQ( failedInt.out.u.arr.items[0].u.i, 0 );
	EXPECT_EQ( failedInt.out.u.arr.items[1].u.i, int64_t( int( 0xDeadBeef ) ) );

	// The wrong kind of value fails the same way
	host.recorder.respond = []( BlueCValue* out, BlueCError* ) { return ReturnString( out, "not an int" ); };
	Result wrongKind;
	Call( wrongKind, (TestCallbacks*)callbacks, "CallCallbackIntStringFloat", { Str( "a" ), Double( 1.5 ) } );
	ASSERT_CABI_OK( wrongKind );
	EXPECT_EQ( wrongKind.out.u.arr.items[0].u.i, 0 );
	EXPECT_EQ( wrongKind.out.u.arr.items[1].u.i, int64_t( int( 0xDeadBeef ) ) );
	EXPECT_EQ( host.recorder.calls, 3 );
}

TEST( CAbiCallbacks, WrongKindIsATypeError )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );

	Result set, property, attribute;
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { Int( 1 ) } );
	Set( property, (TestCallbacks*)callbacks, "callback", Str( "f" ) );
	Set( attribute, (TestCallbacks*)callbacks, "callbackAttribute", Double( 1 ) );

	EXPECT_CABI_ERROR( set, BLUE_E_TYPE );
	EXPECT_EQ( set.Message(), "argument 0: expected callback, got int" );
	EXPECT_CABI_ERROR( property, BLUE_E_TYPE );
	EXPECT_CABI_ERROR( attribute, BLUE_E_TYPE );
	EXPECT_EQ( attribute.Message(), "value: expected callback, got double" );
}

TEST( CAbiCallbacks, PropertyAndAttribute )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );
	HostCallback viaProperty;
	HostCallback viaAttribute;

	// The property and the attribute are the same member
	SetOk( (TestCallbacks*)callbacks, "callback", Callback( viaProperty.handle ) );
	{
		Result property, attribute;
		Get( property, (TestCallbacks*)callbacks, "callback" );
		Get( attribute, (TestCallbacks*)callbacks, "callbackAttribute" );
		EXPECT_CABI_OK( property );
		EXPECT_CABI_OK( attribute );
		EXPECT_EQ( property.Kind(), uint32_t( BLUE_VK_CALLBACK ) );
		EXPECT_EQ( property.out.u.callback, viaProperty.handle );
		EXPECT_EQ( attribute.out.u.callback, viaProperty.handle );
	}

	SetOk( (TestCallbacks*)callbacks, "callbackAttribute", Callback( viaAttribute.handle ) );
	viaProperty.Release();
	EXPECT_EQ( viaProperty.recorder.destroyed, 1 );

	Result call;
	Call( call, (TestCallbacks*)callbacks, "CallCallbackVoid" );
	EXPECT_EQ( call.I(), 1 );
	EXPECT_EQ( viaAttribute.recorder.calls, 1 );

	SetOk( (TestCallbacks*)callbacks, "callbackAttribute", None() );
	viaAttribute.Release();
	EXPECT_EQ( viaAttribute.recorder.destroyed, 1 );
}


//--------------------------------------------------------------------
// BlueScriptCallback called from C++
//--------------------------------------------------------------------

TEST( CAbiCallbacks, ArgumentsAreWrapped )
{
	HostCallback host;
	BlueScriptCallback callback( host.handle );
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	const int refCount = RefCount( (TestMethods*)object );

	auto status = callback.CallVoid( 7, std::string( "two" ), 2.5f, (TestMethods*)object );
	EXPECT_TRUE( bool( status ) );
	EXPECT_EQ( host.recorder.calls, 1 );
	EXPECT_EQ( host.recorder.kinds, ( std::vector<uint32_t>{ BLUE_VK_INT, BLUE_VK_STRING, BLUE_VK_DOUBLE, BLUE_VK_OBJECT } ) );
	EXPECT_EQ( host.recorder.ints, std::vector<int64_t>{ 7 } );
	EXPECT_EQ( host.recorder.strings, std::vector<std::string>{ "two" } );
	EXPECT_EQ( host.recorder.doubles, std::vector<double>{ 2.5 } );
	ASSERT_EQ( host.recorder.objects.size(), size_t( 1 ) );
	EXPECT_EQ( host.recorder.objects[0], reinterpret_cast<IRoot*>( (TestMethods*)object ) );

	// The arguments were released after the call
	EXPECT_EQ( RefCount( (TestMethods*)object ), refCount );

	// C strings, as blue passes them
	status = callback.CallVoid( "Primary", "res:/a" );
	EXPECT_TRUE( bool( status ) );
	EXPECT_EQ( host.recorder.strings, ( std::vector<std::string>{ "two", "Primary", "res:/a" } ) );
}

TEST( CAbiCallbacks, UnconvertibleArgumentFailsTheCall )
{
	HostCallback host;
	BlueScriptCallback callback( host.handle );

	auto status = callback.CallVoid( 1, std::set<int>{ 2 } );
	EXPECT_FALSE( bool( status ) );
	status.MuteException();
	EXPECT_EQ( host.recorder.calls, 0 );
}

TEST( CAbiCallbacks, CopiesShareTheHandle )
{
	HostCallback host;
	{
		BlueScriptCallback a( host.handle );
		host.Release();
		BlueScriptCallback b( a );
		BlueScriptCallback c;
		c = b;
		BlueScriptCallback& self = c;
		c = self;
		EXPECT_TRUE( a.IsValid() && b.IsValid() && c.IsValid() );
		EXPECT_EQ( c.GetCHandle(), a.GetCHandle() );

		a.Destroy();
		EXPECT_FALSE( a.IsValid() );
		EXPECT_FALSE( bool( a.CallVoid() ) );
		b = BlueScriptCallback();
		EXPECT_EQ( host.recorder.destroyed, 0 );

		EXPECT_TRUE( bool( c.CallVoid() ) );
		EXPECT_EQ( host.recorder.calls, 1 );
	}
	EXPECT_EQ( host.recorder.destroyed, 1 );
}

TEST( CAbiCallbacks, CallReturnsAnInt )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue* out, BlueCError* ) {
		*out = Int( 42 );
		return int32_t( BLUE_OK );
	};
	BlueScriptCallback callback( host.handle );

	int result = 0;
	auto status = callback.Call( result, std::string( "arg" ) );
	EXPECT_TRUE( bool( status ) );
	EXPECT_TRUE( status.GetErrorMessage().empty() );
	EXPECT_EQ( result, 42 );
	EXPECT_EQ( host.recorder.strings, std::vector<std::string>{ "arg" } );

	// Out of range for the C++ type
	host.recorder.respond = []( BlueCValue* out, BlueCError* ) {
		*out = Int( int64_t( 1 ) << 40 );
		return int32_t( BLUE_OK );
	};
	result = 7;
	status = callback.Call( result );
	EXPECT_FALSE( bool( status ) );
	status.MuteException();
	EXPECT_EQ( result, 7 );
}

TEST( CAbiCallbacks, CallReturnsAnObject )
{
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	IRoot* obj = reinterpret_cast<IRoot*>( (TestMethods*)object );
	const int refCount = obj->GetRefCount();

	HostCallback host;
	host.recorder.respond = [obj]( BlueCValue* out, BlueCError* ) { return ReturnObject( out, obj ); };
	BlueScriptCallback callback( host.handle );

	// The returned pointer carries a reference of its own, as with Python; the host's is released
	TestMethods* result = nullptr;
	auto status = callback.Call( result );
	EXPECT_TRUE( bool( status ) );
	EXPECT_EQ( result, (TestMethods*)object );
	EXPECT_EQ( obj->GetRefCount(), refCount + 1 );
	result->Unlock();
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// An object of the wrong class: type error, the host's reference released, result untouched
	TestCallbacks* wrongClass = nullptr;
	status = callback.Call( wrongClass );
	EXPECT_FALSE( bool( status ) );
	status.MuteException();
	EXPECT_EQ( wrongClass, nullptr );
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// None is a null pointer
	host.recorder.respond = []( BlueCValue*, BlueCError* ) { return int32_t( BLUE_OK ); };
	result = (TestMethods*)object;
	status = callback.Call( result );
	EXPECT_TRUE( bool( status ) );
	EXPECT_EQ( result, nullptr );
	EXPECT_EQ( obj->GetRefCount(), refCount );
}

TEST( CAbiCallbacks, CallbackErrorFailsTheCall )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue*, BlueCError* err ) { return Fail( err, BLUE_E_FAILED, "host says no" ); };
	BlueScriptCallback callback( host.handle );

	auto status = callback.CallVoid( 1 );
	EXPECT_FALSE( bool( status ) );
	EXPECT_EQ( status.GetErrorMessage(), "host says no" );
	// Logged once by ReportException (here, and not again by the destructor)
	status.ReportException();

	int result = 3;
	auto withResult = callback.Call( result );
	EXPECT_FALSE( bool( withResult ) );
	EXPECT_EQ( withResult.GetErrorMessage(), "host says no" );
	withResult.MuteException();
	EXPECT_EQ( result, 3 );

	// Without a message the status is named
	host.recorder.respond = []( BlueCValue*, BlueCError* ) { return int32_t( BLUE_E_NOT_FOUND ); };
	auto bare = callback.CallVoid();
	EXPECT_FALSE( bool( bare ) );
	EXPECT_EQ( bare.GetErrorMessage(), "callback failed with status -9" );
	bare.MuteException();
	EXPECT_EQ( host.recorder.calls, 3 );
}

TEST( CAbiCallbacks, CallWrongKindIsACallError )
{
	HostCallback host;
	host.recorder.respond = []( BlueCValue* out, BlueCError* ) { return ReturnString( out, "text" ); };
	BlueScriptCallback callback( host.handle );

	int result = 3;
	auto status = callback.Call( result );
	EXPECT_FALSE( bool( status ) );
	EXPECT_EQ( status.GetErrorMessage(), "result: expected integer, got string" );
	status.MuteException();
	EXPECT_EQ( result, 3 );

	// CallVoid ignores (and releases) whatever comes back
	EXPECT_TRUE( bool( callback.CallVoid() ) );
}

TEST( CAbiCallbacks, ReturnedValuesAreReleasedOnce )
{
	TestMethodsPtr object;
	ASSERT_TRUE( object.CreateInstance() );
	IRoot* obj = reinterpret_cast<IRoot*>( (TestMethods*)object );
	const int refCount = obj->GetRefCount();
	HostCallback inner;
	BlueCallbackHandle* innerHandle = inner.handle;

	// [ object, callback, object ], each carrying a reference given to blue
	HostCallback host;
	host.recorder.respond = [obj, innerHandle]( BlueCValue* out, BlueCError* err ) {
		const int32_t status = BlueCValueSetArray( out, 3, err );
		if( status != BLUE_OK )
		{
			return status;
		}
		ReturnObject( &out->u.arr.items[0], obj );
		BlueCCallbackAddRef( innerHandle );
		out->u.arr.items[1] = Callback( innerHandle );
		ReturnObject( &out->u.arr.items[2], obj );
		return int32_t( BLUE_OK );
	};
	BlueScriptCallback callback( host.handle );

	// Rejected part-way through: item 1 is a callback, not an object
	std::vector<IRoot*> objects;
	auto partial = callback.Call( objects );
	EXPECT_FALSE( bool( partial ) );
	EXPECT_EQ( partial.GetErrorMessage(), "result: expected object, got callback" );
	partial.MuteException();
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Converted

	host.recorder.respond = [obj, innerHandle]( BlueCValue* out, BlueCError* err ) {
		const int32_t status = BlueCValueSetArray( out, 2, err );
		if( status != BLUE_OK )
		{
			return status;
		}
		ReturnObject( &out->u.arr.items[0], obj );
		BlueCCallbackAddRef( innerHandle );
		out->u.arr.items[1] = Callback( innerHandle );
		return int32_t( BLUE_OK );
	};
	std::pair<TestMethods*, BlueScriptCallback> pair;
	auto ok = callback.Call( pair );
	EXPECT_TRUE( bool( ok ) ) << ok.GetErrorMessage();
	EXPECT_EQ( pair.first, (TestMethods*)object );
	EXPECT_EQ( pair.second.GetCHandle(), innerHandle );
	// The pair borrows the object (only top-level pointers get a reference) and holds its own
	// reference to the callback; the returned value's references are gone
	EXPECT_EQ( obj->GetRefCount(), refCount );
	pair = {};

	// Rejected (wrong kind)
	int wrong = 0;
	auto rejected = callback.Call( wrong );
	EXPECT_FALSE( bool( rejected ) );
	rejected.MuteException();
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Failed after building the value
	host.recorder.respond = [obj]( BlueCValue* out, BlueCError* err ) {
		ReturnObject( out, obj );
		return Fail( err, BLUE_E_FAILED, "late failure" );
	};
	TestMethods* none = nullptr;
	auto failed = callback.Call( none );
	EXPECT_FALSE( bool( failed ) );
	failed.MuteException();
	EXPECT_EQ( none, nullptr );
	EXPECT_EQ( obj->GetRefCount(), refCount );

	// Every returned callback reference was dropped: the host's last one destroys it, once
	EXPECT_EQ( inner.recorder.destroyed, 0 );
	inner.Release();
	EXPECT_EQ( inner.recorder.destroyed, 1 );
}

#endif // BLUE_CABI_CALLBACKS
