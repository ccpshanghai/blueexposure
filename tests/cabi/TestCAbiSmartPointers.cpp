// Copyright (c) 2026 CCP Games
//
// Smart pointers (BluePtr<T>, BlueBasicPtr<T>, IRootPtr) as arguments, return values, out
// parameters and property values: converted as T*, so a returned one is an OBJECT holding one
// reference for the caller, null is NONE, and an argument holds a reference only during the call.

#include "TestCAbiUtil.h"
#include "TestMethods.h"
#include "TestMultipleInterfaces.h"

#include <vector>

using namespace CAbiTest;

// Methods and a property over smart pointers; used by the C-ABI tests only (the Python backend
// has no smart pointer conversion)
BLUE_CLASS( TestCAbiSmartPointers ) : public IRoot
{
public:
	EXPOSE_TO_BLUE();

	BluePtr<TestMultipleInterfaces> PassPtr( BluePtr<TestMultipleInterfaces> value )
	{
		m_refsDuringCall = value ? RefCount( (TestMultipleInterfaces*)value ) : 0;
		return value;
	}

	BlueBasicPtr<TestMultipleInterfaces> PassBasicPtr( const BlueBasicPtr<TestMultipleInterfaces>& value )
	{
		return value;
	}

	IRootPtr PassRoot( const IRootPtr& value )
	{
		return value;
	}

	std::vector<BluePtr<TestMultipleInterfaces>> PassList( const std::vector<BluePtr<TestMultipleInterfaces>>& values )
	{
		return values;
	}

	// The out parameter is handed to the caller on success
	Be::Result<bool> GetHeldByRef( IRootPtr& out ) const
	{
		out = m_held;
		return Be::Result<bool>( true );
	}

	IRootPtr GetHeld() const
	{
		return m_held;
	}

	void SetHeld( IRootPtr value )
	{
		m_held = value;
	}

	IRootPtr m_held;
	int m_refsDuringCall = -1;
};

TYPEDEF_BLUECLASS( TestCAbiSmartPointers );

BLUE_DEFINE( TestCAbiSmartPointers );

// The Python backend has no smart pointer conversion, so with both backends on the methods and
// the property are registered in the C backend only, through the fragments the MAP_* macros use
#define MAP_CABI_METHOD( name, functionName, docString ) \
	BLUE_CABI_ADD_METHOD_AND_WRAP( name, functionName, docString )

#define MAP_CABI_PROPERTY( nameString, getterFunction, setterFunction, docString ) \
	{ \
		static Be::VarEntry d = { nameString, Be::IROOTPTR, 0, 0, nullptr, docString, Be::READWRITE, nullptr, nullptr, nullptr \
								  BLUE_CABI_PROPERTY_FIELDS( getterFunction, setterFunction ) }; \
		s_attributes.push_back( d ); \
	}

const Be::ClassInfo* TestCAbiSmartPointers::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiSmartPointers, "Smart pointers for the C-ABI tests" )
		MAP_INTERFACE( TestCAbiSmartPointers )
		MAP_CABI_METHOD( "PassPtr", PassPtr, "BluePtr<TestMultipleInterfaces> in and out" )
		MAP_CABI_METHOD( "PassBasicPtr", PassBasicPtr, "BlueBasicPtr<TestMultipleInterfaces> in and out" )
		MAP_CABI_METHOD( "PassRoot", PassRoot, "IRootPtr in and out" )
		MAP_CABI_METHOD( "PassList", PassList, "a list of BluePtr<TestMultipleInterfaces> in and out" )
		MAP_CABI_METHOD( "GetHeldByRef", GetHeldByRef, "IRootPtr out parameter" )
		MAP_CABI_PROPERTY( "held", GetHeld, SetHeld, "an IRootPtr property" )
	EXPOSURE_END()
}

namespace
{

class CAbiSmartPointers : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE( m_obj.CreateInstance() );
		ASSERT_TRUE( m_interfaces.CreateInstance() );
		ASSERT_TRUE( m_methods.CreateInstance() );
	}

	TestCAbiSmartPointers* Self() { return m_obj; }
	TestMultipleInterfaces* Interfaces() { return m_interfaces; }

	TestCAbiSmartPointersPtr m_obj;
	TestMultipleInterfacesPtr m_interfaces;
	TestMethodsPtr m_methods;
};

} // namespace

TEST_F( CAbiSmartPointers, Supported )
{
	EXPECT_TRUE( BlueCIsSupported<BluePtr<TestMultipleInterfaces>>::value );
	EXPECT_TRUE( BlueCIsSupported<BlueBasicPtr<TestMultipleInterfaces>>::value );
	EXPECT_TRUE( BlueCIsSupported<IRootPtr>::value );
}

TEST_F( CAbiSmartPointers, RoundTripKeepsReferencesBalanced )
{
	const char* methods[] = { "PassPtr", "PassBasicPtr", "PassRoot" };
	for( const char* name : methods )
	{
		const int refs = RefCount( Interfaces() );
		{
			Result r;
			Call( r, Self(), name, { Obj( Interfaces() ) } );
			EXPECT_CABI_OK( r ) << name;
			ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_OBJECT ) ) << name;
			EXPECT_EQ( r.O(), reinterpret_cast<IRoot*>( Interfaces() ) ) << name;
			// The returned value holds one reference; the argument's smart pointer has let go
			EXPECT_EQ( RefCount( Interfaces() ), refs + 1 ) << name;
		}
		EXPECT_EQ( RefCount( Interfaces() ), refs ) << name;
	}
}

