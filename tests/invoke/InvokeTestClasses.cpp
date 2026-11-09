// Copyright (c) 2026 CCP Games
//
// Implementation and exposure of the Invoke hook's test classes, plus a few extern "C" probes the
// test driver dlsym()s. Exposure runs at static initialisation (ClassRegistrar), so the driver
// initialises Python before dlopen()ing this library.

#ifndef INVOKE_TEST_MODULE
#error "INVOKE_TEST_MODULE must name this DSO's module"
#endif

const char* g_moduleName = INVOKE_TEST_MODULE;

#include "InvokeTestClasses.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <typeinfo>

#ifdef _WIN32
#define INVOKE_TEST_EXPORT extern "C" __declspec( dllexport )
#else
#define INVOKE_TEST_EXPORT extern "C" __attribute__( ( visibility( "default" ) ) )
#endif

BLUE_DEFINE( InvokeTestObj );
BLUE_DEFINE( InvokeTestDerived );

InvokeTestObj::InvokeTestObj( IRoot* /*lockobj*/ )
{
}

const char* InvokeTestObj::EchoCStr( const char* v )
{
	m_cstr = v ? v : "";
	return m_cstr.c_str();
}

const wchar_t* InvokeTestObj::EchoWCStr( const wchar_t* v )
{
	m_wcstr = v ? v : L"";
	return m_wcstr.c_str();
}

IRootPtr InvokeTestObj::GetSelfRootPtr()
{
	IRootPtr p;
	p = static_cast<IRoot*>( this );
	return p;
}

InvokeTestObjPtr InvokeTestObj::GetSelfPtr()
{
	InvokeTestObjPtr p;
	p = this;
	return p;
}

BlueStdResult InvokeTestObj::Fail( int code )
{
	if( code == 0 )
	{
		return BlueStdResult();
	}
	char buf[32];
	std::snprintf( buf, sizeof( buf ), "fail %d", code );
	return BlueStdResult( BLUE_STD_RESULT_VALUE_ERROR, buf );
}

BlueStdResult InvokeTestObj::MakeChild( int tag, InvokeTestObj** out )
{
	if( tag < 0 )
	{
		return BlueStdResult( BLUE_STD_RESULT_VALUE_ERROR, "negative tag" );
	}
	OInvokeTestObj* child = new OInvokeTestObj; // one lock, transferred to the caller
	child->m_tag = tag;
	*out = child;
	return BlueStdResult();
}

BlueStdResult InvokeTestObj::GetCount( int& out )
{
	out = m_count;
	return BlueStdResult();
}

BlueStdResult InvokeTestObj::SetCount( int v )
{
	m_count = v;
	return BlueStdResult();
}

void InvokeTestObj::Scale( float& v )
{
	v *= 2.0f;
	m_lastFloat = v;
}

int InvokeTestObj::Throw( int v )
{
	throw std::runtime_error( "boom " + std::to_string( v ) );
}

PyObject* InvokeTestObj::PyEcho( PyObject* v )
{
	Py_XINCREF( v );
	return v;
}

BlueStdResult InvokeTestObj::GetFwd( InvokeFwd** out )
{
	*out = nullptr;
	return BlueStdResult();
}

BlueStdResult InvokeTestObj::GetFwdRef( InvokeFwd*& out )
{
	out = nullptr;
	return BlueStdResult();
}

BlueStdResult InvokeTestObj::GetName( std::string& out )
{
	out = m_name;
	return BlueStdResult();
}

BlueStdResult InvokeTestObj::SetName( const std::string& v )
{
	if( v.empty() )
	{
		return BlueStdResult( BLUE_STD_RESULT_VALUE_ERROR, "name must not be empty" );
	}
	m_name = v;
	return BlueStdResult();
}

PyObject* InvokeTestObj::GetPyProp()
{
	Py_RETURN_NONE;
}

Be::VarChooser InvokeTestModeChooser[] = {
	{ "MODE_A", { BeCast( InvokeTestObj::MODE_A ) }, "A" },
	{ "MODE_B", { BeCast( InvokeTestObj::MODE_B ) }, "B" },
	{ "MODE_C", { BeCast( InvokeTestObj::MODE_C ) }, "C" },
	{ 0 },
};

