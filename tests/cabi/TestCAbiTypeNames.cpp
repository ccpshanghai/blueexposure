// Copyright (c) 2026 CCP Games
//
// C type names (BlueCAbiTypeName.h) and what the exposure macros record with them: the C
// signatures of methods (ClassInfo::mCSignatures) and module functions
// (BlueRegistration::GetCFuncSignatures), and the type names of members (VarEntry::mCTypeName,
// BlueCMemberTypeName).

#include "TestCAbiUtil.h"
#include "TestAttributes.h"
#include "TestCallbacks.h"
#include "TestMethods.h"
#include "TestMultipleInterfaces.h"
#include "TestOptionalArgs.h"
#include "TestProperties.h"
#include "TestReferences.h"

#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace CAbiTest;

// No C-ABI conversion
struct TestCAbiNoConversion
{
};

enum TestCAbiSmallEnum : uint8_t
{
	TEST_CABI_SMALL,
};

enum class TestCAbiWideEnum : int64_t
{
	WIDE = 1,
};

namespace
{

template< typename T >
std::string NameOf()
{
	const char* name = BlueCTypeName<T>::Get();
	EXPECT_NE( name, nullptr );
	return name ? name : "";
}

bool StartsWithUnknown( const std::string& name )
{
	return name.compare( 0, 8, "unknown:" ) == 0;
}

// The C signature of method name of type (own table only), or null
const BlueCSignature* SignatureOf( const Be::ClassInfo* type, const char* name )
{
	for( size_t i = 0; type->mCMethodTable[i].name; ++i )
	{
		if( strcmp( type->mCMethodTable[i].name, name ) == 0 )
		{
			return &type->mCSignatures[i];
		}
	}
	return nullptr;
}

// "returns(arg,arg) opt=N", for comparing a whole signature at once
std::string Describe( const BlueCSignature* signature )
{
	if( !signature )
	{
		return "<none>";
	}
	std::string result = signature->returns ? signature->returns : "<null>";
	result += "(";
	for( uint32_t i = 0; i < signature->argCount; ++i )
	{
		result += i ? "," : "";
		result += signature->args[i] ? signature->args[i] : "<null>";
	}
	result += ") opt=" + std::to_string( signature->optionalCount );
	return result;
}

std::string MethodSignature( const Be::ClassInfo* type, const char* name )
{
	return Describe( SignatureOf( type, name ) );
}

const Be::VarEntry* MemberOf( const Be::ClassInfo* type, const char* name )
{
	return BlueCFindMember( type, name, strlen( name ), nullptr );
}

std::string MemberType( const Be::ClassInfo* type, const char* name )
{
	const Be::VarEntry* entry = MemberOf( type, name );
	EXPECT_NE( entry, nullptr ) << name;
	return entry ? BlueCMemberTypeName( *entry ) : "<none>";
}

} // namespace


//--------------------------------------------------------------------
// Names of types
//--------------------------------------------------------------------

TEST( CAbiTypeNames, Scalars )
{
	EXPECT_EQ( NameOf<void>(), "void" );
	EXPECT_EQ( NameOf<bool>(), "bool" );

	EXPECT_EQ( NameOf<int8_t>(), "int8" );
	EXPECT_EQ( NameOf<int16_t>(), "int16" );
	EXPECT_EQ( NameOf<int32_t>(), "int32" );
	EXPECT_EQ( NameOf<int64_t>(), "int64" );
	EXPECT_EQ( NameOf<uint8_t>(), "uint8" );
	EXPECT_EQ( NameOf<uint16_t>(), "uint16" );
	EXPECT_EQ( NameOf<uint32_t>(), "uint32" );
	EXPECT_EQ( NameOf<uint64_t>(), "uint64" );
	EXPECT_EQ( NameOf<signed char>(), "int8" );
	EXPECT_EQ( NameOf<short>(), "int16" );
	EXPECT_EQ( NameOf<long long>(), "int64" );
	EXPECT_EQ( NameOf<unsigned long long>(), "uint64" );
	EXPECT_EQ( NameOf<long>(), sizeof( long ) == 8 ? "int64" : "int32" );
	EXPECT_EQ( NameOf<size_t>(), sizeof( size_t ) == 8 ? "uint64" : "uint32" );
	EXPECT_EQ( NameOf<ptrdiff_t>(), sizeof( ptrdiff_t ) == 8 ? "int64" : "int32" );

	EXPECT_EQ( NameOf<float>(), "float" );
	EXPECT_EQ( NameOf<double>(), "double" );
}

