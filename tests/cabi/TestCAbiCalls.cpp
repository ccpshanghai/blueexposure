// Copyright (c) 2026 CCP Games
//
// Calling methods and module functions through their C thunks: every arity, the Be::Result
// conventions (plain, object out-parameter, reference out-parameter), optional arguments,
// superclass methods, argument and self errors, and C++ exceptions.

#include "TestCAbiUtil.h"
#include "TestMethods.h"
#include "TestMultipleInterfaces.h"
#include "TestOptionalArgs.h"
#include "TestSuperclassMethods.h"

#include <stdexcept>

using namespace CAbiTest;

static std::vector<BlueCValue> Ints( size_t count )
{
	std::vector<BlueCValue> args;
	for( size_t i = 0; i < count; ++i )
	{
		args.push_back( Int( int64_t( i ) + 1 ) );
	}
	return args;
}

static std::string Numbered( const char* name, size_t n )
{
	return std::string( name ) + std::to_string( n );
}

class CAbiCalls : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE( m_methods.CreateInstance() );
		SetReturnError( false );
	}

	void TearDown() override
	{
		SetReturnError( false );
	}

	// Both the TestMethods attribute and the module-level flag, through the C ABI
	void SetReturnError( bool value )
	{
		SetOk( Methods(), "returnError", Bool( value ) );
		Result r;
		CallFunction( r, "SetReturnError", { Bool( value ) } );
		EXPECT_CABI_OK( r );
	}

	TestMethods* Methods() { return m_methods; }

	TestMethodsPtr m_methods;
};


//--------------------------------------------------------------------
// Arities
//--------------------------------------------------------------------

TEST_F( CAbiCalls, VoidAndIntArities )
{
	for( size_t n = 0; n <= 9; ++n )
	{
		Result v, i, fv, fi;
		Call( v, Methods(), Numbered( "VoidArity", n ).c_str(), Ints( n ) );
		Call( i, Methods(), Numbered( "IntArity", n ).c_str(), Ints( n ) );
		CallFunction( fv, Numbered( "VoidArity", n ).c_str(), Ints( n ) );
		CallFunction( fi, Numbered( "IntArity", n ).c_str(), Ints( n ) );

		EXPECT_CABI_OK( v ) << " arity " << n;
		EXPECT_EQ( v.Kind(), uint32_t( BLUE_VK_NONE ) );
		EXPECT_CABI_OK( i ) << " arity " << n;
		EXPECT_EQ( i.Kind(), uint32_t( BLUE_VK_INT ) );
		EXPECT_EQ( i.I(), int64_t( n ) );
		EXPECT_CABI_OK( fv ) << " arity " << n;
		EXPECT_CABI_OK( fi ) << " arity " << n;
		EXPECT_EQ( fi.I(), int64_t( n ) );
	}
}

TEST_F( CAbiCalls, BeResultArities )
{
	for( bool fail : { false, true } )
	{
		SetReturnError( fail );
		for( size_t n = 0; n <= 9; ++n )
		{
			Result m, f;
			Call( m, Methods(), Numbered( "BeResultArity", n ).c_str(), Ints( n ) );
			CallFunction( f, Numbered( "BeResultArity", n ).c_str(), Ints( n ) );
			if( fail )
			{
				EXPECT_CABI_ERROR( m, BLUE_E_FAILED );
				EXPECT_EQ( m.Message(), "Failed!" );
				EXPECT_CABI_ERROR( f, BLUE_E_FAILED );
			}
			else
			{
				EXPECT_CABI_OK( m ) << " arity " << n;
				EXPECT_EQ( m.Kind(), uint32_t( BLUE_VK_NONE ) );
				EXPECT_CABI_OK( f ) << " arity " << n;
			}
		}
	}
}

