// Copyright (c) 2026 CCP Games
//
// Members resolved once (BlueCResolveMember) and accessed by entry (BlueCGetMemberEntry /
// BlueCSetMemberEntry): the same entries, results and statuses as by name, the parent offset
// computed from the object's class, and BLUE_E_SELF for an object of another class.

#include "TestCAbiUtil.h"
#include "TestAttributes.h"
#include "TestMultipleInterfaces.h"
#include "TestProperties.h"

using namespace CAbiTest;

// A parent with an attribute and a property, and a child whose parent part is not at offset 0,
// so the child's members of the parent need a non-zero parent offset. Used by the C-ABI tests
// only.
BLUE_CLASS( TestCAbiEntryBase ) : public IRoot
{
public:
	EXPOSE_TO_BLUE();

	int GetBaseProperty() const { return m_baseProperty; }
	void SetBaseProperty( int value ) { m_baseProperty = value; }

	int m_base = 0;
	int m_baseProperty = 0;
};

TYPEDEF_BLUECLASS( TestCAbiEntryBase );

BLUE_DEFINE( TestCAbiEntryBase );

const Be::ClassInfo* TestCAbiEntryBase::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiEntryBase, "Parent class for the member entry tests" )
		MAP_INTERFACE( TestCAbiEntryBase )
		MAP_ATTRIBUTE( "base", m_base, "an attribute of the parent", Be::READWRITE )
		MAP_PROPERTY( "baseProperty", GetBaseProperty, SetBaseProperty, "a property of the parent" )
	EXPOSURE_END()
}

BLUE_CLASS( TestCAbiEntryChild ) :
	public ITestInterfaceOne,
	public TestCAbiEntryBase
{
public:
	EXPOSE_TO_BLUE();

	void MethodOne() override {}

	int m_own = 0;
	int m_hidden = 0;
};

TYPEDEF_BLUECLASS( TestCAbiEntryChild );

BLUE_DEFINE( TestCAbiEntryChild );

const Be::ClassInfo* TestCAbiEntryChild::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiEntryChild, "Child class for the member entry tests" )
		MAP_INTERFACE( TestCAbiEntryChild )
		MAP_INTERFACE( ITestInterfaceOne )
		MAP_ATTRIBUTE( "own", m_own, "an attribute of the child", Be::READWRITE )
		MAP_ATTRIBUTE( "hidden", m_hidden, "a hidden attribute", Be::READWRITE | Be::HIDDEN )
	EXPOSURE_CHAINTO( TestCAbiEntryBase )
}

// TestProperties is not declared with BLUE_CLASS, so it is created by class id
class TestPropertiesInstance : public BluePtr<TestProperties>
{
public:
	bool CreateInstance() { return BluePtr<TestProperties>::CreateInstance( *TestProperties::Clsid() ); }
};

namespace
{

struct Member
{
	const Be::ClassInfo* owner = nullptr;
	const Be::VarEntry* entry = nullptr;
};

Member Resolve( const Be::ClassInfo* type, const char* name )
{
	Member m;
	m.entry = BlueCResolveMember( type, name, strlen( name ), &m.owner );
	return m;
}

void GetEntry( Result& r, IRoot* obj, const Member& m )
{
	r.status = BlueCGetMemberEntry( obj, m.owner, m.entry, &r.out, &r.err );
}

void SetEntry( Result& r, IRoot* obj, const Member& m, const BlueCValue& value )
{
	r.status = BlueCSetMemberEntry( obj, m.owner, m.entry, &value, &r.err );
}

} // namespace