BLUE_REGISTER_ENUM( INVOKE_TEST_MODULE "_MODE", InvokeTestObj::Mode, InvokeTestModeChooser );

// No canonical name declared for Level: its table's typeName is "InvokeTestObj::Level" as written here.
Be::VarChooser InvokeTestLevelChooser[] = {
	{ "LEVEL_LOW", { BeCast( InvokeTestObj::LEVEL_LOW ) }, "low" },
	{ "LEVEL_HIGH", { BeCast( InvokeTestObj::LEVEL_HIGH ) }, "high" },
	{ 0 },
};

BLUE_REGISTER_ENUM( INVOKE_TEST_MODULE "_LEVEL", InvokeTestObj::Level, InvokeTestLevelChooser );

const Be::ClassInfo* InvokeTestObj::ExposeToBlue()
{
	EXPOSURE_BEGIN( InvokeTestObj, "carbon-bind Invoke hook test class" )
		MAP_INTERFACE( InvokeTestObj )

		MAP_METHOD_AND_WRAP( "EchoBool", EchoBool, "" )
		MAP_METHOD_AND_WRAP( "EchoInt", EchoInt, "" )
		MAP_METHOD_AND_WRAP( "EchoUInt", EchoUInt, "" )
		MAP_METHOD_AND_WRAP( "EchoInt64", EchoInt64, "" )
		MAP_METHOD_AND_WRAP( "EchoUInt64", EchoUInt64, "" )
		MAP_METHOD_AND_WRAP( "EchoSize", EchoSize, "" )
		MAP_METHOD_AND_WRAP( "EchoFloat", EchoFloat, "" )
		MAP_METHOD_AND_WRAP( "EchoDouble", EchoDouble, "" )
		MAP_METHOD_AND_WRAP( "EchoCStr", EchoCStr, "" )
		MAP_METHOD_AND_WRAP( "EchoString", EchoString, "" )
		MAP_METHOD_AND_WRAP( "EchoWCStr", EchoWCStr, "" )
		MAP_METHOD_AND_WRAP( "EchoWString", EchoWString, "" )
		MAP_METHOD_AND_WRAP( "EchoVector2", EchoVector2, "" )
		MAP_METHOD_AND_WRAP( "EchoVector3", EchoVector3, "" )
		MAP_METHOD_AND_WRAP( "EchoVector4", EchoVector4, "" )
		MAP_METHOD_AND_WRAP( "EchoColor", EchoColor, "" )
		MAP_METHOD_AND_WRAP( "EchoQuaternion", EchoQuaternion, "" )
		MAP_METHOD_AND_WRAP( "EchoMatrix", EchoMatrix, "" )
		MAP_METHOD_AND_WRAP( "EchoMode", EchoMode, "" )
		MAP_METHOD_AND_WRAP( "TakeMode", TakeMode, "" )
		MAP_METHOD_AND_WRAP( "TakeOther", TakeOther, "" )
		MAP_METHOD_AND_WRAP( "EchoObj", EchoObj, "" )
		MAP_METHOD_AND_WRAP( "EchoRoot", EchoRoot, "" )
		MAP_METHOD_AND_WRAP( "GetSelfRootPtr", GetSelfRootPtr, "" )
		MAP_METHOD_AND_WRAP( "GetSelfPtr", GetSelfPtr, "" )
		MAP_METHOD_AND_WRAP( "IsDerived", IsDerived, "" )

		MAP_METHOD_AND_WRAP_OPTIONAL_ARGS( "Sum3", Sum3, 2, "" )
		MAP_METHOD_AND_WRAP_OPTIONAL_ARGS( "OptBox", OptBox, 1, "" )
		MAP_METHOD_AND_WRAP_OPTIONAL_ARGS( "OptDefault", OptDefault, 1, "" )

		MAP_METHOD_AND_WRAP( "Fail", Fail, "" )
		MAP_METHOD_AND_WRAP( "MakeChild", MakeChild, "" )
		MAP_METHOD_AND_WRAP( "GetCount", GetCount, "" )
		MAP_METHOD_AND_WRAP( "SetCount", SetCount, "" )
		MAP_METHOD_AND_WRAP( "GetTag", GetTag, "" )

		MAP_METHOD_AND_WRAP( "Scale", Scale, "" )
		MAP_METHOD_AND_WRAP( "GetLastFloat", GetLastFloat, "" )
		MAP_METHOD_AND_WRAP( "Throw", Throw, "" )

		MAP_METHOD_AND_WRAP( "PyEcho", PyEcho, "" )
		MAP_METHOD_AND_WRAP( "EchoVector3d", EchoVector3d, "" )
		MAP_METHOD_AND_WRAP( "ListSize", ListSize, "" )
		MAP_METHOD_AND_WRAP( "TakeFwd", TakeFwd, "" )
		MAP_METHOD_AND_WRAP( "GetFwd", GetFwd, "" )
		MAP_METHOD_AND_WRAP( "GetFwdRef", GetFwdRef, "" )

		MAP_PROPERTY( "scale", GetScale, SetScale, "" )
		MAP_PROPERTY_READONLY( "counter", GetCounter, "" )
		MAP_PROPERTY( "name", GetName, SetName, "" )
		MAP_PROPERTY_PERSISTED( "mode", GetModeProp, SetModeProp, "" )
		MAP_PROPERTY_READONLY( "pyProp", GetPyProp, "" )
	EXPOSURE_END()
}