TEST( CAbiTypeNames, EnumsAreSignedIntegersOfTheirSize )
{
	// BlueCConvert wraps every enum as INT, whatever the signedness of its underlying type
	EXPECT_EQ( NameOf<TestMethods::Enum>(), "int32" );
	EXPECT_EQ( NameOf<TestCAbiSmallEnum>(), "int8" );
	EXPECT_EQ( NameOf<TestCAbiWideEnum>(), "int64" );
}

TEST( CAbiTypeNames, Strings )
{
	EXPECT_EQ( NameOf<std::string>(), "string" );
	EXPECT_EQ( NameOf<const char*>(), "string" );
	EXPECT_EQ( NameOf<char*>(), "string" );
	EXPECT_EQ( NameOf<BlueCStringArg>(), "string" );
	EXPECT_EQ( NameOf<std::wstring>(), "string" );
	EXPECT_EQ( NameOf<const wchar_t*>(), "string" );
	EXPECT_EQ( NameOf<wchar_t*>(), "string" );
	EXPECT_EQ( NameOf<CastableStdWstring>(), "string" );
	EXPECT_EQ( NameOf<BlueSharedString>(), "string" );
	EXPECT_EQ( NameOf<BlueSharedStringW>(), "string" );
}

TEST( CAbiTypeNames, Vectors )
{
	EXPECT_EQ( NameOf<Vector2>(), "vector2" );
	EXPECT_EQ( NameOf<Vector3>(), "vector3" );
	EXPECT_EQ( NameOf<Vector4>(), "vector4" );
	EXPECT_EQ( NameOf<Vector2d>(), "vector2d" );
	EXPECT_EQ( NameOf<Vector3d>(), "vector3d" );
	EXPECT_EQ( NameOf<Vector4d>(), "vector4d" );
	EXPECT_EQ( NameOf<Vector3i>(), "vector3i" );
	EXPECT_EQ( NameOf<Color>(), "color" );
	EXPECT_EQ( NameOf<Quaternion>(), "quaternion" );
	EXPECT_EQ( NameOf<Matrix>(), "matrix" );
}

TEST( CAbiTypeNames, Objects )
{
	EXPECT_EQ( NameOf<IRoot*>(), "object:IRoot" );
	EXPECT_EQ( NameOf<const IRoot*>(), "object:IRoot" );
	EXPECT_EQ( NameOf<TestMethods*>(), "object:TestMethods" );
	EXPECT_EQ( NameOf<const TestMethods*>(), "object:TestMethods" );
	EXPECT_EQ( NameOf<ITestInterfaceOne*>(), "object:ITestInterfaceOne" );

	EXPECT_EQ( NameOf<IRootPtr>(), "object:IRoot" );
	EXPECT_EQ( NameOf<BluePtr<TestMultipleInterfaces>>(), "object:TestMultipleInterfaces" );
	EXPECT_EQ( NameOf<BlueBasicPtr<TestMethods>>(), "object:TestMethods" );
}

TEST( CAbiTypeNames, Callback )
{
#if BLUE_CABI_CALLBACKS
	EXPECT_EQ( NameOf<BlueScriptCallback>(), "callback" );
	EXPECT_TRUE( BlueCIsConvertible<BlueScriptCallback>::value );
#else
	// With both backends the callback is the Python one, without a C conversion
	EXPECT_EQ( NameOf<BlueScriptCallback>(), "unknown:BlueScriptCallback" );
	EXPECT_FALSE( BlueCIsConvertible<BlueScriptCallback>::value );
#endif
}

