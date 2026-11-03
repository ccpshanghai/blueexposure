// Copyright (c) 2026 CCP Games
//
// Object identity through the C ABI: every object blue hands out is the object's root
// (GetRootObject), so an object reached through any of its interfaces has one BlueObject*.

#include "TestCAbiUtil.h"
#include "TestMultipleInterfaces.h"

using namespace CAbiTest;

// Two IRoot bases, so ITestInterfaceTwo* is not the root pointer. Used by the C-ABI tests only.
BLUE_CLASS( TestCAbiIdentity ) :
	public ITestInterfaceOne,
	public ITestInterfaceTwo
{
public:
	EXPOSE_TO_BLUE();

	void MethodOne() override {}
	void MethodTwo() override {}

	// this, as its second interface
	ITestInterfaceTwo* GetTwo()
	{
		return this;
	}

	// The same through an out parameter, which hands over a reference
	Be::Result<bool> GetTwoOut( ITestInterfaceTwo** out )
	{
		*out = this;
		( *out )->Lock();
		return Be::Result<bool>( true );
	}

	ITestInterfaceTwoPtr GetTwoPtr()
	{
		return ITestInterfaceTwoPtr( static_cast<ITestInterfaceTwo*>( this ) );
	}

	ITestInterfaceTwo* PassTwo( ITestInterfaceTwo* two )
	{
		return two;
	}

	ITestInterfaceTwoPtr m_two;
};

TYPEDEF_BLUECLASS( TestCAbiIdentity );

BLUE_DEFINE( TestCAbiIdentity );

const Be::ClassInfo* TestCAbiIdentity::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiIdentity, "Object identity for the C-ABI tests" )
		MAP_INTERFACE( TestCAbiIdentity )
		MAP_INTERFACE( ITestInterfaceOne )
		MAP_INTERFACE( ITestInterfaceTwo )
		MAP_ATTRIBUTE_WITH_IID( "two", m_two, "an ITestInterfaceTwo", Be::READWRITE, &GetITestInterfaceTwoIID() )
		BLUE_CABI_ADD_METHOD_AND_WRAP( "GetTwo", GetTwo, "this as ITestInterfaceTwo*" )
		BLUE_CABI_ADD_METHOD_AND_WRAP( "GetTwoOut", GetTwoOut, "this as an ITestInterfaceTwo** out parameter" )
		BLUE_CABI_ADD_METHOD_AND_WRAP( "GetTwoPtr", GetTwoPtr, "this as BluePtr<ITestInterfaceTwo>" )
		BLUE_CABI_ADD_METHOD_AND_WRAP( "PassTwo", PassTwo, "an ITestInterfaceTwo* in and out" )
	EXPOSURE_END()
}

namespace
{

class CAbiIdentity : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE( m_obj.CreateInstance() );
		// Two IRoot bases: ask through the first
		m_root = static_cast<ITestInterfaceOne*>( (TestCAbiIdentity*)m_obj )->GetRootObject();
		m_two = static_cast<ITestInterfaceTwo*>( (TestCAbiIdentity*)m_obj );
		// The point of the class: its second interface is another pointer
		ASSERT_NE( reinterpret_cast<IRoot*>( m_two ), m_root );
	}

	TestCAbiIdentityPtr m_obj;
	IRoot* m_root = nullptr;
	ITestInterfaceTwo* m_two = nullptr;
};

} // namespace

TEST_F( CAbiIdentity, ReturnedInterfaceIsTheRoot )
{
	const int refs = m_root->GetRefCount();
	const char* methods[] = { "GetTwo", "GetTwoOut", "GetTwoPtr" };
	for( const char* name : methods )
	{
		{
			Result r;
			Call( r, m_root, name );
			EXPECT_CABI_OK( r ) << name;
			ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_OBJECT ) ) << name;
			EXPECT_EQ( r.O(), m_root ) << name;
			// One reference, on the object
			EXPECT_EQ( m_root->GetRefCount(), refs + 1 ) << name;
		}
		EXPECT_EQ( m_root->GetRefCount(), refs ) << name;
	}
}

TEST_F( CAbiIdentity, QueryInterfaceWrapsAsTheRoot )
{
	const int refs = m_root->GetRefCount();
	{
		ITestInterfaceTwoPtr two;
		ASSERT_TRUE( m_root->QueryInterface( GetITestInterfaceTwoIID(), (void**)&two, BEQI_NONE ) );
		IRoot* interfacePointer = reinterpret_cast<IRoot*>( (ITestInterfaceTwo*)two );
		EXPECT_EQ( interfacePointer, reinterpret_cast<IRoot*>( m_two ) );

		Result wrapped;
		BlueCWrapObject( interfacePointer, wrapped.out );
		ASSERT_EQ( wrapped.Kind(), uint32_t( BLUE_VK_OBJECT ) );
		EXPECT_EQ( wrapped.O(), m_root );
		EXPECT_EQ( m_root->GetRefCount(), refs + 2 );

		// The interface pointer is accepted as an argument and as self, and comes back as the root
		Result passed, asSelf;
		Call( passed, m_root, "PassTwo", { Obj( interfacePointer ) } );
		Call( asSelf, interfacePointer, "GetTwo" );
		EXPECT_CABI_OK( passed );
		EXPECT_CABI_OK( asSelf );
		EXPECT_EQ( passed.O(), m_root );
		EXPECT_EQ( asSelf.O(), m_root );
	}
	EXPECT_EQ( m_root->GetRefCount(), refs );
}

TEST_F( CAbiIdentity, ObjectAttributeIsTheRoot )
{
	// The attribute holds the interface it is declared as; reading it gives the root
	TestCAbiIdentityPtr holder;
	ASSERT_TRUE( holder.CreateInstance() );
	const int refs = m_root->GetRefCount();

	SetOk( (TestCAbiIdentity*)holder, "two", Obj( m_root ) );
	EXPECT_EQ( reinterpret_cast<IRoot*>( (ITestInterfaceTwo*)holder->m_two ), reinterpret_cast<IRoot*>( m_two ) );
	EXPECT_EQ( m_root->GetRefCount(), refs + 1 );
	{
		Result r;
		Get( r, (TestCAbiIdentity*)holder, "two" );
		EXPECT_CABI_OK( r );
		EXPECT_EQ( r.O(), m_root );
		EXPECT_EQ( m_root->GetRefCount(), refs + 2 );
	}
	SetOk( (TestCAbiIdentity*)holder, "two", None() );
	EXPECT_EQ( m_root->GetRefCount(), refs );
}
