////////////////////////////////////////////////////////////
//
//    Creator:   Filipp Pavlov
//    Created:   August 2013
// Copyright (c) 2026 CCP Games
//

#pragma once
#ifndef BlueScriptCallback_H
#define BlueScriptCallback_H

#include "BlueTypes.h"
#include "BlueExtractArgument.h"
#include "BlueWrapReturnValue.h"
#if BLUE_CABI_CALLBACKS
#include "BlueCAbiValue.h"
#include <string>
#endif

// Status of call
// Note that clang doesn't allow BLUEIMPORT on an embedded class - have to
// have this toplevel rather than inside the scope of BlueScriptCallback.
class BLUEIMPORT BlueScriptCallbackStatus
{
public:
	BlueScriptCallbackStatus( const BlueScriptCallbackStatus& );
	BlueScriptCallbackStatus& operator=( const BlueScriptCallbackStatus& );
	~BlueScriptCallbackStatus();
	operator bool() const;
	void MuteException();
	void ReportException();
#if BLUE_CABI_CALLBACKS
	// Why the call failed (the callback's message, or a conversion error); empty on success
	const std::string& GetErrorMessage() const { return m_message; }
#endif
private:
	enum Status
	{
		OK,
		CALL_ERROR,
		EXCEPTION
	};
	BlueScriptCallbackStatus( Status hasException );
#if BLUE_CABI_CALLBACKS
	BlueScriptCallbackStatus( Status hasException, const char* message );
#endif

	Status m_hasException;
	bool m_muteException;
	bool m_exceptionReported;

#if BLUE_WITH_PYTHON
	PyObject* m_type;
	PyObject* m_value;
	PyObject* m_traceback;
#elif BLUE_CABI_CALLBACKS
	// Why the call failed, logged by ReportException
	std::string m_message;
#endif
	friend class BLUEIMPORT BlueScriptCallback;
};

// --------------------------------------------------------------------------------------
// Description:
//   BlueScriptCallback encapsulates script callback functions. One can store a script 
//   function in BlueScriptCallback and subsequently call it from C++.
//   With C-ABI as the only backend it holds a reference to a host callback handle
//   (BlueCallbackHandle, BlueCAbi.h). The arguments are converted with BlueCWrap and the
//   value a Call overload expects with BlueCExtract (CALL_ERROR on a mismatch, a returned
//   Blue object pointer carrying a reference of its own, as with Python). A callback that
//   reports failure makes the status CALL_ERROR, and its message is what ReportException
//   logs. Calls run on the calling thread, without any lock.
// --------------------------------------------------------------------------------------
class BLUEIMPORT BlueScriptCallback
{
public:
	BlueScriptCallback();
	BlueScriptCallback( const BlueScriptCallback& other );
	~BlueScriptCallback();

	BlueScriptCallback& operator=( const BlueScriptCallback& other );

	bool IsValid() const;
	operator bool() const { return IsValid(); }

	void Destroy();

	BlueScriptCallbackStatus CallVoid();
	
	template <typename Ret>
	BlueScriptCallbackStatus Call( Ret& returnValue );

	template <typename A0>
	BlueScriptCallbackStatus CallVoid( A0 a0 );

	template <typename Ret, typename A0>
	BlueScriptCallbackStatus Call( Ret& returnValue, A0 a0 );

	template <typename A0, typename A1>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1 );

	template <typename Ret, typename A0, typename A1>
	BlueScriptCallbackStatus Call( Ret& returnValue, A0 a0, A1 a1 );

	template <typename A0, typename A1, typename A2>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1, A2 a2 );

	template <typename Ret, typename A0, typename A1, typename A2>
	BlueScriptCallbackStatus Call( Ret& returnValue, A0 a0, A1 a1, A2 a2 );

	template <typename A0, typename A1, typename A2, typename A3>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1, A2 a2, A3 a3 );

	template <typename Ret, typename A0, typename A1, typename A2, typename A3>
	BlueScriptCallbackStatus Call( Ret& returnValue, A0 a0, A1 a1, A2 a2, A3 a3 );

	template <typename A0, typename A1, typename A2, typename A3, typename A4>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4 );

	template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5);

	template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
	BlueScriptCallbackStatus CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6);

#if BLUE_CABI_CALLBACKS
	// Holds its own reference to handle; null makes an empty callback
	explicit BlueScriptCallback( BlueCallbackHandle* handle );

	// The handle (no new reference), or null for an empty callback
	BlueCallbackHandle* GetCHandle() const { return m_callback; }
#endif

private:
#if BLUE_CABI_CALLBACKS
	BlueCallbackHandle* m_callback;

	template <typename... A>
	bool InvokeC( BlueCValue* result, BlueCError& error, const A&... a );

	template <typename... A>
	BlueScriptCallbackStatus CallC( const A&... a );

	template <typename Ret, typename... A>
	BlueScriptCallbackStatus CallReturningC( Ret& returnValue, const A&... a );

	// CALL_ERROR with error's message
	static BlueScriptCallbackStatus FailedC( const BlueCError& error );