TEST( CAbiTypeNames, Containers )
{
	EXPECT_EQ( NameOf<std::vector<int>>(), "list<int32>" );
	EXPECT_EQ( NameOf<std::list<std::string>>(), "list<string>" );
	EXPECT_EQ( NameOf<std::vector<std::vector<uint8_t>>>(), "list<list<uint8>>" );
	EXPECT_EQ( NameOf<std::vector<const char*>>(), "list<string>" );
	EXPECT_EQ( ( NameOf<std::map<std::string, int>>() ), "map<string,int32>" );
	EXPECT_EQ( ( NameOf<std::map<std::wstring, std::vector<float>>>() ), "map<string,list<float>>" );
	EXPECT_EQ( ( NameOf<std::pair<bool, int64_t>>() ), "tuple<bool,int64>" );
	EXPECT_EQ( ( NameOf<std::vector<std::pair<int, Vector3>>>() ), "list<tuple<int32,vector3>>" );
	EXPECT_EQ( NameOf<Be::Optional<int>>(), "optional<int32>" );
	EXPECT_EQ( ( NameOf<Be::OptionalWithDefaultValue<int, 123>>() ), "optional<int32>" );
	EXPECT_EQ( NameOf<Be::Optional<std::vector<double>>>(), "optional<list<double>>" );
	EXPECT_EQ( NameOf<std::vector<BluePtr<TestMultipleInterfaces>>>(), "list<object:TestMultipleInterfaces>" );
	EXPECT_EQ( NameOf<std::vector<IRoot*>>(), "list<object:IRoot>" );
}

TEST( CAbiTypeNames, Unknown )
{
	EXPECT_EQ( NameOf<TestCAbiNoConversion>(), "unknown:TestCAbiNoConversion" );
	EXPECT_EQ( NameOf<void*>(), "unknown:void*" );
	EXPECT_EQ( NameOf<BlueWeakRef<TestMethods>>(), "unknown:BlueWeakRef<TestMethods>" );
	EXPECT_TRUE( StartsWithUnknown( NameOf<std::optional<int>>() ) ) << NameOf<std::optional<int>>();

	// Spaces only between two words
	EXPECT_EQ( NameOf<std::optional<unsigned int>>(), "unknown:std::optional<unsigned int>" );
#if BLUE_WITH_PYTHON
	EXPECT_EQ( NameOf<PyObject*>(), "unknown:_object*" );
#endif
}

TEST( CAbiTypeNames, UnknownIsDeep )
{
	// A container is unknown as a whole when an item type is: its name is the container's
	EXPECT_EQ( NameOf<std::vector<TestCAbiNoConversion>>(), "unknown:std::vector<TestCAbiNoConversion,std::allocator<TestCAbiNoConversion>>" );
	EXPECT_TRUE( StartsWithUnknown( NameOf<std::list<std::vector<TestCAbiNoConversion>>>() ) );
	EXPECT_TRUE( StartsWithUnknown( ( NameOf<std::map<std::string, TestCAbiNoConversion>>() ) ) );
	EXPECT_TRUE( StartsWithUnknown( ( NameOf<std::map<TestCAbiNoConversion, int>>() ) ) );
	EXPECT_TRUE( StartsWithUnknown( ( NameOf<std::pair<int, std::vector<TestCAbiNoConversion>>>() ) ) );
	EXPECT_TRUE( StartsWithUnknown( NameOf<Be::Optional<TestCAbiNoConversion>>() ) );
	EXPECT_TRUE( StartsWithUnknown( NameOf<std::vector<BlueWeakRef<TestMethods>>>() ) );

	EXPECT_FALSE( BlueCIsConvertible<std::vector<TestCAbiNoConversion>>::value );
	EXPECT_FALSE( ( BlueCIsConvertible<std::map<std::string, std::vector<TestCAbiNoConversion>>>::value ) );
	EXPECT_TRUE( ( BlueCIsConvertible<std::map<std::string, std::vector<int>>>::value ) );
}

TEST( CAbiTypeNames, Interned )
{
	// Built once, and equal names share one pointer
	EXPECT_EQ( BlueCTypeName<std::vector<int>>::Get(), BlueCTypeName<std::vector<int>>::Get() );
	EXPECT_EQ( BlueCTypeName<std::vector<int>>::Get(), BlueCTypeName<std::list<int>>::Get() );
	EXPECT_EQ( BlueCTypeName<IRootPtr>::Get(), BlueCTypeName<IRoot*>::Get() );
	EXPECT_EQ( BlueCInternTypeName( "list<int32>" ), BlueCTypeName<std::vector<int>>::Get() );
	EXPECT_EQ( BlueCObjectTypeName( nullptr ), BlueCTypeName<IRoot*>::Get() );

	EXPECT_TRUE( BlueCIsKnownTypeName( "list<int32>" ) );
	EXPECT_FALSE( BlueCIsKnownTypeName( "unknown:Foo" ) );
	EXPECT_FALSE( BlueCIsKnownTypeName( nullptr ) );
}


