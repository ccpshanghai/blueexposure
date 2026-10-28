// Copyright (c) 2026 CCP Games
//
// C-ABI backend of the exposure macros: the BLUE_CABI_* fragments that the common macros in
// BlueExposureMacros.h expand. Included from BlueExposureMacros.h; with BLUE_WITH_CABI off
// every fragment expands to nothing.
//
// The thunks they register are in BlueExposureHelpersCAbi.h.

#pragma once

#if BLUE_WITH_CABI

// EXPOSURE_BEGIN_IMP: the class's C method table
#define BLUE_CABI_BEGIN_EXTRA \
	static std::vector<BlueCMethodDef> s_cmethods;

// EXPOSURE_END_IMPL: terminate the C method table and hook it into _classinfo
#define BLUE_CABI_END_EXTRA \
	{ \
		BlueCMethodDef cmethodsEnd = { nullptr, nullptr, nullptr, 0 }; \
		s_cmethods.push_back( cmethodsEnd ); \
	} \
	_classinfo.mCMethodTable = &s_cmethods[0];

// MAP_METHOD_AND_WRAP / MAP_METHOD_AND_WRAP_OPTIONAL_ARGS: add a C thunk to the method table
#define BLUE_CABI_ADD_METHOD_AND_WRAP( name, functionName, docString ) \
{\
	BlueCMethodDef d = { name, &BlueCMethodHelper<decltype(&_Class::functionName), &_Class::functionName>, docString, 0 };\
	s_cmethods.push_back( d );\
}

#define BLUE_CABI_ADD_METHOD_AND_WRAP_OPTIONAL_ARGS( name, functionName, numOptional, docString ) \
{\
	BlueCMethodDef d = { name, &BlueCMethodHelperWithOptionalArgs<decltype(&_Class::functionName), &_Class::functionName, numOptional>, docString, numOptional };\
	s_cmethods.push_back( d );\
}

// MAP_PROPERTY*: trailing VarEntry initialisers for the C getter and setter, each with a
// leading comma
#define BLUE_CABI_PROPERTY_FIELDS( getterFunction, setterFunction ) \
	, &BlueCPropertyGetter<_Class, decltype(&_Class::getterFunction), &_Class::getterFunction> \
	, &BlueCPropertySetter<_Class, decltype(&_Class::setterFunction), &_Class::setterFunction>

#define BLUE_CABI_PROPERTY_FIELDS_READONLY( getterFunction ) \
	, &BlueCPropertyGetter<_Class, decltype(&_Class::getterFunction), &_Class::getterFunction> \
	, nullptr

// Every other brace-initialised VarEntry (attributes): no C getter or setter
#define BLUE_CABI_NO_PROPERTY_FIELDS \
	, nullptr, nullptr

// MAP_FUNCTION_AND_WRAP: register a C thunk for a module function; a declaration ending in ';'
#define BLUE_CABI_MAP_FUNCTION_AND_WRAP( _expname, _func, _docstring ) \
	static CFunctionRegistrar s_##_func##CFunctionRegistrar( _expname, &BlueCFunctionHelper<decltype(&_func), &_func>, _docstring );

#else

#define BLUE_CABI_BEGIN_EXTRA
#define BLUE_CABI_END_EXTRA
#define BLUE_CABI_ADD_METHOD_AND_WRAP( name, functionName, docString )
#define BLUE_CABI_ADD_METHOD_AND_WRAP_OPTIONAL_ARGS( name, functionName, numOptional, docString )
#define BLUE_CABI_PROPERTY_FIELDS( getterFunction, setterFunction )
#define BLUE_CABI_PROPERTY_FIELDS_READONLY( getterFunction )
#define BLUE_CABI_NO_PROPERTY_FIELDS
#define BLUE_CABI_MAP_FUNCTION_AND_WRAP( _expname, _func, _docstring )

#endif // BLUE_WITH_CABI