#else
	BlueScriptValue m_callback;
#endif

	friend BLUEIMPORT BlueScriptValue BlueWrapReturnValueImpl( 
		BlueScriptArguments args, 
		const BlueScriptCallback& val );
	friend BLUEIMPORT bool BlueExtractArgumentImpl( 
		BlueScriptValue argument, 
		BlueScriptCallback& result, 
		unsigned int argID, 
		std::false_type isBlueType );

	template <typename T>
	static inline void RefIRoot( T obj, std::false_type isBlueObj )
	{
	}

	template <typename T>
	static inline void RefIRoot( T obj, std::true_type isBlueObj )
	{
		obj->Lock();
	}

	template <typename T>
	static inline void RefIRoot( T obj )
	{
		BlueScriptCallback::RefIRoot( obj, typename is_pointer_to_blue<T>::type() );
	}

};

inline void BlueGetNullValue( BlueScriptCallback& resultRef )
{
	resultRef = BlueScriptCallback();
}


template <typename Ret>
BlueScriptCallbackStatus BlueScriptCallback::Call( Ret& returnValue )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, nullptr );
	if( ret )
	{
		if( !BlueExtractArgument( ret, returnValue, 0 ) )
		{
			return BlueScriptCallbackStatus::EXCEPTION;
		}
		BlueScriptCallback::RefIRoot( returnValue );
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallReturningC( returnValue );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename A0>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, nullptr );
	Py_DECREF( arg0 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename Ret, typename A0>
BlueScriptCallbackStatus BlueScriptCallback::Call( Ret& returnValue, A0 a0 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, nullptr );
	Py_DECREF( arg0 );
	if( ret )
	{
		if( !BlueExtractArgument( ret, returnValue, 0 ) )
		{
			return BlueScriptCallbackStatus::EXCEPTION;
		}
		BlueScriptCallback::RefIRoot( returnValue );
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallReturningC( returnValue, a0 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename A0, typename A1>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename Ret, typename A0, typename A1>
BlueScriptCallbackStatus BlueScriptCallback::Call( Ret& returnValue, A0 a0, A1 a1 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	if( ret )
	{
		if( !BlueExtractArgument( ret, returnValue, 0 ) )
		{
			return BlueScriptCallbackStatus::EXCEPTION;
		}
		BlueScriptCallback::RefIRoot( returnValue );
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallReturningC( returnValue, a0, a1 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename A0, typename A1, typename A2>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1, A2 a2 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1, a2 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename Ret, typename A0, typename A1, typename A2>
BlueScriptCallbackStatus BlueScriptCallback::Call( Ret& returnValue, A0 a0, A1 a1, A2 a2 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	if( ret )
	{
		if( !BlueExtractArgument( ret, returnValue, 0 ) )
		{
			return BlueScriptCallbackStatus::EXCEPTION;
		}
		BlueScriptCallback::RefIRoot( returnValue );
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallReturningC( returnValue, a0, a1, a2 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename A0, typename A1, typename A2, typename A3>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1, A2 a2, A3 a3 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	BlueScriptValue arg3 = BlueWrapReturnValue( args, a3 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, arg3, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	Py_DECREF( arg3 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1, a2, a3 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename Ret, typename A0, typename A1, typename A2, typename A3>
BlueScriptCallbackStatus BlueScriptCallback::Call( Ret& returnValue, A0 a0, A1 a1, A2 a2, A3 a3 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	BlueScriptValue arg3 = BlueWrapReturnValue( args, a3 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, arg3, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	Py_DECREF( arg3 );
	if( ret )
	{
		if( !BlueExtractArgument( ret, returnValue, 0 ) )
		{
			return BlueScriptCallbackStatus::EXCEPTION;
		}
		BlueScriptCallback::RefIRoot( returnValue );
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallReturningC( returnValue, a0, a1, a2, a3 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}


template <typename A0, typename A1, typename A2, typename A3, typename A4>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	BlueScriptValue arg3 = BlueWrapReturnValue( args, a3 );
	BlueScriptValue arg4 = BlueWrapReturnValue( args, a4 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, arg3, arg4, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	Py_DECREF( arg3 );
	Py_DECREF( arg4 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1, a2, a3, a4 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}

template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5 )
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	BlueScriptValue arg3 = BlueWrapReturnValue( args, a3 );
	BlueScriptValue arg4 = BlueWrapReturnValue( args, a4 );
	BlueScriptValue arg5 = BlueWrapReturnValue( args, a5 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, arg3, arg4, arg5, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	Py_DECREF( arg3 );
	Py_DECREF( arg4 );
	Py_DECREF( arg5 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1, a2, a3, a4, a5 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}

template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
BlueScriptCallbackStatus BlueScriptCallback::CallVoid( A0 a0, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6)
{
	if( !IsValid() )
	{
		return BlueScriptCallbackStatus::CALL_ERROR;
	}

#if BLUE_WITH_PYTHON
	auto gil = PyGILState_Ensure();
	ON_BLOCK_EXIT( [&gil] { PyGILState_Release( gil ); } );

	BlueScriptArguments args = 0;

	BlueScriptValue arg0 = BlueWrapReturnValue( args, a0 );
	BlueScriptValue arg1 = BlueWrapReturnValue( args, a1 );
	BlueScriptValue arg2 = BlueWrapReturnValue( args, a2 );
	BlueScriptValue arg3 = BlueWrapReturnValue( args, a3 );
	BlueScriptValue arg4 = BlueWrapReturnValue( args, a4 );
	BlueScriptValue arg5 = BlueWrapReturnValue( args, a5 );
	BlueScriptValue arg6 = BlueWrapReturnValue( args, a6 );
	PyObject* ret = PyObject_CallFunctionObjArgs( m_callback, arg0, arg1, arg2, arg3, arg4, arg5, arg6, nullptr );
	Py_DECREF( arg0 );
	Py_DECREF( arg1 );
	Py_DECREF( arg2 );
	Py_DECREF( arg3 );
	Py_DECREF( arg4 );
	Py_DECREF( arg5 );
	Py_DECREF( arg6 );
	if( ret )
	{
		Py_DECREF( ret );
		return BlueScriptCallbackStatus::OK;
	}
	return BlueScriptCallbackStatus::EXCEPTION;
#elif BLUE_CABI_CALLBACKS
	return CallC( a0, a1, a2, a3, a4, a5, a6 );
#else
	return BlueScriptCallbackStatus::EXCEPTION;
#endif
}

#if BLUE_CABI_CALLBACKS
// Wraps every argument as a BlueCValue and calls the C callback. result (null to discard it)
// receives the returned value. On failure error says why.
template <typename... A>
bool BlueScriptCallback::InvokeC( BlueCValue* result, BlueCError& error, const A&... a )
{
	// One more than needed, so that no arguments is not a zero-length array
	BlueCValue args[sizeof...( A ) + 1];
	for( BlueCValue& arg : args )
	{
		BlueCValueSetNone( arg );
	}
	ON_BLOCK_EXIT( [&args] {
		for( BlueCValue& arg : args )
		{
			BlueCValueRelease( &arg );
		}
	} );

	// Wrapped in order; argc counts the wrapped ones, so the first failure stops the rest
	uint32_t argc = 0;
	const bool wrapped = ( ( BlueCWrap( a, args[argc], &error ) && ++argc ) && ... );
	if( !wrapped )
	{
		return false;
	}
	return BlueCCallbackInvoke( m_callback, args, argc, result, &error ) == BLUE_OK;
}

// Calls the C callback and drops any value it returns
template <typename... A>
BlueScriptCallbackStatus BlueScriptCallback::CallC( const A&... a )
{
	BlueCError error = { BLUE_OK, nullptr, 0 };
	ON_BLOCK_EXIT( [&error] { BlueCErrorClear( &error ); } );
	if( !InvokeC( nullptr, error, a... ) )
	{
		return FailedC( error );
	}
	return BlueScriptCallbackStatus::OK;
}

// Calls the C callback and converts the value it returns into returnValue. A pointer to a Blue
// object gets a reference of its own, as with Python (RefIRoot, which does not expect null);
// the returned value itself is released.
template <typename Ret, typename... A>
BlueScriptCallbackStatus BlueScriptCallback::CallReturningC( Ret& returnValue, const A&... a )
{
	BlueCValueHolder result;
	BlueCError error = { BLUE_OK, nullptr, 0 };
	ON_BLOCK_EXIT( [&error] { BlueCErrorClear( &error ); } );
	if( !InvokeC( &result.Get(), error, a... ) )
	{
		return FailedC( error );
	}
	if( !BlueCExtract( result.Get(), returnValue, BLUE_CABI_RESULT_INDEX, &error ) )
	{
		return FailedC( error );
	}
	if constexpr( is_pointer_to_blue<Ret>::type::value )
	{
		// NONE extracts as null
		if( returnValue )
		{
			returnValue->Lock();
		}
	}
	return BlueScriptCallbackStatus::OK;
}

// A callback is CALLBACK, or NONE for an empty one. Extract takes blue's own reference to the
// handle (held by the BlueScriptCallback); wrap adds one for the caller.
template<>
struct BlueCConvert< BlueScriptCallback >
{
	static const bool supported = true;

	static bool Extract( const BlueCValue& value, BlueScriptCallback& result, unsigned argIndex, BlueCError* err )
	{
		BlueCallbackHandle* handle = nullptr;
		if( !BlueCExtractCallback( value, handle, argIndex, err ) )
		{
			return false;
		}
		result = BlueScriptCallback( handle );
		return true;
	}

	static bool Wrap( const BlueScriptCallback& value, BlueCValue& out, BlueCError* )
	{
		BlueCWrapCallback( value.GetCHandle(), out );
		return true;
	}
};
#endif

#endif