//--------------------------------------------------------------------
// The trait follows BlueCConvert
//--------------------------------------------------------------------

template< typename... T >
struct TypeList
{
};

// For a type that is not a container, a name exists exactly when a conversion does, and the
// name says whether it does
template< typename T >
void ExpectConsistent()
{
	const std::string name = NameOf<T>();
	EXPECT_EQ( BlueCIsConvertible<T>::value, BlueCIsSupported<T>::value ) << name;
	EXPECT_EQ( BlueCIsConvertible<T>::value, !StartsWithUnknown( name ) ) << name;
	EXPECT_EQ( BlueCIsKnownTypeName( name.c_str() ), BlueCIsConvertible<T>::value ) << name;
}

template< typename... T >
void ExpectConsistent( TypeList<T...> )
{
	( ExpectConsistent<T>(), ... );
}

TEST( CAbiTypeNames, MatchesConvert )
{
	ExpectConsistent( TypeList<
		bool, char, signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long,
		long long, unsigned long long, wchar_t, float, double, TestMethods::Enum, TestCAbiSmallEnum, TestCAbiWideEnum,
		std::string, const char*, char*, BlueCStringArg, std::wstring, const wchar_t*, wchar_t*, CastableStdWstring,
		BlueSharedString, BlueSharedStringW,
		Vector2, Vector3, Vector4, Vector2d, Vector3d, Vector4d, Vector3i, Color, Quaternion, Matrix,
		IRoot*, const IRoot*, TestMethods*, ITestInterfaceOne*, IRootPtr, BluePtr<TestMethods>, BlueBasicPtr<TestMethods>,
		BlueScriptCallback,
		TestCAbiNoConversion, void*, BlueWeakRef<TestMethods>, std::optional<int>, Be::Optional<int>
#if BLUE_WITH_PYTHON
		, PyObject*
#endif
		>() );

	// Containers: BlueCIsSupported is shallow, BlueCIsConvertible deep
	EXPECT_TRUE( BlueCIsSupported<std::vector<TestCAbiNoConversion>>::value );
	EXPECT_FALSE( BlueCIsConvertible<std::vector<TestCAbiNoConversion>>::value );
	ExpectConsistent( TypeList<
		std::vector<int>, std::list<std::string>, std::map<std::string, int>, std::pair<bool, double>,
		std::vector<std::vector<Vector3>>, std::vector<BluePtr<TestMethods>> >() );
}


//--------------------------------------------------------------------
// Recorded C signatures
//--------------------------------------------------------------------

TEST( CAbiSignatures, ParallelToTheMethodTable )
{
	// Every class's C signatures have an entry per C method and the same terminator
	size_t classes = 0;
	for( const Be::ClassRegistration& reg : BlueRegistration::GetClassRegs() )
	{
		const Be::ClassInfo* type = reg.mType;
		ASSERT_NE( type->mCMethodTable, nullptr ) << type->mClassId->GetName();
		ASSERT_NE( type->mCSignatures, nullptr ) << type->mClassId->GetName();
		size_t i = 0;
		for( ; type->mCMethodTable[i].name; ++i )
		{
			const BlueCSignature& signature = type->mCSignatures[i];
			EXPECT_NE( signature.returns, nullptr ) << type->mCMethodTable[i].name;
			EXPECT_NE( signature.args, nullptr ) << type->mCMethodTable[i].name;
			EXPECT_EQ( signature.optionalCount, type->mCMethodTable[i].optionalCount ) << type->mCMethodTable[i].name;
			for( uint32_t a = 0; a < signature.argCount; ++a )
			{
				EXPECT_NE( signature.args[a], nullptr ) << type->mCMethodTable[i].name << " argument " << a;
			}
		}
		EXPECT_EQ( type->mCSignatures[i].returns, nullptr );
		++classes;
	}
	EXPECT_GT( classes, size_t( 0 ) );
}

