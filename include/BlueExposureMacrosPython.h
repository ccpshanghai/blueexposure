// Copyright (c) 2026 CCP Games
//
// Python backend of the exposure macros: the BLUE_PY_* fragments that the common macros in
// BlueExposureMacros.h expand, plus the macros that only exist for Python (methods taking raw
// PyObject* arguments, module init, exceptions). Included from BlueExposureMacros.h; with
// BLUE_WITH_PYTHON off every fragment expands to nothing, and exceptions are only names.

#pragma once

#if BLUE_WITH_PYTHON

//--------------------------------------------------------------------
// Fragments used by the common exposure macros
//--------------------------------------------------------------------

// EXPOSE_TO_BLUE: the tp_new of the class's Python type
#define BLUE_PY_EXPOSE_TO_BLUE_EXTRA \
	static PyObject* PyNew( PyTypeObject* type, PyObject* args, PyObject* kwds ) \
	{\
		return BlueCreateInstanceFromPython( *Clsid(), args, kwds );\
	}

// EXPOSURE_BEGIN_IMP: the Python type object and its method and member tables
#define BLUE_PY_BEGIN_EXTRA \
	static PyTypeObject s_pyType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) }; \
	static std::vector<PyMethodDef> s_methods;\
	static std::vector<PyMemberDef> s_memberDefs;

// EXPOSURE_END_IMPL: set up the Python type and hook it into _classinfo
#define BLUE_PY_END_EXTRA \
	BlueInitializePyType( &s_pyType, &_tmpclsid, _tmpdoc, &s_interfaces[0], PyNew ); \
	_classinfo.mTypeObject = &s_pyType; \
	BlueRegisterPyMethodDefs( &_classinfo, &s_methods); \
	_classinfo.mPyMethodTable = _classinfo.mTypeObject->tp_methods; \
	BlueRegisterPyMemberDefs( &_classinfo, &s_memberDefs );

// MAP_METHOD_AND_WRAP / MAP_METHOD_AND_WRAP_OPTIONAL_ARGS
#define BLUE_PY_ADD_METHOD_AND_WRAP( name, functionName, docString ) \
{\
	PyMethodDef d = { name, &BlueMethodHelper<decltype(&_Class::functionName), &_Class::functionName>, METH_VARARGS, docString };\
	s_methods.push_back( d );\
}

#define BLUE_PY_ADD_METHOD_AND_WRAP_OPTIONAL_ARGS( name, functionName, numOptional, docString ) \
{\
	PyMethodDef d = { name, &BlueMethodHelperWithOptionalArgs<decltype(&_Class::functionName), &_Class::functionName, numOptional>, METH_VARARGS, docString };\
	s_methods.push_back( d );\
}

// MAP_PROPERTY*: the Python getter and setter of a VarEntry
#define BLUE_PY_PROPERTY_GETTER( getterFunction ) \
	BluePropertyGetter<_Class, decltype(&_Class::getterFunction), &_Class::getterFunction>

#define BLUE_PY_PROPERTY_SETTER( setterFunction ) \
	BluePropertySetter<_Class, decltype(&_Class::setterFunction), &_Class::setterFunction>

// MAP_FUNCTION_AND_WRAP: a declaration, ending in ';'
#define BLUE_PY_MAP_FUNCTION_AND_WRAP( _expname, _func, _docstring ) \
	static FunctionRegistrar s_##_func##FunctionRegistrar(_expname, &BlueFunctionHelper<decltype(&_func), &_func>, _docstring);

// Use this function to cast a Blue-wrapped Python object to a concrete C++ type (Blue class).
// This function is typically used inside a Py C++ wrapper function for a C++ class method.
// Example:
//
// static PyObject* PyFoo( PyObject* self, PyObject* args ) 
// { 
//		Bongo* b = BluePythonCast<Bongo*>( self ); 
//		b->Foo();
//		Py_INCREF(Py_None); return PyNone;
// }
template< class POINTER_TYPE > POINTER_TYPE BluePythonCast( PyObject* self )
{
	return dynamic_cast<POINTER_TYPE>(BlueUnwrapObjectFromPython(self));
}

// Use this function to cast a Blue-wrapped Python object to a concrete C++ type (Blue class).
// This function is typically used inside a Py C++ wrapper function for a C++ class method.
// This is essentially the same as BluePythonCast, making use of the fact that each Blue
// object type has its own Python type object now. Introduced as a separate function
// so we can test it more thoroughly before having it replace BluePythonCast.
// Example:
//
// static PyObject* PyFoo( PyObject* self, PyObject* args ) 
// { 
//		Bongo* b = BluePythonStaticCast<Bongo*>( self ); 
//		b->Foo();
//		Py_INCREF(Py_None); return PyNone;
// }
template< class POINTER_TYPE > POINTER_TYPE BluePythonStaticCast( PyObject* self )
{
	PyTypeObject* type = self->ob_type;
	if( type != std::remove_pointer<POINTER_TYPE>::type::ClassType_()->mTypeObject )
	{
		return NULL;
	}

	BluePythonObject *wrap = static_cast<BluePythonObject *>(self);

	return static_cast<POINTER_TYPE>( wrap->mObj );
}

BLUEIMPORT PyObject* BlueCreateInstanceFromPython( const Be::Clsid& clsid, PyObject* args, PyObject* kwds );

//--------------------------------------------------------------------
// Python-only mapping macros
//--------------------------------------------------------------------

// MAP_METHOD maps a free function with the signature
// PyObject* pyFunc( PyObject* self, PyObject* args )
// as a method on the object in Python
#define MAP_METHOD( nameString, pyFunc, docString )\
{\
	PyMethodDef d = { nameString, &BlueFunctionAsMethodHelper<pyFunc>, METH_VARARGS, docString };\
	s_methods.push_back( d );\
}