TEST_F( CAbiCalls, BeResultWithReferenceOut )
{
	// The last (reference) parameter is the result; the script passes one argument fewer
	for( bool fail : { false, true } )
	{
		SetReturnError( fail );
		for( size_t n = 1; n <= 9; ++n )
		{
			Result m, f, tooMany;
			Call( m, Methods(), Numbered( "BeResultArity", n ).append( "WithRef" ).c_str(), Ints( n - 1 ) );
			CallFunction( f, Numbered( "BeResultArity", n ).append( "WithRef" ).c_str(), Ints( n - 1 ) );
			Call( tooMany, Methods(), Numbered( "BeResultArity", n ).append( "WithRef" ).c_str(), Ints( n ) );
			if( fail )
			{
				EXPECT_CABI_ERROR( m, BLUE_E_FAILED );
				EXPECT_CABI_ERROR( f, BLUE_E_FAILED );
			}
			else
			{
				EXPECT_CABI_OK( m ) << " arity " << n;
				EXPECT_EQ( m.Kind(), uint32_t( BLUE_VK_INT ) );
				EXPECT_EQ( m.I(), 42 );
				EXPECT_CABI_OK( f ) << " arity " << n;
				EXPECT_EQ( f.I(), 42 );
			}
			EXPECT_CABI_ERROR( tooMany, BLUE_E_ARGUMENT_COUNT );
		}
	}
}

TEST_F( CAbiCalls, BeResultWithObjectOut )
{
	// The last (X**) parameter is the result: the caller gets the factory's one reference
	const uint32_t live = LiveCount<TestMultipleInterfaces>();
	for( size_t n = 0; n <= 8; ++n )
	{
		Result r;
		Call( r, Methods(), Numbered( "SafeFactoryFunction", n ).c_str(), Ints( n ) );
		EXPECT_CABI_OK( r ) << " arity " << n;
		ASSERT_EQ( r.Kind(), uint32_t( BLUE_VK_OBJECT ) );
		IRoot* obj = r.O();
		EXPECT_EQ( RefCount( obj ), 1 );
		EXPECT_NE( dynamic_cast<TestMultipleInterfaces*>( obj ), nullptr );
		EXPECT_EQ( LiveCount<TestMultipleInterfaces>(), live + 1 );
	}
	EXPECT_EQ( LiveCount<TestMultipleInterfaces>(), live );

	SetReturnError( true );
	Result failed;
	Call( failed, Methods(), "SafeFactoryFunction2", Ints( 2 ) );
	EXPECT_CABI_ERROR( failed, BLUE_E_FAILED );
	EXPECT_EQ( LiveCount<TestMultipleInterfaces>(), live );
}

TEST_F( CAbiCalls, DiscardedResultIsReleased )
{
	const uint32_t live = LiveCount<TestMultipleInterfaces>();
	const BlueCMethodDef* def = BlueCFindMethod( Methods()->ClassType(), "SafeFactoryFunction0", strlen( "SafeFactoryFunction0" ) );
	ASSERT_NE( def, nullptr );
	EXPECT_EQ( def->fn( AsC( Methods() ), nullptr, 0, nullptr, nullptr ), int32_t( BLUE_OK ) );
	EXPECT_EQ( LiveCount<TestMultipleInterfaces>(), live );
}

