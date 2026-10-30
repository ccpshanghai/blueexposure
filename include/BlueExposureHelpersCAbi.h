// Copyright (c) 2026 CCP Games
//
// C-ABI backend: the thunks the exposure macros register, and lookup of members and methods
// through Be::ClassInfo. The C-ABI twin of BlueExposureHelpersPython.h; included from
// BlueExposureHelpers.h when BLUE_WITH_CABI is set.
//
//   BlueCMethodHelper< decltype( &C::F ), &C::F >                         BlueCMethodFn
//   BlueCMethodHelperWithOptionalArgs< decltype( &C::F ), &C::F, n >      BlueCMethodFn
//   BlueCFunctionHelper< decltype( &F ), &F >                             BlueCMethodFn (self ignored)
//   BlueCPropertyGetter< C, decltype( &C::Get ), &C::Get >                BlueCGetFn
//   BlueCPropertySetter< C, decltype( &C::Set ), &C::Set >                BlueCSetFn
//
// A thunk resolves self (an IRoot*) to the method's class with dynamic_cast, as BluePythonCast
// does, and never lets a C++ exception or a failed Be::Result escape: both become BlueCError.
//
// Alongside, the exposure macros record what a thunk takes and returns, in the type names of
// BlueCAbiTypeName.h: BlueCGetSignature for methods and functions, BlueCPropertyTypeName for
// properties.

#pragma once
#ifndef BlueExposureHelpersCAbi_h
#define BlueExposureHelpersCAbi_h

#include "BlueCAbiCall.h"
#include "BlueCAbiTypeName.h"
#include <exception>
#include <typeinfo>
#include <vector>

namespace BlueCAbiDetail
{

// Run body( out, err ) with out reset to NONE and err never null (a local one stands in when
// the caller passes null), turning exceptions into BLUE_E_EXCEPTION. out may be null to
// discard the result.
template< typename Body >
int32_t Guard( BlueCValue* out, BlueCError* err, Body&& body )
{
	BlueCValue discard;
	BlueCValue& result = out ? *out : discard;
	BlueCValueSetNone( result );

	BlueCError localError = { BLUE_OK, nullptr, 0 };
	BlueCError& error = err ? *err : localError;

	int32_t status = BLUE_OK;
	try
	{
		status = body( result, error );
	}
	catch( const std::exception& e )
	{
		status = BlueCSetError( &error, BLUE_E_EXCEPTION, "C++ exception: %s", e.what() );
	}
	catch( ... )
	{
		status = BlueCSetError( &error, BLUE_E_EXCEPTION, "unknown C++ exception" );
	}

	if( status != BLUE_OK || !out )
	{
		BlueCValueRelease( &result );
	}
	BlueCErrorClear( &localError );
	return status;
}

// self as a C*, or null with BLUE_E_SELF in err
template< typename C >
C* GetSelf( BlueObject* self, BlueCError& err )
{
	if( !self )
	{
		BlueCSetError( &err, BLUE_E_SELF, "method called without an object" );
		return nullptr;
	}
	IRoot* obj = reinterpret_cast<IRoot*>( self );
	C* p = dynamic_cast<C*>( obj );
	if( !p )
	{
		BlueCSetError( &err, BLUE_E_SELF, "object of class %s is not a %s", BlueCClassNameOf( obj ), BlueCDemangle( typeid( C ).name() ).c_str() );
	}
	return p;
}

} // namespace BlueCAbiDetail


//--------------------------------------------------------------------
// Thunks
//--------------------------------------------------------------------

template< typename memFnType, memFnType memFn, int numOptional >
int32_t BlueCMethodHelperWithOptionalArgs( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	static_assert( numOptional >= 0, "numOptional must not be negative" );
	typedef typename BlueCAbiDetail::FunctionTraits<memFnType>::Class Class;

	return BlueCAbiDetail::Guard( out, err, [&]( BlueCValue& result, BlueCError& error ) -> int32_t {
		Class* pThis = BlueCAbiDetail::GetSelf<Class>( self, error );
		if( !pThis )
		{
			return error.code;
		}
		auto call = [pThis]( auto&&... a ) -> decltype( auto ) {
			return ( pThis->*memFn )( std::forward<decltype( a )>( a )... );
		};
		return BlueCAbiDetail::Invoke<memFnType, size_t( numOptional )>( call, args, argc, result, &error );
	} );
}

template< typename memFnType, memFnType memFn >
int32_t BlueCMethodHelper( BlueObject* self, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	return BlueCMethodHelperWithOptionalArgs<memFnType, memFn, 0>( self, args, argc, out, err );
}