const Be::ClassInfo* InvokeTestDerived::ExposeToBlue()
{
	EXPOSURE_BEGIN( InvokeTestDerived, "carbon-bind Invoke hook derived test class" )
		MAP_INTERFACE( InvokeTestDerived )
		MAP_METHOD_AND_WRAP( "DerivedOnly", DerivedOnly, "" )
	EXPOSURE_CHAINTO( InvokeTestObj )
}

//--------------------------------------------------------------------------------------------
// Probes for the driver
//--------------------------------------------------------------------------------------------

INVOKE_TEST_EXPORT IRoot* invoke_test_create( const char* which )
{
	if( std::strcmp( which, "obj" ) == 0 )
	{
		InvokeTestObj* o = new OInvokeTestObj;
		return reinterpret_cast<IRoot*>( o );
	}
	if( std::strcmp( which, "derived" ) == 0 )
	{
		InvokeTestObj* o = new OInvokeTestDerived;
		return reinterpret_cast<IRoot*>( o );
	}
	return nullptr;
}

INVOKE_TEST_EXPORT const std::type_info* invoke_test_mode_type()
{
	return &typeid( InvokeTestObj::Mode );
}

INVOKE_TEST_EXPORT const std::type_info* invoke_test_other_type()
{
	return &typeid( InvokeTestObj::Other );
}

#if !BLUE_INVOKE_DISABLE
// Entries are constant expressions when their initialiser is: these force constant evaluation of
// representative ones (scalar, enum with type_info, object with type name, optional, Be::Result
// out-parameter, unsupported). The hook's own `static const` entries use the same function.
constexpr BlueInvokeMethod kProbeInt = BlueInvokeMakeMethod<decltype( &InvokeTestObj::EchoInt ), &InvokeTestObj::EchoInt, 0>( "EchoInt" );
constexpr BlueInvokeMethod kProbeMode = BlueInvokeMakeMethod<decltype( &InvokeTestObj::TakeMode ), &InvokeTestObj::TakeMode, 0>( "TakeMode" );
constexpr BlueInvokeMethod kProbeOther = BlueInvokeMakeMethod<decltype( &InvokeTestObj::TakeOther ), &InvokeTestObj::TakeOther, 0>( "TakeOther" );
constexpr BlueInvokeMethod kProbeObj = BlueInvokeMakeMethod<decltype( &InvokeTestObj::EchoObj ), &InvokeTestObj::EchoObj, 0>( "EchoObj" );
constexpr BlueInvokeMethod kProbeOpt = BlueInvokeMakeMethod<decltype( &InvokeTestObj::OptDefault ), &InvokeTestObj::OptDefault, 1>( "OptDefault" );
constexpr BlueInvokeMethod kProbeChild = BlueInvokeMakeMethod<decltype( &InvokeTestObj::MakeChild ), &InvokeTestObj::MakeChild, 0>( "MakeChild" );
constexpr BlueInvokeMethod kProbePy = BlueInvokeMakeMethod<decltype( &InvokeTestObj::PyEcho ), &InvokeTestObj::PyEcho, 0>( "PyEcho" );
constexpr BlueInvokeProperty kProbeProp =
	BlueInvokeMakeProperty<InvokeTestObj, decltype( &InvokeTestObj::GetModeProp ), &InvokeTestObj::GetModeProp, decltype( &InvokeTestObj::SetModeProp ),
						   &InvokeTestObj::SetModeProp>( "mode" );

