// Copyright (c) 2026 CCP Games
//
// C-ABI backend of the exposure macros: the BLUE_CABI_* fragments that the common macros in
// BlueExposureMacros.h expand. Included from BlueExposureMacros.h.
//
// The C thunks do not exist yet, so every fragment expands to nothing, whether or not
// BLUE_WITH_CABI is set.

#pragma once

// EXPOSURE_BEGIN_IMP: statics for the class's C method table
#define BLUE_CABI_BEGIN_EXTRA

// EXPOSURE_END_IMPL: hook the C method table into _classinfo
#define BLUE_CABI_END_EXTRA

// MAP_METHOD_AND_WRAP / MAP_METHOD_AND_WRAP_OPTIONAL_ARGS: add a C thunk to the method table
#define BLUE_CABI_ADD_METHOD_AND_WRAP( name, functionName, docString )
#define BLUE_CABI_ADD_METHOD_AND_WRAP_OPTIONAL_ARGS( name, functionName, numOptional, docString )

// MAP_PROPERTY*: trailing VarEntry initialisers for the C getter and setter, each with a
// leading comma
#define BLUE_CABI_PROPERTY_FIELDS( getterFunction, setterFunction )
#define BLUE_CABI_PROPERTY_FIELDS_READONLY( getterFunction )

// MAP_FUNCTION_AND_WRAP: register a C thunk for a module function; a declaration ending in ';'
#define BLUE_CABI_MAP_FUNCTION_AND_WRAP( _expname, _func, _docstring )