template< typename FnType, FnType fn >
int32_t BlueCFunctionHelper( BlueObject*, const BlueCValue* args, uint32_t argc, BlueCValue* out, BlueCError* err )
{
	return BlueCAbiDetail::Guard( out, err, [&]( BlueCValue& result, BlueCError& error ) -> int32_t {
		FnType call = fn;
		return BlueCAbiDetail::Invoke<FnType, 0>( call, args, argc, result, &error );
	} );
}

// Getter of a MAP_PROPERTY: R Class::Get() const, or Be::Result<T> Class::Get( V& ) const
template< typename Class, typename getterFnType, getterFnType getterFn >
int32_t BlueCPropertyGetter( BlueObject* self, BlueCValue* out, BlueCError* err )
{
	typedef BlueCAbiDetail::FunctionTraits<getterFnType> Traits;
	typedef typename Traits::Return R;

	return BlueCAbiDetail::Guard( out, err, [&]( BlueCValue& result, BlueCError& error ) -> int32_t {
		Class* pThis = BlueCAbiDetail::GetSelf<Class>( self, error );
		if( !pThis )
		{
			return error.code;
		}

		if constexpr( is_be_result<R>::type::value )
		{
			static_assert( Traits::arity == 1, "Getters returning Be::Result must have one argument, as a reference" );
			typedef BlueCAbiDetail::ArgumentType<getterFnType, 0> A1;
			static_assert( std::is_reference<A1>::value, "Getters returning Be::Result must have one argument, as a reference" );
			typedef typename remove_const_and_reference<A1>::type Value;

			Value value{};
			R r = ( pThis->*getterFn )( value );
			if( !BeIsSuccess( r ) )
			{
				return BlueCAbiDetail::ResultError( r, &error );
			}
			return BlueCConvert<Value>::Wrap( value, result, &error ) ? int32_t( BLUE_OK ) : error.code;
		}
		else
		{
			static_assert( Traits::arity == 0, "Getter function can't take any arguments" );
			typedef typename remove_const_and_reference<R>::type Value;

			const Value& value = ( pThis->*getterFn )();
			return BlueCConvert<Value>::Wrap( value, result, &error ) ? int32_t( BLUE_OK ) : error.code;
		}
	} );
}

// Setter of a MAP_PROPERTY: void / R / Be::Result<T> Class::Set( V )
template< typename Class, typename setterFnType, setterFnType setterFn >
int32_t BlueCPropertySetter( BlueObject* self, const BlueCValue* in, BlueCError* err )
{
	typedef BlueCAbiDetail::FunctionTraits<setterFnType> Traits;
	typedef typename Traits::Return R;
	static_assert( Traits::arity == 1, "Setter function must take one argument" );

	return BlueCAbiDetail::Guard( nullptr, err, [&]( BlueCValue&, BlueCError& error ) -> int32_t {
		if( !in )
		{
			return BlueCSetError( &error, BLUE_E_INVALID_ARGUMENT, "no value to set" );
		}
		Class* pThis = BlueCAbiDetail::GetSelf<Class>( self, error );
		if( !pThis )
		{
			return error.code;
		}

		typedef typename BlueCArgStorage< BlueCAbiDetail::ArgumentType<setterFnType, 0> >::type Value;
		Value value{};
		if( !BlueCConvert<Value>::Extract( *in, value, BLUE_CABI_VALUE_INDEX, &error ) )
		{
			return error.code;
		}

		if constexpr( is_be_result<R>::type::value )
		{
			R r = ( pThis->*setterFn )( value );
			if( !BeIsSuccess( r ) )
			{
				return BlueCAbiDetail::ResultError( r, &error );
			}
		}
		else
		{
			( pThis->*setterFn )( value );
		}
		return BLUE_OK;
	} );
}


//--------------------------------------------------------------------
// Signatures
//--------------------------------------------------------------------

// What a C method or module function takes and returns, as a caller of its thunk sees it
// (BlueCAbiCall.h): a Be::Result's out parameter is the return value, not an argument. The
// strings are BlueCAbiTypeName.h names; they and the args array live as long as the process.
struct BlueCSignature
{
	const char* returns;			// "void" when the call returns NONE
	const char* const* args;		// argCount names (may be null when argCount is 0)
	uint32_t argCount;				// arguments a caller passes at most
	uint32_t optionalCount;			// how many trailing ones it may omit
};