TEST_F( CAbiSmartPointers, ArgumentHoldsAReferenceDuringTheCall )
{
	const int refs = RefCount( Interfaces() );
	Result r;
	Call( r, Self(), "PassPtr", { Obj( Interfaces() ) } );
	EXPECT_CABI_OK( r );
	// The argument storage and the by-value parameter each hold one
	EXPECT_EQ( Self()->m_refsDuringCall, refs + 2 );
}

TEST_F( CAbiSmartPointers, NullIsNone )
{
	const char* methods[] = { "PassPtr", "PassBasicPtr", "PassRoot" };
	for( const char* name : methods )
	{
		Result r;
		Call( r, Self(), name, { None() } );
		EXPECT_CABI_OK( r ) << name;
		EXPECT_EQ( r.Kind(), uint32_t( BLUE_VK_NONE ) ) << name;
	}
}

TEST_F( CAbiSmartPointers, WrongClassAndKind )
{
	Result wrongClass, wrongKind;
	Call( wrongClass, Self(), "PassPtr", { Obj( (TestMethods*)m_methods ) } );
	Call( wrongKind, Self(), "PassBasicPtr", { Int( 1 ) } );
	EXPECT_CABI_ERROR( wrongClass, BLUE_E_TYPE );
	EXPECT_NE( wrongClass.Message().find( "TestMethods" ), std::string::npos ) << wrongClass.Message();
	EXPECT_CABI_ERROR( wrongKind, BLUE_E_TYPE );

	// IRootPtr takes any object
	Result any;
	Call( any, Self(), "PassRoot", { Obj( (TestMethods*)m_methods ) } );
	EXPECT_CABI_OK( any );
	EXPECT_EQ( any.O(), reinterpret_cast<IRoot*>( (TestMethods*)m_methods ) );
}

TEST_F( CAbiSmartPointers, ListOfSmartPointers )
{
	const int refs = RefCount( Interfaces() );
	{
		std::vector<BlueCValue> items = { Obj( Interfaces() ), None(), Obj( Interfaces() ) };
		Result r;
		Call( r, Self(), "PassList", { Array( items ) } );
		EXPECT_CABI_OK( r );
		ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_ARRAY ) );
		ASSERT_EQ( r.out.u.arr.count, size_t( 3 ) );
		EXPECT_EQ( r.out.u.arr.items[0].kind, uint32_t( BLUE_VK_OBJECT ) );
		EXPECT_EQ( r.out.u.arr.items[0].u.obj, AsC( Interfaces() ) );
		EXPECT_EQ( r.out.u.arr.items[1].kind, uint32_t( BLUE_VK_NONE ) );
		EXPECT_EQ( r.out.u.arr.items[2].u.obj, AsC( Interfaces() ) );
		// One reference per returned item
		EXPECT_EQ( RefCount( Interfaces() ), refs + 2 );
	}
	EXPECT_EQ( RefCount( Interfaces() ), refs );

	// One bad item fails the whole argument, without leaking the items before it
	std::vector<BlueCValue> bad = { Obj( Interfaces() ), Obj( (TestMethods*)m_methods ) };
	Result r;
	Call( r, Self(), "PassList", { Array( bad ) } );
	EXPECT_CABI_ERROR( r, BLUE_E_TYPE );
	EXPECT_EQ( RefCount( Interfaces() ), refs );
}

TEST_F( CAbiSmartPointers, OutParameterAndProperty )
{
	const int refs = RefCount( Interfaces() );

	Result empty;
	Call( empty, Self(), "GetHeldByRef" );
	EXPECT_CABI_OK( empty );
	EXPECT_EQ( empty.Kind(), uint32_t( BLUE_VK_NONE ) );

	SetOk( Self(), "held", Obj( Interfaces() ) );
	EXPECT_EQ( RefCount( Interfaces() ), refs + 1 );
	{
		Result byRef, get;
		Call( byRef, Self(), "GetHeldByRef" );
		Get( get, Self(), "held" );
		EXPECT_CABI_OK( byRef );
		EXPECT_CABI_OK( get );
		EXPECT_EQ( byRef.O(), reinterpret_cast<IRoot*>( Interfaces() ) );
		EXPECT_EQ( get.O(), reinterpret_cast<IRoot*>( Interfaces() ) );
		EXPECT_EQ( RefCount( Interfaces() ), refs + 3 );
	}
	EXPECT_EQ( RefCount( Interfaces() ), refs + 1 );

	SetOk( Self(), "held", None() );
	EXPECT_EQ( RefCount( Interfaces() ), refs );
	Result cleared;
	Get( cleared, Self(), "held" );
	EXPECT_CABI_OK( cleared );
	EXPECT_EQ( cleared.Kind(), uint32_t( BLUE_VK_NONE ) );
}