TEST_F( CAbiCalls, FunctionReturnTypes )
{
	Result b, i, u, f, d, c, s;
	CallFunction( b, "FunctionReturningBool", { Bool( true ) } );
	CallFunction( i, "FunctionReturningInt", { Int( -3 ) } );
	CallFunction( u, "FunctionReturningUnsignedInt", { UInt( 3 ) } );
	CallFunction( f, "FunctionReturningFloat", { Double( 0.25 ) } );
	CallFunction( d, "FunctionReturningDouble", { Double( 0.1 ) } );
	CallFunction( c, "FunctionReturningConstChar", { Str( "c" ) } );
	CallFunction( s, "FunctionReturningString", { Str( "s" ) } );

	EXPECT_CABI_OK( b );
	EXPECT_EQ( b.Kind(), uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( i.I(), -3 );
	EXPECT_EQ( u.Kind(), uint32_t( BLUE_VK_UINT ) );
	EXPECT_EQ( u.U(), 3u );
	EXPECT_EQ( f.D(), 0.25 );
	EXPECT_EQ( d.D(), 0.1 );
	EXPECT_EQ( c.S(), "c" );
	EXPECT_EQ( s.S(), "s" );
}

TEST( CAbiFunctions, RegisteredWithDocAndNoOptionals )
{
	const BlueCMethodDef* def = BlueCFindFunction( BlueRegistration::GetCFuncRegs(), "IntArity3", 9 );
	ASSERT_NE( def, nullptr );
	EXPECT_STREQ( def->name, "IntArity3" );
	EXPECT_STREQ( def->doc, "" );
	EXPECT_EQ( def->optionalCount, 0u );
	// Lookup is by length, not by NUL
	EXPECT_NE( BlueCFindFunction( BlueRegistration::GetCFuncRegs(), "IntArity3xyz", 9 ), nullptr );
	EXPECT_EQ( BlueCFindFunction( BlueRegistration::GetCFuncRegs(), "IntArity", 8 ), nullptr );
	EXPECT_EQ( BlueCFindFunction( BlueRegistration::GetCFuncRegs(), "NoSuchFunction", 14 ), nullptr );
}


//--------------------------------------------------------------------
// Argument, self and output errors
//--------------------------------------------------------------------

TEST_F( CAbiCalls, ArgumentCount )
{
	Result tooFew, tooMany;
	Call( tooFew, Methods(), "IntArity2", Ints( 1 ) );
	Call( tooMany, Methods(), "IntArity2", Ints( 3 ) );

	EXPECT_CABI_ERROR( tooFew, BLUE_E_ARGUMENT_COUNT );
	EXPECT_EQ( tooFew.Message(), "function expects 2 arguments, got 1" );
	EXPECT_CABI_ERROR( tooMany, BLUE_E_ARGUMENT_COUNT );
}

TEST_F( CAbiCalls, ArgumentType )
{
	Result r;
	Call( r, Methods(), "IntArity2", { Int( 1 ), Str( "two" ) } );
	EXPECT_CABI_ERROR( r, BLUE_E_TYPE );
	EXPECT_EQ( r.Message(), "argument 1: expected integer, got string" );
}

TEST_F( CAbiCalls, NullArgumentArray )
{
	const BlueCMethodDef* def = BlueCFindMethod( Methods()->ClassType(), "IntArity2", 9 );
	ASSERT_NE( def, nullptr );
	Result r;
	r.status = def->fn( AsC( Methods() ), nullptr, 2, &r.out, &r.err );
	EXPECT_CABI_ERROR( r, BLUE_E_INVALID_ARGUMENT );
}

TEST_F( CAbiCalls, NullErrorStillReturnsStatus )
{
	const BlueCMethodDef* def = BlueCFindMethod( Methods()->ClassType(), "IntArity2", 9 );
	ASSERT_NE( def, nullptr );
	BlueCValue out;
	EXPECT_EQ( def->fn( AsC( Methods() ), nullptr, 0, &out, nullptr ), int32_t( BLUE_E_ARGUMENT_COUNT ) );
	EXPECT_EQ( out.kind, uint32_t( BLUE_VK_NONE ) );
}

TEST_F( CAbiCalls, Self )
{
	TestOptionalArgsPtr other;
	ASSERT_TRUE( other.CreateInstance() );
	const BlueCMethodDef* def = BlueCFindMethod( Methods()->ClassType(), "IntArity0", 9 );
	ASSERT_NE( def, nullptr );

	Result noSelf, wrongSelf;
	noSelf.status = def->fn( nullptr, nullptr, 0, &noSelf.out, &noSelf.err );
	wrongSelf.status = def->fn( AsC( (TestOptionalArgs*)other ), nullptr, 0, &wrongSelf.out, &wrongSelf.err );

	EXPECT_CABI_ERROR( noSelf, BLUE_E_SELF );
	EXPECT_CABI_ERROR( wrongSelf, BLUE_E_SELF );
	EXPECT_NE( wrongSelf.Message().find( "TestOptionalArgs" ), std::string::npos ) << wrongSelf.Message();
}


//--------------------------------------------------------------------
// Optional arguments
//--------------------------------------------------------------------

class CAbiOptionalArgs : public testing::Test
{
protected:
	void SetUp() override { ASSERT_TRUE( m_obj.CreateInstance() ); }
	TestOptionalArgs* Obj() { return m_obj; }
	TestOptionalArgsPtr m_obj;
};

TEST_F( CAbiOptionalArgs, OmittedGetNullValues )
{
	Result b, i, u, f, d, c, s, cw, w, v2, v3, o;
	Call( b, Obj(), "MethodReturningBool" );
	Call( i, Obj(), "MethodReturningInt" );
	Call( u, Obj(), "MethodReturningUnsignedInt" );
	Call( f, Obj(), "MethodReturningFloat" );
	Call( d, Obj(), "MethodReturningDouble" );
	Call( c, Obj(), "MethodReturningConstChar" );
	Call( s, Obj(), "MethodReturningString" );
	Call( cw, Obj(), "MethodReturningConstWChar" );
	Call( w, Obj(), "MethodReturningWString" );
	Call( v2, Obj(), "MethodReturningVector2" );
	Call( v3, Obj(), "MethodReturningVector3" );
	Call( o, Obj(), "MethodReturningBlueObject" );

	EXPECT_CABI_OK( b );
	EXPECT_EQ( b.Kind(), uint32_t( BLUE_VK_BOOL ) );
	EXPECT_EQ( b.I(), 0 );
	EXPECT_EQ( i.I(), 0 );
	EXPECT_EQ( u.U(), 0u );
	EXPECT_EQ( f.D(), 0.0 );
	EXPECT_EQ( d.D(), 0.0 );
	EXPECT_EQ( c.S(), "" );
	EXPECT_EQ( s.S(), "" );
	EXPECT_EQ( cw.S(), "" );
	EXPECT_EQ( w.S(), "" );
	EXPECT_EQ( v2.Kind(), uint32_t( BLUE_VK_VECTOR ) );
	EXPECT_EQ( v2.out.u.vec.v[0], 0.0 );
	EXPECT_EQ( v3.out.aux, 3 | BLUE_VEC_ELEMENT_FLOAT );
	EXPECT_CABI_OK( o );
	EXPECT_EQ( o.Kind(), uint32_t( BLUE_VK_NONE ) );
}

TEST_F( CAbiOptionalArgs, Given )
{
	Result i, s, tooMany;
	Call( i, Obj(), "MethodReturningInt", { Int( 5 ) } );
	Call( s, Obj(), "MethodReturningConstChar", { Str( "given" ) } );
	Call( tooMany, Obj(), "MethodReturningInt", { Int( 5 ), Int( 6 ) } );

	EXPECT_EQ( i.I(), 5 );
	EXPECT_EQ( s.S(), "given" );
	EXPECT_CABI_ERROR( tooMany, BLUE_E_ARGUMENT_COUNT );
}

TEST_F( CAbiOptionalArgs, CalcSum )
{
	const BlueCMethodDef* def = BlueCFindMethod( Obj()->ClassType(), "CalcSum", 7 );
	ASSERT_NE( def, nullptr );
	EXPECT_EQ( def->optionalCount, 8u );

	for( size_t n = 1; n <= 9; ++n )
	{
		Result r;
		Call( r, Obj(), "CalcSum", Ints( n ) );
		EXPECT_CABI_OK( r ) << " with " << n << " arguments";
		EXPECT_EQ( r.I(), int64_t( n * ( n + 1 ) / 2 ) );
	}

	Result none;
	Call( none, Obj(), "CalcSum" );
	EXPECT_CABI_ERROR( none, BLUE_E_ARGUMENT_COUNT );
	EXPECT_EQ( none.Message(), "function expects 1 to 9 arguments, got 0" );
}

TEST_F( CAbiOptionalArgs, BeOptional )
{
	Result specifiedNo, specifiedYes, valueNo, valueYes, defaultNo, defaultYes;
	Call( specifiedNo, Obj(), "BeOptionalIsSpecified" );
	Call( specifiedYes, Obj(), "BeOptionalIsSpecified", { Int( 0 ) } );
	Call( valueNo, Obj(), "BeOptional" );
	Call( valueYes, Obj(), "BeOptional", { Int( 7 ) } );
	Call( defaultNo, Obj(), "BeOptionalWithDefault" );
	Call( defaultYes, Obj(), "BeOptionalWithDefault", { Int( 4 ) } );

	EXPECT_EQ( specifiedNo.I(), 0 );
	EXPECT_EQ( specifiedYes.I(), 1 );
	EXPECT_EQ( valueNo.I(), 0 );
	EXPECT_EQ( valueYes.I(), 7 );
	EXPECT_EQ( defaultNo.I(), 123 );
	EXPECT_EQ( defaultYes.I(), 4 );
}


//--------------------------------------------------------------------
// Superclass methods
//--------------------------------------------------------------------

TEST( CAbiSuperclass, MethodsComeFromTheParentTable )
{
	TestSuperclassMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	const Be::ClassInfo* type = obj->ClassType();

	// The subclass maps no methods of its own; the parent's are found by walking up
	ASSERT_NE( type->mCMethodTable, nullptr );
	EXPECT_EQ( type->mCMethodTable[0].name, nullptr );
	ASSERT_NE( type->mParentClassInfo, nullptr );
	const BlueCMethodDef* def = BlueCFindMethod( type, "IntArity3", 9 );
	ASSERT_NE( def, nullptr );
	EXPECT_EQ( def, BlueCFindMethod( type->mParentClassInfo, "IntArity3", 9 ) );

	Result r, failed;
	Call( r, (TestSuperclassMethods*)obj, "IntArity3", Ints( 3 ) );
	EXPECT_CABI_OK( r );
	EXPECT_EQ( r.I(), 3 );

	// The parent's attribute too
	SetOk( (TestSuperclassMethods*)obj, "returnError", Bool( true ) );
	Call( failed, (TestSuperclassMethods*)obj, "BeResultArity0" );
	EXPECT_CABI_ERROR( failed, BLUE_E_FAILED );
}

TEST( CAbiSuperclass, UnknownMethod )
{
	TestSuperclassMethodsPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	EXPECT_EQ( BlueCFindMethod( obj->ClassType(), "NoSuchMethod", 12 ), nullptr );
	EXPECT_EQ( BlueCFindMethod( obj->ClassType(), nullptr, 0 ), nullptr );
	// The length counts, not a NUL inside it, and a prefix is not a match
	EXPECT_EQ( BlueCFindMethod( obj->ClassType(), "IntArity3\0xyz", 13 ), nullptr );
	EXPECT_EQ( BlueCFindMethod( obj->ClassType(), "IntArity", 8 ), nullptr );
	EXPECT_NE( BlueCFindMethod( obj->ClassType(), "IntArity3xyz", 9 ), nullptr );
}


//--------------------------------------------------------------------
// C++ exceptions (a class local to this test)
//--------------------------------------------------------------------

BLUE_CLASS( TestCAbiThrowing ) : public IRoot
{
public:
	EXPOSE_TO_BLUE();

	TestCAbiThrowing( IRoot* lockobj = nullptr ) {}

	int ThrowStd( int code ) { throw std::runtime_error( "thrown " + std::to_string( code ) ); }
	void ThrowOther() { throw 42; }
	int GetThrowing() const { throw std::logic_error( "getter" ); }
	void SetThrowing( int ) { throw std::logic_error( "setter" ); }
};

TYPEDEF_BLUECLASS( TestCAbiThrowing );

BLUE_DEFINE( TestCAbiThrowing );

const Be::ClassInfo* TestCAbiThrowing::ExposeToBlue()
{
	EXPOSURE_BEGIN( TestCAbiThrowing, "Throws C++ exceptions from methods and a property" )
		MAP_INTERFACE( TestCAbiThrowing )
		MAP_METHOD_AND_WRAP( "ThrowStd", ThrowStd, "" )
		MAP_METHOD_AND_WRAP( "ThrowOther", ThrowOther, "" )
		MAP_PROPERTY( "throwing", GetThrowing, SetThrowing, "" )
	EXPOSURE_END()
}

TEST( CAbiExceptions, BecomeErrors )
{
	TestCAbiThrowingPtr obj;
	ASSERT_TRUE( obj.CreateInstance() );
	TestCAbiThrowing* raw = obj;

	Result standard, other, get, set;
	Call( standard, raw, "ThrowStd", { Int( 7 ) } );
	Call( other, raw, "ThrowOther" );
	Get( get, raw, "throwing" );
	Set( set, raw, "throwing", Int( 1 ) );

	EXPECT_CABI_ERROR( standard, BLUE_E_EXCEPTION );
	EXPECT_EQ( standard.Message(), "C++ exception: thrown 7" );
	EXPECT_CABI_ERROR( other, BLUE_E_EXCEPTION );
	EXPECT_EQ( other.Message(), "unknown C++ exception" );
	EXPECT_CABI_ERROR( get, BLUE_E_EXCEPTION );
	EXPECT_EQ( get.Message(), "C++ exception: getter" );
	EXPECT_EQ( set.status, int32_t( BLUE_E_EXCEPTION ) );
	EXPECT_EQ( set.Message(), "C++ exception: setter" );
}