static_assert( kProbeInt.fn != nullptr && kProbeInt.arity == 1 && kProbeInt.returnTag == BIV_INT, "EchoInt entry" );
static_assert( kProbeMode.sigHash != kProbeOther.sigHash, "an enum parameter's type must change sigHash" );
static_assert( kProbeObj.argTags[0] == BIV_OBJECT && kProbeObj.argTypeNames[1] != nullptr, "object entry" );
static_assert( kProbeOpt.numOptional == 1 && kProbeOpt.argTags[0] == BIV_INT, "optional entry" );
static_assert( kProbeChild.arity == 1 && ( kProbeChild.flags & BIM_OUT_OBJECT ) && kProbeChild.returnTag == BIV_OBJECT, "T** out-parameter" );
static_assert( kProbePy.fn == nullptr, "Python-typed method must be unsupported" );
static_assert( kProbeProp.get != nullptr && kProbeProp.set != nullptr && kProbeProp.typeTag == BIV_ENUM, "enum property" );
static_assert( std::is_standard_layout<BlueInvokeValue>::value && std::is_trivially_copyable<BlueInvokeMethod>::value &&
				   std::is_trivially_destructible<BlueInvokeProperty>::value,
			   "entries are PODs" );

// Golden values: canonical names are declared in the source, so names and sigHash are the same on
// every compiler. The literals were computed outside the compiler (FNV-1a 64 over the recipe in
// BlueInvokeFillArgs / BlueInvokeMakeProperty); a change here is a change of the hook's ABI.
constexpr bool InvokeTestStrEq( const char* a, const char* b )
{
	while( *a && *a == *b )
	{
		++a;
		++b;
	}
	return *a == *b;
}
namespace InvokeTestNs
{
enum Kind
{
	KIND_A = 0,
};
BLUE_INVOKE_CANONICAL_NAME( Kind, "InvokeTestNs::Kind" );
} // namespace InvokeTestNs
struct InvokeTestUnnamed : InvokeTestObj
{
};
static_assert( InvokeTestStrEq( BlueInvokeCanonicalName<InvokeTestObj>::value, "InvokeTestObj" ), "class name (BLUE_CLASS)" );
static_assert( InvokeTestStrEq( BlueInvokeCanonicalName<InvokeTestDerived>::value, "InvokeTestDerived" ), "class name (BLUE_DECLARE)" );
static_assert( InvokeTestStrEq( BlueInvokeCanonicalName<IRoot>::value, "IRoot" ), "interface name" );
static_assert( InvokeTestStrEq( BlueInvokeCanonicalName<InvokeTestObj::Mode>::value, "InvokeTestObj::Mode" ), "nested enum name" );
static_assert( BlueInvokeCanonicalName<InvokeTestObj::Mode>::hash == 0xa5486c3686196a8dull, "FNV-1a of the enum name" );
static_assert( BlueInvokeCanonicalName<InvokeTestObj>::hash == 0x1deeb1828823e092ull, "FNV-1a of the class name" );
static_assert( !BlueInvokeHasCanonicalName<InvokeTestObj::Level>(), "Level declares no name" );
static_assert( InvokeTestStrEq( BlueInvokeCanonicalName<InvokeTestNs::Kind>::value, "InvokeTestNs::Kind" ) &&
				   BlueInvokeCanonicalName<InvokeTestNs::Kind>::hash == 0xa1269286b9f3d3f4ull,
			   "namespaced enum name, declared inside its namespace (found through the enum's namespace)" );
