// Copyright (c) 2026 CCP Games
//
// C-ABI backend: calling a C++ function or method with BlueCValue arguments.
//
// The C-ABI twin of BlueCallFunction.h / BlueCallFunctionWithBeResult.h, written once with
// variadic templates instead of once per arity. It keeps the Python backend's conventions:
//
//   R f( A... )                          returns R; void returns NONE
//   Be::Result<T> f( A... )              NONE on success, BLUE_E_FAILED on failure
//   Be::Result<T> f( A..., X** out )     X is a Blue class or interface: the script passes A...,
//                                        and on success gets *out (the function's reference
//                                        is handed to the caller), or NONE if it is null
//   Be::Result<T> f( A..., V& out )      the script passes A..., and on success gets out
//   optional arguments                   the last numOptional arguments may be omitted; an
//                                        omitted one gets BlueGetNullValue
//
// Failed Be::Results report BeGetErrorMessage; C++ exceptions are caught by the thunks in
// BlueExposureHelpersCAbi.h. Argument indices in messages are 0-based, as in Python's.

#pragma once
#ifndef BlueCAbiCall_h
#define BlueCAbiCall_h

#include "BlueCAbiValue.h"
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace BlueCAbiDetail
{

//--------------------------------------------------------------------
// Function traits: free functions and member functions, const and noexcept
//--------------------------------------------------------------------

template< typename F >
struct FunctionTraits;

template< typename R, typename... A >
struct FunctionTraits< R ( * )( A... ) >
{
	typedef R Return;
	typedef void Class;
	typedef std::tuple<A...> Arguments;
	static const size_t arity = sizeof...( A );
};

template< typename R, typename... A >
struct FunctionTraits< R ( * )( A... ) noexcept > : FunctionTraits< R ( * )( A... ) >
{
};

template< typename R, typename C, typename... A >
struct FunctionTraits< R ( C::* )( A... ) > : FunctionTraits< R ( * )( A... ) >
{
	typedef C Class;
};

template< typename R, typename C, typename... A >
struct FunctionTraits< R ( C::* )( A... ) const > : FunctionTraits< R ( * )( A... ) >
{
	typedef C Class;
};

template< typename R, typename C, typename... A >
struct FunctionTraits< R ( C::* )( A... ) noexcept > : FunctionTraits< R ( * )( A... ) >
{
	typedef C Class;
};

template< typename R, typename C, typename... A >
struct FunctionTraits< R ( C::* )( A... ) const noexcept > : FunctionTraits< R ( * )( A... ) >
{
	typedef C Class;
};

template< typename F, size_t I >
using ArgumentType = typename std::tuple_element< I, typename FunctionTraits<F>::Arguments >::type;


//--------------------------------------------------------------------
// How the result of a call reaches the script
//--------------------------------------------------------------------

enum class ReturnKind
{
	Void,				// void f( ... )
	Value,				// R f( ... )
	Result,				// Be::Result<T> f( ... )
	ResultObject,		// Be::Result<T> f( ..., X** )
	ResultReference,	// Be::Result<T> f( ..., V& )
};

template< typename F, size_t Arity = FunctionTraits<F>::arity >
struct LastArgument
{
	typedef ArgumentType<F, Arity - 1> type;
};

template< typename F >
struct LastArgument< F, 0 >
{
	typedef void type;
};

template< typename F >
constexpr ReturnKind GetReturnKind()
{
	typedef typename FunctionTraits<F>::Return R;
	typedef typename LastArgument<F>::type Last;

	if( std::is_void<R>::value )
	{
		return ReturnKind::Void;
	}
	if( !is_be_result<R>::type::value )
	{
		return ReturnKind::Value;
	}
	if( FunctionTraits<F>::arity == 0 )
	{
		return ReturnKind::Result;
	}
	if( is_pointer_to_pointer_to_blue<Last>::type::value )
	{
		return ReturnKind::ResultObject;
	}
	if( std::is_reference<Last>::value )
	{
		return ReturnKind::ResultReference;
	}
	return ReturnKind::Result;
}

// Number of arguments the script passes
template< typename F >
constexpr size_t ScriptArity()
{
	return ( GetReturnKind<F>() == ReturnKind::ResultObject || GetReturnKind<F>() == ReturnKind::ResultReference )
		? FunctionTraits<F>::arity - 1
		: FunctionTraits<F>::arity;
}

// The storage for the script arguments, in order
template< typename F, typename Sequence >
struct ArgumentStorage;

template< typename F, size_t... I >
struct ArgumentStorage< F, std::index_sequence<I...> >
{
	typedef std::tuple< typename BlueCArgStorage< ArgumentType<F, I> >::type... > type;
};


//--------------------------------------------------------------------
// Arguments
//--------------------------------------------------------------------

BLUEIMPORT bool CheckArgumentCount( const BlueCValue* args, uint32_t argc, size_t arity, size_t numOptional, BlueCError* err );

template< size_t I, size_t Arity, size_t NumOptional, typename Storage >
bool ExtractArgument( Storage& storage, const BlueCValue* args, uint32_t argc, BlueCError* err )
{
	// Only the trailing NumOptional arguments may be missing; CheckArgumentCount saw to the rest
	if constexpr( NumOptional > 0 && I + NumOptional >= Arity )
	{
		if( I >= argc )
		{
			BlueGetNullValue( storage );
			return true;
		}
	}
	return BlueCConvert<Storage>::Extract( args[I], storage, unsigned( I ), err );
}

template< size_t Arity, size_t NumOptional, typename Tuple, size_t... I >
bool ExtractArguments( Tuple& storage, const BlueCValue* args, uint32_t argc, BlueCError* err, std::index_sequence<I...> )
{
	(void)args;
	(void)argc;
	(void)err;
	return ( ExtractArgument<I, Arity, NumOptional>( std::get<I>( storage ), args, argc, err ) && ... );
}

template< typename Call, typename Tuple, size_t... I, typename... Extra >
decltype( auto ) Apply( Call& call, Tuple& storage, std::index_sequence<I...>, Extra&&... extra )
{
	(void)storage;
	return call( std::get<I>( storage )..., std::forward<Extra>( extra )... );
}

template< typename R >
int32_t ResultError( const R& result, BlueCError* err )
{
	const char* message = BeGetErrorMessage( result );
	return BlueCSetError( err, BLUE_E_FAILED, "%s", message ? message : "failed" );
}


//--------------------------------------------------------------------
// Invoke: extract, call, wrap
//
// call is a callable taking the C++ function's parameters (a bound member function for
// methods). F is the function type, which drives the conventions above. err must not be null;
// on failure it holds the returned status. out is NONE on entry and stays NONE on failure.
//--------------------------------------------------------------------

template< typename F, size_t NumOptional, typename Call >
int32_t Invoke( Call& call, const BlueCValue* args, uint32_t argc, BlueCValue& out, BlueCError* err )
{
	constexpr ReturnKind kind = GetReturnKind<F>();
	constexpr size_t arity = ScriptArity<F>();
	typedef std::make_index_sequence<arity> Sequence;
	typedef typename FunctionTraits<F>::Return R;

	if( !CheckArgumentCount( args, argc, arity, NumOptional, err ) )
	{
		return err->code;
	}

	typename ArgumentStorage<F, Sequence>::type storage{};
	if( !ExtractArguments<arity, NumOptional>( storage, args, argc, err, Sequence() ) )
	{
		return err->code;
	}

	if constexpr( kind == ReturnKind::Void )
	{
		Apply( call, storage, Sequence() );
		return BLUE_OK;
	}
	else if constexpr( kind == ReturnKind::Value )
	{
		typedef typename remove_const_and_reference<R>::type Stripped;
		const Stripped& result = Apply( call, storage, Sequence() );
		if( !BlueCConvert<Stripped>::Wrap( result, out, err ) )
		{
			return err->code;
		}
		return BLUE_OK;
	}
	else if constexpr( kind == ReturnKind::Result )
	{
		R result = Apply( call, storage, Sequence() );
		if( !BeIsSuccess( result ) )
		{
			return ResultError( result, err );
		}
		return BLUE_OK;
	}
	else if constexpr( kind == ReturnKind::ResultObject )
	{
		typedef typename std::remove_pointer< typename LastArgument<F>::type >::type ObjectPointer;
		ObjectPointer p{ nullptr };
		R result = Apply( call, storage, Sequence(), &p );
		if( !BeIsSuccess( result ) )
		{
			// The function must not hand out an object on failure (the Python backend asserts
			// this); a non-null p is left alone rather than released twice
			return ResultError( result, err );
		}
		if( p )
		{
			// Wrapping locks, so give up the reference the function handed over
			IRoot* obj = reinterpret_cast<IRoot*>( p );
			BlueCWrapObject( obj, out );
			obj->Unlock();
		}
		return BLUE_OK;
	}
	else
	{
		typedef typename remove_const_and_reference< typename LastArgument<F>::type >::type Value;
		Value value{};
		R result = Apply( call, storage, Sequence(), value );
		if( !BeIsSuccess( result ) )
		{
			return ResultError( result, err );
		}
		if( !BlueCConvert<Value>::Wrap( value, out, err ) )
		{
			return err->code;
		}
		return BLUE_OK;
	}
}

} // namespace BlueCAbiDetail

#endif // BlueCAbiCall_h