TEST( CAbiMemberEntry, ResolvesWhatFindMemberFinds )
{
	const Be::ClassInfo* base = TestCAbiEntryBase::ClassType_();
	const Be::ClassInfo* child = TestCAbiEntryChild::ClassType_();

	// Own and inherited, resolved on the class and on a subclass: one entry, held by its class
	const Member own = Resolve( child, "own" );
	const Member inherited = Resolve( child, "base" );
	const Member onParent = Resolve( base, "base" );
	const Member property = Resolve( child, "baseProperty" );
	EXPECT_EQ( own.owner, child );
	EXPECT_EQ( inherited.owner, base );
	EXPECT_EQ( onParent.owner, base );
	EXPECT_EQ( property.owner, base );
	EXPECT_EQ( inherited.entry, onParent.entry );
	EXPECT_EQ( own.entry, BlueCFindMember( child, "own", 3, nullptr ) );
	EXPECT_EQ( inherited.entry, BlueCFindMember( child, "base", 4, nullptr ) );
	EXPECT_EQ( property.entry, BlueCFindMember( child, "baseProperty", 12, nullptr ) );
	ASSERT_NE( property.entry, nullptr );
	EXPECT_NE( property.entry->mCGet, nullptr );

	// The child's parent part is not at offset 0, so its inherited attributes need the offset
	ptrdiff_t extraOffset = 0;
	ASSERT_NE( BlueCFindMember( child, "base", 4, &extraOffset ), nullptr );
	EXPECT_NE( extraOffset, 0 );

	// Hidden, unknown and null names resolve to nothing
	const Member hidden = Resolve( child, "hidden" );
	const Member unknown = Resolve( child, "noSuchMember" );
	EXPECT_EQ( hidden.entry, nullptr );
	EXPECT_EQ( hidden.owner, nullptr );
	EXPECT_EQ( unknown.entry, nullptr );
	EXPECT_EQ( unknown.owner, nullptr );
	EXPECT_EQ( BlueCResolveMember( child, nullptr, 0, nullptr ), nullptr );
}

TEST( CAbiMemberEntry, AttributesMatchByName )
{
	TestCAbiEntryChildPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestCAbiEntryChild* raw = obj;
	IRoot* root = static_cast<ITestInterfaceOne*>( raw )->GetRootObject();
	// Another pointer of the same object: its parent part
	IRoot* parentPart = static_cast<IRoot*>( static_cast<TestCAbiEntryBase*>( raw ) );
	ASSERT_NE( parentPart, root );

	// A member resolved on the parent class, used on the child
	const Member base = Resolve( TestCAbiEntryBase::ClassType_(), "base" );
	const Member own = Resolve( TestCAbiEntryChild::ClassType_(), "own" );

	for( IRoot* self : { root, parentPart } )
	{
		Result setBase, setOwn;
		SetEntry( setBase, self, base, Int( 41 ) );
		SetEntry( setOwn, self, own, Int( 42 ) );
		EXPECT_CABI_OK( setBase );
		EXPECT_CABI_OK( setOwn );
		// Written where the C++ members are
		EXPECT_EQ( raw->m_base, 41 );
		EXPECT_EQ( raw->m_own, 42 );

		SetOk( raw, "base", Int( 7 ) );
		SetOk( raw, "own", Int( 8 ) );
		Result getBase, getOwn;
		GetEntry( getBase, self, base );
		GetEntry( getOwn, self, own );
		EXPECT_CABI_OK( getBase );
		EXPECT_CABI_OK( getOwn );
		EXPECT_EQ( getBase.I(), 7 );
		EXPECT_EQ( getOwn.I(), 8 );
	}

	// A failure is the same as by name
	Result byEntry, byName;
	SetEntry( byEntry, root, own, Str( "x" ) );
	Set( byName, raw, "own", Str( "x" ) );
	EXPECT_CABI_ERROR( byEntry, BLUE_E_TYPE );
	EXPECT_EQ( byEntry.Message(), byName.Message() );
}

