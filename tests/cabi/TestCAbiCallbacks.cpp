// Copyright (c) 2026 CCP Games
//
// Host callbacks: BlueCallbackHandle lifetime and BLUE_VK_CALLBACK values in every C-ABI flavour,
// and, with C-ABI as the only backend, BlueScriptCallback holding a host callback - set and read
// through TestCallbacks' C thunks, property and attribute, and called with wrapped arguments.

#include "TestCAbiUtil.h"
#include "TestCallbacks.h"
#include "TestMethods.h"

#include <set>

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
};

void RecordCall( void* userData, const BlueCValue* args, uint32_t argc )
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

	BlueCCallbackInvoke( handle, nullptr, 0 );
	EXPECT_EQ( recorder.calls, 1 );

	BlueCCallbackAddRef( handle );
	BlueCCallbackRelease( handle );
	EXPECT_EQ( recorder.destroyed, 0 );
	BlueCCallbackRelease( handle );
	EXPECT_EQ( recorder.destroyed, 1 );

	// Null handles are ignored
	BlueCCallbackAddRef( nullptr );
	BlueCCallbackRelease( nullptr );
	BlueCCallbackInvoke( nullptr, nullptr, 0 );
	EXPECT_EQ( BlueCCallbackGet( nullptr ), nullptr );
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

TEST( CAbiCallbacks, ReturnValuesAreUnsupported )
{
	TestCallbacksPtr callbacks;
	ASSERT_TRUE( callbacks.CreateInstance() );
	HostCallback host;

	Result set;
	Call( set, (TestCallbacks*)callbacks, "SetCallback", { Callback( host.handle ) } );
	ASSERT_CABI_OK( set );

	// A C callback returns nothing: Call( result, ... ) fails without calling it and leaves
	// the preset result
	Result pairInt, pairString;
	Call( pairInt, (TestCallbacks*)callbacks, "CallCallbackInt" );
	Call( pairString, (TestCallbacks*)callbacks, "CallCallbackStringInt", { Int( 5 ) } );
	EXPECT_CABI_OK( pairInt );
	ASSERT_EQ( pairInt.out.u.arr.count, size_t( 2 ) );
	EXPECT_EQ( pairInt.out.u.arr.items[0].u.i, 0 );
	EXPECT_EQ( pairInt.out.u.arr.items[1].u.i, int64_t( int( 0xDeadBeef ) ) );
	EXPECT_CABI_OK( pairString );
	EXPECT_EQ( pairString.out.u.arr.items[0].u.i, 0 );
	EXPECT_EQ( host.recorder.calls, 0 );
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

#endif // BLUE_CABI_CALLBACKS