static_assert( !BlueInvokeHasCanonicalName<InvokeTestUnnamed>(), "a class derived from a named one does not take its name" );
static_assert( kProbeInt.sigHash == 0x8f3c0b19fc075764ull, "sigHash int(int)" );
static_assert( kProbeMode.sigHash == 0x9d5a1332386f4df7ull, "sigHash int(InvokeTestObj::Mode)" );
static_assert( kProbeOther.sigHash == 0x285c68c65cbb9e00ull, "sigHash int(InvokeTestObj::Other)" );
static_assert( kProbeObj.sigHash == 0x775d63be6c66ff3cull, "sigHash InvokeTestObj*(InvokeTestObj*)" );
static_assert( kProbeProp.sigHash == 0xd67d14147623fc1cull, "sigHash of property InvokeTestObj::Mode" );
static_assert( InvokeTestStrEq( kProbeMode.argTypeNames[1], "InvokeTestObj::Mode" ) && InvokeTestStrEq( kProbeObj.argTypeNames[0], "InvokeTestObj" ) &&
				   InvokeTestStrEq( kProbeProp.typeName, "InvokeTestObj::Mode" ),
			   "entries report canonical names" );
#endif

// Conversions that the exposure macros cannot reach (the Python path has no extractor for them).
INVOKE_TEST_EXPORT int invoke_test_direct( char* msg, size_t len )
{
	int fails = 0;
	msg[0] = '\0';
	auto check = [&]( bool ok, const char* what ) {
		if( !ok )
		{
			++fails;
			const size_t used = std::strlen( msg );
			std::snprintf( msg + used, len - used, "%s; ", what );
		}
	};
	BlueInvokeError e{};
	BlueInvokeValue v{};
	BlueInvokeValue r{};

	// BlueSharedString in/out
	v.tag = BIV_STRING;
	v.v.s.p = "pooled";
	v.v.s.n = 6;
	BlueSharedString ss;
	check( BlueInvokeConv<BlueSharedString>::In( v, ss, &e, 1 ) == BIS_OK && std::strcmp( ss.c_str(), "pooled" ) == 0, "BlueSharedString In" );
	BlueInvokeConv<BlueSharedString>::Out( ss, &r );
	check( r.tag == BIV_STRING && r.v.s.n == 6 && std::strcmp( r.v.s.p, "pooled" ) == 0 && r.release, "BlueSharedString Out" );
	BlueInvokeValueClear( &r );

	// BluePtr<T> argument: holds one lock while stored
	InvokeTestObj* o = new OInvokeTestObj;
	IRoot* root = reinterpret_cast<IRoot*>( o );
	const int before = root->GetRefCount();
	v = BlueInvokeValue{};
	v.tag = BIV_OBJECT;
	v.v.obj = root;
	{
		InvokeTestObjPtr p;
		check( BlueInvokeConv<InvokeTestObjPtr>::In( v, p, &e, 1 ) == BIS_OK && static_cast<InvokeTestObj*>( p ) == o, "BluePtr In" );
		check( root->GetRefCount() == before + 1, "BluePtr In holds a lock" );
	}
	check( root->GetRefCount() == before, "BluePtr releases its lock" );

	// invalid UTF-8 into a wide string
	v = BlueInvokeValue{};
	v.tag = BIV_STRING;
	v.v.s.p = "\xff\xfe";
	v.v.s.n = 2;
	std::wstring w;
	check( BlueInvokeConv<std::wstring>::In( v, w, &e, 1 ) == BIS_TYPE, "invalid UTF-8 rejected" );

	// wrong FLOATS count
	v = BlueInvokeValue{};
	v.tag = BIV_FLOATS;
	v.count = 2;
	Vector3 v3;
	check( BlueInvokeConv<Vector3>::In( v, v3, &e, 1 ) == BIS_TYPE, "Vector3 needs 3 floats" );

	root->Unlock();
	return fails;
}

// Completed only now: ExposeToBlue above saw InvokeFwd incomplete.
class InvokeFwd : public IRoot
{
};
BLUE_DEFINE_INTERFACE_IMPL( InvokeFwd );