TEST( CAbiMemberEntry, PropertiesMatchByName )
{
	// Inherited property, on the child
	TestCAbiEntryChildPtr child;
	ASSERT_TRUE( child.CreateInstance() );
	IRoot* root = static_cast<ITestInterfaceOne*>( (TestCAbiEntryChild*)child )->GetRootObject();
	const Member inherited = Resolve( TestCAbiEntryBase::ClassType_(), "baseProperty" );
	Result set, get;
	SetEntry( set, root, inherited, Int( 5 ) );
	GetEntry( get, root, inherited );
	EXPECT_CABI_OK( set );
	EXPECT_CABI_OK( get );
	EXPECT_EQ( child->m_baseProperty, 5 );
	EXPECT_EQ( get.I(), 5 );

	// Read-write, read-only and failing properties
	TestPropertiesInstance obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestProperties* raw = obj;
	IRoot* self = reinterpret_cast<IRoot*>( raw );
	const Member myInt = Resolve( TestProperties::ClassType_(), "myInt" );
	const Member readOnly = Resolve( TestProperties::ClassType_(), "myStringConst" );
	const Member withError = Resolve( TestProperties::ClassType_(), "myIntWithError" );

	Result setInt, getInt;
	SetEntry( setInt, self, myInt, Int( -9 ) );
	Get( getInt, raw, "myInt" );
	EXPECT_CABI_OK( setInt );
	EXPECT_EQ( getInt.I(), -9 );

	Result readOnlyByEntry, readOnlyByName;
	SetEntry( readOnlyByEntry, self, readOnly, Str( "x" ) );
	Set( readOnlyByName, raw, "myStringConst", Str( "x" ) );
	EXPECT_CABI_ERROR( readOnlyByEntry, BLUE_E_READ_ONLY );
	EXPECT_EQ( readOnlyByEntry.Message(), readOnlyByName.Message() );

	SetOk( raw, "returnError", Bool( true ) );
	Result errorByEntry, errorByName;
	GetEntry( errorByEntry, self, withError );
	Get( errorByName, raw, "myIntWithError" );
	EXPECT_CABI_ERROR( errorByEntry, BLUE_E_FAILED );
	EXPECT_EQ( errorByEntry.Message(), errorByName.Message() );
}

TEST( CAbiMemberEntry, WrongClassIsSelf )
{
	TestCAbiEntryBasePtr parent;
	TestAttributesPtr unrelated;
	ASSERT_TRUE( parent.CreateInstance() );
	ASSERT_TRUE( unrelated.CreateInstance() );
	IRoot* parentRoot = parent->GetRootObject();
	IRoot* unrelatedRoot = unrelated->GetRootObject();

	const Member own = Resolve( TestCAbiEntryChild::ClassType_(), "own" );
	const Member base = Resolve( TestCAbiEntryBase::ClassType_(), "base" );
	const Member property = Resolve( TestCAbiEntryBase::ClassType_(), "baseProperty" );

	// A child's member on a parent object, a parent's attribute and property on an unrelated one
	Result childOnParent, attribute, getProperty, setProperty;
	GetEntry( childOnParent, parentRoot, own );
	SetEntry( attribute, unrelatedRoot, base, Int( 1 ) );
	GetEntry( getProperty, unrelatedRoot, property );
	SetEntry( setProperty, unrelatedRoot, property, Int( 1 ) );
	EXPECT_CABI_ERROR( childOnParent, BLUE_E_SELF );
	EXPECT_EQ( childOnParent.Message(), "object of class TestCAbiEntryBase is not a TestCAbiEntryChild" );
	EXPECT_CABI_ERROR( attribute, BLUE_E_SELF );
	EXPECT_EQ( attribute.Message(), "object of class TestAttributes is not a TestCAbiEntryBase" );
	EXPECT_CABI_ERROR( getProperty, BLUE_E_SELF );
	EXPECT_CABI_ERROR( setProperty, BLUE_E_SELF );

	// Nothing to access, or nothing to set
	Result noObject, noEntry, noOwner, noValue;
	GetEntry( noObject, nullptr, base );
	GetEntry( noEntry, parentRoot, Member{ base.owner, nullptr } );
	GetEntry( noOwner, parentRoot, Member{ nullptr, base.entry } );
	noValue.status = BlueCSetMemberEntry( parentRoot, base.owner, base.entry, nullptr, &noValue.err );
	EXPECT_CABI_ERROR( noObject, BLUE_E_SELF );
	EXPECT_CABI_ERROR( noEntry, BLUE_E_INVALID_ARGUMENT );
	EXPECT_CABI_ERROR( noOwner, BLUE_E_INVALID_ARGUMENT );
	EXPECT_CABI_ERROR( noValue, BLUE_E_INVALID_ARGUMENT );
}