// MAP_METHOD_AS_METHOD maps a member function with the signature
// PyObject* Class::pyFunc( PyObject* args )
// as a method on the object in Python
#define MAP_METHOD_AS_METHOD( nameString, pyFunc, docString )\
{\
	PyMethodDef d = { nameString, &BlueMethodAsMethodHelper<decltype(&_Class::pyFunc), &_Class::pyFunc>, METH_VARARGS, docString };\
	s_methods.push_back( d );\
}

// MAP_METHOD_WITH_KEYWORD_ARGUMENTS maps a member function with the signature
// PyObject* Class::pyFunc( PyObject* args, PyObject* kwargs )
// as a method on the object in Python
#define MAP_METHOD_WITH_KEYWORD_ARGUMENTS( nameString, pyFunc, docString )\
{ \
	PyMethodDef d = { \
		nameString, \
		reinterpret_cast<PyCFunction>( &BlueMethodWithKeywordArgumentsHelper<decltype( &_Class::pyFunc ), &_Class::pyFunc> ), \
		METH_VARARGS | METH_KEYWORDS, \
		docString }; \
	s_methods.push_back( d ); \
}

#ifdef _WIN32
#define BLUE_EXPORTED_INIT
#else
#define BLUE_EXPORTED_INIT __attribute__((visibility("default")))
#endif

// Use this macro to implement a standard module init function
#define BLUE_STANDARD_MODULE_INIT( moduleName ) \
	const char* g_moduleName = #moduleName; \
	PyMODINIT_FUNC BLUE_EXPORTED_INIT CCP_CONCATENATE( PyInit_, CCP_CONCATENATE( moduleName, CCP_BUILD_FLAVOR ) )() \
	{ \
		BeClasses->RegisterClasses( BlueRegistration::GetClassRegs() ); \
		static struct PyModuleDef moduledef = { \
			PyModuleDef_HEAD_INIT,                       \
			CCP_STRINGIZE( CCP_CONCATENATE( moduleName, CCP_BUILD_FLAVOR ) ),                                             \
			"", \
			-1, \
			nullptr, \
			nullptr, \
			nullptr, \
			nullptr, \
			nullptr, \
		}; \
		PyObject* module = PyModule_Create( &moduledef ); \
		if ( !module ) { \
			return nullptr; \
		} \
		BlueRegisterToModule( module, \
			BlueRegistration::GetClassRegs(), \
			BlueRegistration::GetFuncRegs(), \
			BlueRegistration::GetEnumRegs(), \
			BlueRegistration::GetTestRegs(), \
			BlueRegistration::GetThunkerRegs(), \
			BlueRegistration::GetFuncSignatures() ); \
		BlueRegisterObjectsToModule( module, BlueRegistration::GetObjectRegs() ); \
		BlueRegisterExceptionsToModule( module, BlueRegistration::GetExceptionRegs() ); \
		PyModule_AddObject( module, "BlueWrapper", (PyObject*)BePyTypePtr ); \
		return module; \
	}

#define BLUE_DECLARE_EXCEPTION_EX( name, ... ) __VA_ARGS__ PyObject* CCP_CONCATENATE( BlueGetException, name )();
#define BLUE_DECLARE_EXCEPTION( name ) BLUE_DECLARE_EXCEPTION_EX( name )

#define BLUE_GET_EXCEPTION( name ) ( CCP_CONCATENATE( BlueGetException, name )() )

#define BLUE_DEFINE_EXCEPTION( name, parent ) \
	PyObject* CCP_CONCATENATE( BlueGetException, name )() \
	{ \
		static auto s_exception = PyErr_NewException( const_cast<char*>( ( std::string( g_moduleName ) + "." #name ).c_str() ), BLUE_GET_EXCEPTION( parent ), nullptr ); \
		return s_exception; \
	} \
	BLUE_REGISTER_EXCEPTION( name, CCP_CONCATENATE( BlueGetException, name ) )

#else

#define BLUE_PY_EXPOSE_TO_BLUE_EXTRA
#define BLUE_PY_BEGIN_EXTRA
#define BLUE_PY_END_EXTRA
#define BLUE_PY_ADD_METHOD_AND_WRAP( name, functionName, docString )
#define BLUE_PY_ADD_METHOD_AND_WRAP_OPTIONAL_ARGS( name, functionName, numOptional, docString )
#define BLUE_PY_PROPERTY_GETTER( getterFunction ) nullptr
#define BLUE_PY_PROPERTY_SETTER( setterFunction ) nullptr
#define BLUE_PY_MAP_FUNCTION_AND_WRAP( _expname, _func, _docstring )

// Methods taking raw Python arguments are not exposed without Python
#define MAP_METHOD( nameString, pyFunc, docString )
#define MAP_METHOD_AS_METHOD( nameString, pyFunc, docString )
#define MAP_METHOD_WITH_KEYWORD_ARGUMENTS( nameString, pyFunc, docString )

// Without Python an exception is just its name (ScriptExceptionGetter, BeGetException)
#define BLUE_DECLARE_EXCEPTION_EX( name, ... ) __VA_ARGS__ const char* CCP_CONCATENATE( BlueGetException, name )();
#define BLUE_DECLARE_EXCEPTION( name ) BLUE_DECLARE_EXCEPTION_EX( name )

#define BLUE_GET_EXCEPTION( name ) ( CCP_CONCATENATE( BlueGetException, name )() )

#define BLUE_DEFINE_EXCEPTION( name, parent ) \
	const char* CCP_CONCATENATE( BlueGetException, name )() \
	{ \
		return #name; \
	} \
	BLUE_REGISTER_EXCEPTION( name, CCP_CONCATENATE( BlueGetException, name ) )

#endif // BLUE_WITH_PYTHON