TEST( CAbiSignatures, Methods )
{
	const Be::ClassInfo* type = TestMethods::ClassType_();

	EXPECT_EQ( MethodSignature( type, "MethodReturningBool" ), "bool(bool) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningInt" ), "int32(int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningUnsignedInt" ), "uint32(uint32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningFloat" ), "float(float) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningDouble" ), "double(double) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningConstChar" ), "string(string) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningString" ), "string(string) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningConstWChar" ), "string(string) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningWString" ), "string(string) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningVector2" ), "vector2(vector2) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningVector3" ), "vector3(vector3) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodWithListOfVector3Parameter" ), "int32(list<vector3>) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodWithListOfListOfIntParameter" ), "int32(list<list<int32>>) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodWithMapOfStringToIntParameter" ), "int32(map<string,int32>) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningListOfVector3" ), "list<vector3>(int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningMapOfStringToInt" ), "map<string,int32>(int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodWithEnumParam" ), "int32(int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "MethodReturningBlueObject" ), "object:TestMultipleInterfaces(object:TestMultipleInterfaces) opt=0" );
	EXPECT_EQ( MethodSignature( type, "ConvertStringToSharedString" ), "string(string) opt=0" );
	EXPECT_EQ( MethodSignature( type, "LeakyFactoryFunction" ), "object:TestMultipleInterfaces() opt=0" );
#if BLUE_WITH_PYTHON
	EXPECT_EQ( MethodSignature( type, "MethodReturningPyObject" ), "unknown:_object*(unknown:_object*) opt=0" );
#endif
}