namespace BlueCAbiDetail
{

template< typename F, typename Sequence >
struct SignatureArguments;

// The names of the argument types the thunk extracts into, ending with a null entry
template< typename F, size_t... I >
struct SignatureArguments< F, std::index_sequence<I...> >
{
	static const char* const* Get()
	{
		static const char* const s_args[sizeof...( I ) + 1] = { BlueCTypeName< typename BlueCArgStorage< ArgumentType<F, I> >::type >::Get()..., nullptr };
		return s_args;
	}
};

// The name of what Invoke<F> hands back
template< typename F >
const char* SignatureReturn()
{
	constexpr ReturnKind kind = GetReturnKind<F>();
	typedef typename LastArgument<F>::type Last;

	if constexpr( kind == ReturnKind::Void || kind == ReturnKind::Result )
	{
		return BlueCTypeName<void>::Get();
	}
	else if constexpr( kind == ReturnKind::Value )
	{
		return BlueCTypeName< typename remove_const_and_reference< typename FunctionTraits<F>::Return >::type >::Get();
	}
	else if constexpr( kind == ReturnKind::ResultObject )
	{
		return BlueCTypeName< typename std::remove_pointer<Last>::type >::Get();
	}
	else
	{
		return BlueCTypeName< typename remove_const_and_reference<Last>::type >::Get();
	}
}

} // namespace BlueCAbiDetail

// The signature of the thunk BlueCMethodHelperWithOptionalArgs / BlueCFunctionHelper makes of a
// function of type F
template< typename F >
BlueCSignature BlueCGetSignature( uint32_t numOptional )
{
	constexpr size_t arity = BlueCAbiDetail::ScriptArity<F>();
	BlueCSignature signature = {
		BlueCAbiDetail::SignatureReturn<F>(),
		BlueCAbiDetail::SignatureArguments< F, std::make_index_sequence<arity> >::Get(),
		uint32_t( arity ),
		numOptional,
	};
	return signature;
}

// The type name of a MAP_PROPERTY's value: what its getter returns (BlueCPropertyGetter)
template< typename getterFnType >
const char* BlueCPropertyTypeName()
{
	typedef typename BlueCAbiDetail::FunctionTraits<getterFnType>::Return R;
	if constexpr( is_be_result<R>::type::value )
	{
		return BlueCTypeName< typename remove_const_and_reference< BlueCAbiDetail::ArgumentType<getterFnType, 0> >::type >::Get();
	}
	else
	{
		return BlueCTypeName< typename remove_const_and_reference<R>::type >::Get();
	}
}


//--------------------------------------------------------------------
// Lookup through Be::ClassInfo (BlueCAbi.cpp)
//
// Names are UTF-8 with an explicit length. Classes are searched before their parents, so a
// class's own member or method hides an inherited one of the same name.
//--------------------------------------------------------------------

// The method named name of type or one of its parents, or else an interface method
// (BLUE_REGISTER_CTHUNKER) of an interface that type or one of its parents maps; null if none.
// A class's methods hide its interfaces' methods of the same name, as in Python.
BLUEIMPORT const BlueCMethodDef* BlueCFindMethod( const Be::ClassInfo* type, const char* name, size_t nameLength );

// Called by BlueCForEachMethod with a method and its C signature (null when none was recorded:
// an interface method registered without signatures). Returns false to stop.
typedef bool ( *BlueCMethodVisitor )( const BlueCMethodDef* method, const BlueCSignature* signature, void* context );

// Visit every method BlueCFindMethod can find for type, in its lookup order (own methods,
// parents' methods, interface methods), each name once with the method BlueCFindMethod returns
// for it. Returns false if visitor stopped early.
BLUEIMPORT bool BlueCForEachMethod( const Be::ClassInfo* type, BlueCMethodVisitor visitor, void* context );

// The function named name in a module's function list (BlueRegistration::GetCFuncRegs), or null
BLUEIMPORT const BlueCMethodDef* BlueCFindFunction( const std::vector<BlueCMethodDef>& functions, const char* name, size_t nameLength );

// The member (attribute or property) named name of type or one of its parents, or null.
// Hidden members are skipped. extraOffset (optional) receives the parent offset to pass to
// BeMapMemberOffset for an attribute.
BLUEIMPORT const Be::VarEntry* BlueCFindMember( const Be::ClassInfo* type, const char* name, size_t nameLength, ptrdiff_t* extraOffset );

// The type name (BlueCAbiTypeName.h) of a member as BlueCGetMember reads it: a property's
// recorded mCTypeName; for an attribute, derived from its VARTYPE, size and IID as the attribute
// conversion does. A member the C ABI cannot read is "unknown:<VARTYPE>" (a property without a C
// getter, a VARTYPE without a conversion).
BLUEIMPORT const char* BlueCMemberTypeName( const Be::VarEntry& entry );

// Read a member of obj: a property through its C getter, an attribute through its VARTYPE
BLUEIMPORT int32_t BlueCGetMember( IRoot* obj, const char* name, size_t nameLength, BlueCValue* out, BlueCError* err );

// Assign a member of obj: a property through its C setter, an attribute through its VARTYPE
// (if it is writable; INotify::OnModified is called for NOTIFY attributes)
BLUEIMPORT int32_t BlueCSetMember( IRoot* obj, const char* name, size_t nameLength, const BlueCValue* in, BlueCError* err );

#endif // BlueExposureHelpersCAbi_h
