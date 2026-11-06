// Copyright (c) 2026 CCP Games
//
// Test classes for the Invoke hook (tests/invoke). Exposed with the real exposure macros; the
// same sources build three DSOs: hooked, unhooked (BLUE_INVOKE_DISABLE=1) and with another build
// ID. INVOKE_TEST_MODULE names each DSO's module, so their class paths differ.

#pragma once
#ifndef InvokeTestClasses_h
#define InvokeTestClasses_h

#include "BlueExposure.h"

#include <string>
#include <vector>

// Forward-declared as engine headers do (BLUE_DECLARE), completed only at the end of
// InvokeTestClasses.cpp (after ExposeToBlue): the hook sees it incomplete while the Python path,
// instantiated at the end of the TU, sees it complete.
BLUE_DECLARE( InvokeFwd );

BLUE_DECLARE( InvokeTestDerived );

BLUE_CLASS( InvokeTestObj ) : public IRoot
{
public:
	EXPOSE_TO_BLUE();

	enum Mode
	{
		MODE_A = 0,
		MODE_B = 5,
		MODE_C = 9,
	};
	enum Other
	{
		OTHER_X = 1,
		OTHER_Y = 2,
	};

	InvokeTestObj( IRoot* lockobj = nullptr );

	// T1 round trips
	bool EchoBool( bool v ) { return v; }
	int EchoInt( int v ) { return v; }
	uint32_t EchoUInt( uint32_t v ) { return v; }
	int64_t EchoInt64( int64_t v ) { return v; }
	uint64_t EchoUInt64( uint64_t v ) { return v; }
	size_t EchoSize( size_t v ) { return v; }
	float EchoFloat( float v ) { return v; }
	double EchoDouble( double v ) { return v; }
	const char* EchoCStr( const char* v );
	std::string EchoString( const std::string& v ) { return v; }
	const wchar_t* EchoWCStr( const wchar_t* v );
	std::wstring EchoWString( const std::wstring& v ) { return v; }
	Vector2 EchoVector2( const Vector2& v ) { return v; }
	Vector3 EchoVector3( const Vector3& v ) { return v; }
	Vector4 EchoVector4( const Vector4& v ) { return v; }
	Color EchoColor( const Color& v ) { return v; }
	Quaternion EchoQuaternion( const Quaternion& v ) { return v; }
	Matrix EchoMatrix( const Matrix& v ) { return v; }
	Mode EchoMode( Mode v ) { return v; }
	int TakeMode( Mode v ) { return int( v ); }
	int TakeOther( Other v ) { return int( v ); }
	InvokeTestObj* EchoObj( InvokeTestObj* v ) { return v; }
	IRoot* EchoRoot( IRoot* v ) { return v; }
	IRootPtr GetSelfRootPtr();
	InvokeTestObjPtr GetSelfPtr();
	int IsDerived( InvokeTestDerived* d ) { return d ? 1 : 0; }

	// optional arguments
	int Sum3( int a, int b, int c ) { return a + b + c; }
	int OptBox( Be::Optional<int> v ) { return v.IsAssigned() ? v.GetValue() : -1; }
	int OptDefault( Be::OptionalWithDefaultValue<int, 7> v ) { return v.GetValue(); }

	// Be::Result
	BlueStdResult Fail( int code );
	BlueStdResult MakeChild( int tag, InvokeTestObj** out );
	BlueStdResult GetCount( int& out );
	BlueStdResult SetCount( int v );
	int GetTag() const { return m_tag; }

	// a non-Be::Result T& is a by-value input
	void Scale( float& v );
	float GetLastFloat() const { return m_lastFloat; }

	// exceptions
	int Throw( int v );

	// unsupported: Python-typed, v1-unsupported types, forward-declared types
	PyObject* PyEcho( PyObject* v );
	Vector3d EchoVector3d( const Vector3d& v ) { return v; }
	int ListSize( const std::vector<int>& v ) { return int( v.size() ); }
	int TakeFwd( InvokeFwd* p ) { return p ? 1 : 0; }
	BlueStdResult GetFwd( InvokeFwd** out );
	BlueStdResult GetFwdRef( InvokeFwd*& out );

	// properties
	float GetScale() const { return m_scale; }
	void SetScale( float v ) { m_scale = v; }
	int GetCounter() const { return m_counter; }
	BlueStdResult GetName( std::string& out );
	BlueStdResult SetName( const std::string& v );
	Mode GetModeProp() const { return m_mode; }
	void SetModeProp( Mode v ) { m_mode = v; }
	PyObject* GetPyProp();

	int m_tag = 0;
	int m_count = 0;
	int m_counter = 3;
	float m_lastFloat = 0.0f;
	float m_scale = 1.0f;
	Mode m_mode = MODE_A;
	std::string m_name;
	std::string m_cstr;
	std::wstring m_wcstr;
};

TYPEDEF_BLUECLASS( InvokeTestObj );

BLUE_CLASS( InvokeTestDerived ) : public InvokeTestObj
{
public:
	EXPOSE_TO_BLUE();
	InvokeTestDerived( IRoot* lockobj = nullptr ) : InvokeTestObj( lockobj ) {}
	int DerivedOnly( int v ) { return v + 1000; }
};

TYPEDEF_BLUECLASS( InvokeTestDerived );

#endif // InvokeTestClasses_h