TEST( CAbiSignatures, ResultsAndArities )
{
	const Be::ClassInfo* type = TestMethods::ClassType_();

	// Be::Result alone returns NONE; a trailing X** or V& is the result, not an argument
	EXPECT_EQ( MethodSignature( type, "BeResultArity0" ), "void() opt=0" );
	EXPECT_EQ( MethodSignature( type, "BeResultArity2" ), "void(int32,int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "SafeFactoryFunction0" ), "object:TestMultipleInterfaces() opt=0" );
	EXPECT_EQ( MethodSignature( type, "SafeFactoryFunction2" ), "object:TestMultipleInterfaces(int32,int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "BeResultArity1WithRef" ), "int32() opt=0" );
	EXPECT_EQ( MethodSignature( type, "BeResultArity3WithRef" ), "int32(int32,int32) opt=0" );

	EXPECT_EQ( MethodSignature( type, "VoidArity0" ), "void() opt=0" );
	EXPECT_EQ( MethodSignature( type, "VoidArity1" ), "void(int32) opt=0" );
	EXPECT_EQ( MethodSignature( type, "IntArity9" ), "int32(int32,int32,int32,int32,int32,int32,int32,int32,int32) opt=0" );
	const BlueCSignature* none = SignatureOf( type, "VoidArity0" );
	ASSERT_NE( none, nullptr );
	EXPECT_EQ( none->args[0], nullptr );
}

TEST( CAbiSignatures, OptionalArguments )
{
	const Be::ClassInfo* type = TestOptionalArgs::ClassType_();

	EXPECT_EQ( MethodSignature( type, "MethodReturningBlueObject" ), "object:TestMultipleInterfaces(object:TestMultipleInterfaces) opt=1" );
	EXPECT_EQ( MethodSignature( type, "CalcSum" ), "int32(int32,int32,int32,int32,int32,int32,int32,int32,int32) opt=8" );
	// A Be::Optional parameter is named as such; that it may be omitted is the optional count
	EXPECT_EQ( MethodSignature( type, "BeOptional" ), "int32(optional<int32>) opt=1" );
	EXPECT_EQ( MethodSignature( type, "BeOptionalWithDefault" ), "int32(optional<int32>) opt=1" );
	EXPECT_EQ( MethodSignature( type, "BeOptionalIsSpecified" ), "bool(optional<int32>) opt=1" );
}

TEST( CAbiSignatures, Callbacks )
{
	const Be::ClassInfo* type = TestCallbacks::ClassType_();
#if BLUE_CABI_CALLBACKS
	EXPECT_EQ( MethodSignature( type, "GetCallback" ), "callback() opt=0" );
	EXPECT_EQ( MethodSignature( type, "SetCallback" ), "void(callback) opt=0" );
#else
	EXPECT_EQ( MethodSignature( type, "GetCallback" ), "unknown:BlueScriptCallback() opt=0" );
#endif
	EXPECT_EQ( MethodSignature( type, "CallCallbackInt" ), "tuple<bool,int32>() opt=0" );
	EXPECT_EQ( MethodSignature( type, "CallCallbackIntStringFloat" ), "tuple<bool,int32>(string,float) opt=0" );
}

TEST( CAbiSignatures, PythonSignaturesUnchanged )
{
	// The backend-neutral signatures keep their Python spelling next to the C ones
	const auto& signatures = *TestMethods::ClassType_()->mFunctionSignatures;
	ASSERT_EQ( signatures.count( "MethodReturningString" ), size_t( 1 ) );
	EXPECT_STREQ( signatures.at( "MethodReturningString" ).returnType, "str" );
	EXPECT_STREQ( signatures.at( "MethodReturningString" ).argumentTypes[0], "basestring" );
	EXPECT_STREQ( signatures.at( "IntArity3" ).returnType, "int" );
	EXPECT_EQ( signatures.at( "IntArity3" ).argumentCount, 3u );
	EXPECT_STREQ( BlueRegistration::GetFuncSignatures().at( "FunctionReturningInt" ).returnType, "int" );
}

TEST( CAbiSignatures, Functions )
{
	const CFuncSignatures& signatures = BlueRegistration::GetCFuncSignatures();

	// One per C function
	for( const BlueCMethodDef& def : BlueRegistration::GetCFuncRegs() )
	{
		EXPECT_EQ( signatures.count( def.name ), size_t( 1 ) ) << def.name;
	}
	EXPECT_EQ( signatures.size(), BlueRegistration::GetCFuncRegs().size() );

	EXPECT_EQ( Describe( &signatures.at( "SetReturnError" ) ), "void(bool) opt=0" );
	EXPECT_EQ( Describe( &signatures.at( "FunctionReturningInt" ) ), "int32(int32) opt=0" );
	EXPECT_EQ( Describe( &signatures.at( "FunctionReturningConstWChar" ) ), "string(string) opt=0" );
	EXPECT_EQ( Describe( &signatures.at( "FunctionReturningVector2d" ) ), "vector2d(vector2d) opt=0" );
	EXPECT_EQ( Describe( &signatures.at( "FunctionReturningVector3i" ) ), "vector3i(vector3i) opt=0" );
}


//--------------------------------------------------------------------
// Member type names
//--------------------------------------------------------------------

TEST( CAbiMemberTypeNames, Attributes )
{
	const Be::ClassInfo* type = TestAttributes::ClassType_();

	EXPECT_EQ( MemberType( type, "myString" ), "string" );
	EXPECT_EQ( MemberType( type, "myUnicode" ), "string" );
	EXPECT_EQ( MemberType( type, "myBool" ), "bool" );
	EXPECT_EQ( MemberType( type, "myInt" ), "int32" );
	EXPECT_EQ( MemberType( type, "myUInt" ), "uint32" );
	EXPECT_EQ( MemberType( type, "myFloat" ), "float" );
	EXPECT_EQ( MemberType( type, "myDouble" ), "double" );
	EXPECT_EQ( MemberType( type, "myInt64" ), "int64" );
	EXPECT_EQ( MemberType( type, "myUInt64" ), "uint64" );
	EXPECT_EQ( MemberType( type, "sharedString" ), "string" );
	EXPECT_EQ( MemberType( type, "sharedStringW" ), "string" );
	// An object embedded in this one, by the IID MAP_ATTRIBUTE recorded
	EXPECT_EQ( MemberType( type, "myVector" ), "object:IList" );

	// Attributes have no recorded name; theirs is derived
	EXPECT_EQ( MemberOf( type, "myInt" )->mCTypeName, nullptr );
}

TEST( CAbiMemberTypeNames, References )
{
	const Be::ClassInfo* type = TestReferences::ClassType_();
	const Be::VarEntry* weak = MemberOf( type, "weakRef" );
	ASSERT_NE( weak, nullptr );
	EXPECT_EQ( MemberType( type, "weakRef" ), std::string( "object:" ) + ( weak->mIID ? weak->mIID->GetName() : "IRoot" ) );
	// IRootPtr records no IID
	EXPECT_EQ( MemberType( type, "strongRef" ), "object:IRoot" );
}

TEST( CAbiMemberTypeNames, Properties )
{
	const Be::ClassInfo* type = TestProperties::ClassType_();

	EXPECT_EQ( MemberType( type, "myString" ), "string" );
	EXPECT_EQ( MemberType( type, "myUnicode" ), "string" );
	EXPECT_EQ( MemberType( type, "myBool" ), "bool" );
	EXPECT_EQ( MemberType( type, "myInt" ), "int32" );
	EXPECT_EQ( MemberType( type, "myUInt" ), "uint32" );
	EXPECT_EQ( MemberType( type, "myFloat" ), "float" );
	EXPECT_EQ( MemberType( type, "myDouble" ), "double" );
	EXPECT_EQ( MemberType( type, "myInt64" ), "int64" );
	EXPECT_EQ( MemberType( type, "myUInt64" ), "uint64" );
	// A getter returning Be::Result hands back its reference parameter
	EXPECT_EQ( MemberType( type, "myIntWithError" ), "int32" );
	EXPECT_EQ( MemberType( type, "myStringConst" ), "string" );
	EXPECT_EQ( MemberType( type, "myStringConstRef" ), "string" );
	EXPECT_EQ( MemberType( type, "sharedString" ), "string" );
	EXPECT_EQ( MemberType( type, "sharedStringW" ), "string" );
	// The attribute next to them
	EXPECT_EQ( MemberType( type, "returnError" ), "bool" );

	// Properties carry the name MAP_PROPERTY recorded
	const Be::VarEntry* entry = MemberOf( type, "myInt64" );
	ASSERT_NE( entry, nullptr );
	EXPECT_EQ( entry->mCTypeName, BlueCTypeName<int64_t>::Get() );
}

TEST( CAbiMemberTypeNames, Callbacks )
{
	const Be::ClassInfo* type = TestCallbacks::ClassType_();
#if BLUE_CABI_CALLBACKS
	EXPECT_EQ( MemberType( type, "callback" ), "callback" );
	EXPECT_EQ( MemberType( type, "callbackAttribute" ), "callback" );
#else
	EXPECT_EQ( MemberType( type, "callback" ), "unknown:BlueScriptCallback" );
	EXPECT_EQ( MemberType( type, "callbackAttribute" ), "unknown:SCRIPTCALLBACK" );
#endif
}

TEST( CAbiMemberTypeNames, ByVarType )
{
	// Attributes of VARTYPEs and sizes no test class has, as BlueCGetAttribute reads them
	struct Case
	{
		Be::VARTYPE type;
		size_t size;
		const Be::IID* iid;
		const char* expected;
	};
	const Case cases[] = {
		{ Be::LONG, 1, nullptr, "int8" },
		{ Be::LONG, 2, nullptr, "int16" },
		{ Be::LONG, 4, nullptr, "int32" },
		{ Be::SHORT, 2, nullptr, "uint16" },
		{ Be::BYTE, 1, nullptr, "uint8" },
		{ Be::CSTRING, 8, nullptr, "string" },
		{ Be::WCSTRING, 8, nullptr, "string" },
		{ Be::CHARARRAY, 8, nullptr, "string" },
		{ Be::FLOATARRAY, 8, nullptr, "vector2" },
		{ Be::FLOATARRAY, 12, nullptr, "vector3" },
		{ Be::FLOATARRAY, 16, nullptr, "vector4" },
		{ Be::FLOATARRAY, 16, &BlueColorIID, "color" },
		{ Be::FLOATARRAY, 16, &BlueRotationIID, "quaternion" },
		{ Be::FLOATARRAY, 64, &BlueMatrixIID, "matrix" },
		{ Be::FLOATARRAY, 64, nullptr, "list<float>" },
		{ Be::FLOATARRAY, 4, nullptr, "unknown:FLOATARRAY[1]" },
		{ Be::DOUBLEARRAY, 16, nullptr, "vector2d" },
		{ Be::DOUBLEARRAY, 24, nullptr, "vector3d" },
		{ Be::DOUBLEARRAY, 32, nullptr, "vector4d" },
		{ Be::DOUBLEARRAY, 40, nullptr, "list<double>" },
		{ Be::INTARRAY, 12, nullptr, "vector3i" },
		{ Be::INTARRAY, 8, nullptr, "unknown:INTARRAY[2]" },
		{ Be::INTARRAY, 20, nullptr, "list<int32>" },
		{ Be::IROOT, 8, nullptr, "object:IRoot" },
		{ Be::IROOTPTR, 8, &GetITestInterfaceOneIID(), "object:ITestInterfaceOne" },
		{ Be::IROOTWEAKREF, 8, &GetITestInterfaceOneIID(), "object:ITestInterfaceOne" },
		{ Be::PYOBJECT, 8, nullptr, "unknown:PYOBJECT" },
		{ Be::PYOBJECTPTR, 8, nullptr, "unknown:PYOBJECTPTR" },
		{ Be::REFERENCE, 8, nullptr, "unknown:REFERENCE" },
		{ Be::BINARYBLOCK, 0, nullptr, "unknown:BINARYBLOCK" },
		{ Be::VARTYPE( 99 ), 0, nullptr, "unknown:VARTYPE99" },
	};
	for( const Case& c : cases )
	{
		Be::VarEntry entry = {};
		entry.mName = "attribute";
		entry.mType = c.type;
		entry.mSize = c.size;
		entry.mIID = c.iid;
		entry.mEditFlags = Be::READ;
		EXPECT_STREQ( BlueCMemberTypeName( entry ), c.expected ) << "VARTYPE " << int( c.type ) << ", size " << c.size;
	}

	// A property without a C getter cannot be read through the C ABI
	Be::VarEntry pythonOnly = {};
	pythonOnly.mName = "property";
	pythonOnly.mType = Be::LONG;
	pythonOnly.mGetProperty = reinterpret_cast<BluePropertyGetterFunction>( 1 );
	EXPECT_STREQ( BlueCMemberTypeName( pythonOnly ), "unknown:LONG" );
}

TEST( CAbiMemberTypeNames, MatrixAttributes )
{
	// TestCAbiConversions (TestCAbiConversions.cpp): a Matrix with and without its IID
	const Be::ClassInfo* type = nullptr;
	for( const Be::ClassRegistration& reg : BlueRegistration::GetClassRegs() )
	{
		if( strcmp( reg.mType->mClassId->GetName(), "TestCAbiConversions" ) == 0 )
		{
			type = reg.mType;
		}
	}
	ASSERT_NE( type, nullptr );
	EXPECT_EQ( MemberType( type, "matrix" ), "matrix" );
	EXPECT_EQ( MemberType( type, "floats16" ), "list<float>" );
	EXPECT_EQ( MemberType( type, "vector" ), "vector3" );
	EXPECT_EQ( MemberType( type, "cstring" ), "string" );
	EXPECT_EQ( MemberType( type, "chars" ), "string" );
}

TEST( CAbiMemberTypeNames, SmartPointers )
{
	// TestCAbiSmartPointers (TestCAbiSmartPointers.cpp)
	const Be::ClassInfo* type = nullptr;
	for( const Be::ClassRegistration& reg : BlueRegistration::GetClassRegs() )
	{
		if( strcmp( reg.mType->mClassId->GetName(), "TestCAbiSmartPointers" ) == 0 )
		{
			type = reg.mType;
		}
	}
	ASSERT_NE( type, nullptr );
	EXPECT_EQ( MemberType( type, "held" ), "object:IRoot" );
	EXPECT_EQ( MethodSignature( type, "PassPtr" ), "object:TestMultipleInterfaces(object:TestMultipleInterfaces) opt=0" );
	EXPECT_EQ( MethodSignature( type, "PassRoot" ), "object:IRoot(object:IRoot) opt=0" );
	EXPECT_EQ( MethodSignature( type, "PassList" ), "list<object:TestMultipleInterfaces>(list<object:TestMultipleInterfaces>) opt=0" );
	EXPECT_EQ( MethodSignature( type, "GetHeldByRef" ), "object:IRoot() opt=0" );
}
