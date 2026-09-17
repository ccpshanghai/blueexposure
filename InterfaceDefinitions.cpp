////////////////////////////////////////////////////////////////////////////////
//
// Creator:		Snorri Sturluson
// Created:		January 2013
// Copyright (c) 2026 CCP Games
//

#include "include/BlueExposureMacros.h"
#include "include/IList.h"
#include "include/IPythonMethods.h"
#include "include/IPythonNumeric.h"

BLUE_DEFINE_INTERFACE_IMPL_EXPORT( IRoot );
BLUE_DEFINE_INTERFACE_EXPORT( INotify );
BLUE_DEFINE_INTERFACE_EXPORT( IInitialize );
BLUE_DEFINE_INTERFACE_EXPORT( IBlueClasses );
BLUE_DEFINE_INTERFACE_EXPORT( IList );
BLUE_DEFINE_INTERFACE_EXPORT( IBlueDict );
BLUE_DEFINE_INTERFACE_EXPORT( IBlueStructureList );
BLUE_DEFINE_INTERFACE_EXPORT( IListNotify );
BLUE_DEFINE_INTERFACE_EXPORT( IWeakObject );
BLUE_DEFINE_INTERFACE_EXPORT( IPythonMethods );
BLUE_DEFINE_INTERFACE_EXPORT( IPythonNumeric );
BLUE_DEFINE_INTERFACE_EXPORT( ICopier );
BLUE_DEFINE_INTERFACE_EXPORT( ICopierCustomAssignment );
BLUE_DEFINE_INTERFACE_EXPORT( ICustomPersist );

// The key functions of the interfaces blue dynamic_casts to across shared objects; see
// BLUE_INTERFACE_ANCHOR in BlueTypes.h.
BLUE_DEFINE_INTERFACE_ANCHOR( IList );
BLUE_DEFINE_INTERFACE_ANCHOR( IPythonMethods );
BLUE_DEFINE_INTERFACE_ANCHOR( IPythonNumeric );

BLUE_DEFINE_CLSID_EXPORT( "blue", List );
BLUE_DEFINE_CLSID_EXPORT( "blue", Dict );
BLUE_DEFINE_CLSID_EXPORT( "blue", StructureList );