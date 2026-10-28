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

#pragma once
#ifndef BlueExposureHelpersCAbi_h
#define BlueExposureHelpersCAbi_h

#include "BlueCAbiCall.h"
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
		BlueCSetError( &err, BLUE_E_SELF, "object of class %s is not a %s", BlueCClassNameOf( obj ), BlueCTypeName( typeid( C ).name() ).c_str() );
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
// Lookup through Be::ClassInfo (BlueCAbi.cpp)
//
// Names are UTF-8 with an explicit length. Classes are searched before their parents, so a
// class's own member or method hides an inherited one of the same name.
//--------------------------------------------------------------------

// The method named name of type or one of its parents, or null
BLUEIMPORT const BlueCMethodDef* BlueCFindMethod( const Be::ClassInfo* type, const char* name, size_t nameLength );

// The function named name in a module's function list (BlueRegistration::GetCFuncRegs), or null
BLUEIMPORT const BlueCMethodDef* BlueCFindFunction( const std::vector<BlueCMethodDef>& functions, const char* name, size_t nameLength );

// The member (attribute or property) named name of type or one of its parents, or null.
// Hidden members are skipped. extraOffset (optional) receives the parent offset to pass to
// BeMapMemberOffset for an attribute.
BLUEIMPORT const Be::VarEntry* BlueCFindMember( const Be::ClassInfo* type, const char* name, size_t nameLength, ptrdiff_t* extraOffset );

// Read a member of obj: a property through its C getter, an attribute through its VARTYPE
BLUEIMPORT int32_t BlueCGetMember( IRoot* obj, const char* name, size_t nameLength, BlueCValue* out, BlueCError* err );

// Assign a member of obj: a property through its C setter, an attribute through its VARTYPE
// (if it is writable; INotify::OnModified is called for NOTIFY attributes)
BLUEIMPORT int32_t BlueCSetMember( IRoot* obj, const char* name, size_t nameLength, const BlueCValue* in, BlueCError* err );

#endif // BlueExposureHelpersCAbi_h
