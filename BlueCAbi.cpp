// Copyright (c) 2026 CCP Games
//
// C-ABI backend: argument count checks, and member and method lookup and access through
// Be::ClassInfo. Attribute access follows the Python backend's (BlueVariable.cpp, BluePyWrap.cpp
// PyGetAttr / PySetAttr) per VARTYPE.

#include "include/BlueExposure.h"
#include "include/INotify.h"

#include <cstring>
#include <set>
#include <string>
#include <vector>


//--------------------------------------------------------------------
// Arguments
//--------------------------------------------------------------------

bool BlueCAbiDetail::CheckArgumentCount( const BlueCValue* args, uint32_t argc, size_t arity, size_t numOptional, BlueCError* err )
{
	if( argc && !args )
	{
		BlueCSetError( err, BLUE_E_INVALID_ARGUMENT, "%u arguments with a null argument array", argc );
		return false;
	}

	const size_t minimum = numOptional < arity ? arity - numOptional : 0;
	if( argc < minimum || argc > arity )
	{
		if( minimum == arity )
		{
			BlueCSetError( err, BLUE_E_ARGUMENT_COUNT, "function expects %zu argument%s, got %u", arity, arity == 1 ? "" : "s", argc );
		}
		else
		{
			BlueCSetError( err, BLUE_E_ARGUMENT_COUNT, "function expects %zu to %zu arguments, got %u", minimum, arity, argc );
		}
		return false;
	}
	return true;
}


//--------------------------------------------------------------------
// Lookup
//--------------------------------------------------------------------

// name need not be NUL-terminated and may hold a NUL, so candidate's length is checked first:
// strncmp would stop at that NUL and candidate[nameLength] could lie past candidate's end
static bool BlueCNameEquals( const char* candidate, const char* name, size_t nameLength )
{
	return candidate && strnlen( candidate, nameLength + 1 ) == nameLength && memcmp( candidate, name, nameLength ) == 0;
}

// Where a method came from, for its signature: a class's own table (the signature is at the
// same index in the class's mCSignatures) or an interface's registration
struct BlueCMethodSource
{
	const Be::ClassInfo* owner;
	const BlueCSignature* signature;

	const BlueCSignature* GetSignature( const BlueCMethodDef* def ) const
	{
		if( !owner )
		{
			return signature;
		}
		return owner->mCSignatures ? &owner->mCSignatures[def - owner->mCMethodTable] : nullptr;
	}
};

// Call visit( method, source ) for each method of type, in lookup order: the class's own
// table, its parents' tables, then the interface methods (BLUE_REGISTER_CTHUNKER) of every
// interface the class and its parents map, in that order - the order Python's attribute lookup
// takes them (BlueRttiType::Init). A name can come up more than once; the first one counts.
// Stops, returning true, as soon as visit returns true.
template< typename Visit >
static bool BlueCVisitMethods( const Be::ClassInfo* type, Visit&& visit )
{
	for( const Be::ClassInfo* t = type; t; t = t->mParentClassInfo )
	{
		for( const BlueCMethodDef* def = t->mCMethodTable; def && def->name; ++def )
		{
			if( visit( def, BlueCMethodSource{ t, nullptr } ) )
			{
				return true;
			}
		}
	}

	const CThunkerRegsVector& thunkers = BlueRegistration::GetGlobalCThunkerRegs();
	if( thunkers.empty() )
	{
		return false;
	}
	for( const Be::ClassInfo* t = type; t; t = t->mParentClassInfo )
	{
		for( const Be::InterfaceEntry* entry = t->mInterfaceTable; entry && entry->mIID; ++entry )
		{
			for( const BlueCThunkerRegistration& reg : thunkers )
			{
				if( !reg.mIID.IsEqual( *entry->mIID ) )
				{
					continue;
				}
				for( size_t i = 0; reg.mMethods && reg.mMethods[i].name; ++i )
				{
					if( visit( &reg.mMethods[i], BlueCMethodSource{ nullptr, reg.mSignatures ? &reg.mSignatures[i] : nullptr } ) )
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

const BlueCMethodDef* BlueCFindMethod( const Be::ClassInfo* type, const char* name, size_t nameLength )
{
	if( !name )
	{
		return nullptr;
	}
	const BlueCMethodDef* result = nullptr;
	BlueCVisitMethods( type, [&]( const BlueCMethodDef* def, const BlueCMethodSource& ) {
		if( BlueCNameEquals( def->name, name, nameLength ) )
		{
			result = def;
			return true;
		}
		return false;
	} );
	return result;
}

bool BlueCForEachMethod( const Be::ClassInfo* type, BlueCMethodVisitor visitor, void* context )
{
	if( !visitor )
	{
		return true;
	}
	std::set<std::string> seen;
	const bool stopped = BlueCVisitMethods( type, [&]( const BlueCMethodDef* def, const BlueCMethodSource& source ) {
		if( !seen.insert( def->name ).second )
		{
			// Hidden by the method of that name found first
			return false;
		}
		return !visitor( def, source.GetSignature( def ), context );
	} );
	return !stopped;
}

const BlueCMethodDef* BlueCFindFunction( const std::vector<BlueCMethodDef>& functions, const char* name, size_t nameLength )
{
	if( !name )
	{
		return nullptr;
	}
	for( const BlueCMethodDef& def : functions )
	{
		if( BlueCNameEquals( def.name, name, nameLength ) )
		{
			return &def;
		}
	}
	return nullptr;
}

const Be::VarEntry* BlueCFindMember( const Be::ClassInfo* type, const char* name, size_t nameLength, ptrdiff_t* extraOffset )
{
	if( !name )
	{
		return nullptr;
	}
	ptrdiff_t offset = 0;
	for( ; type; offset += type->mOffsetToParent, type = type->mParentClassInfo )
	{
		for( const Be::VarEntry* entry = type->mMemberTable; entry && entry->mName; ++entry )
		{
			if( ( entry->mEditFlags & Be::HIDDEN ) == 0 && BlueCNameEquals( entry->mName, name, nameLength ) )
			{
				if( extraOffset )
				{
					*extraOffset = offset;
				}
				return entry;
			}
		}
	}
	return nullptr;
}


//--------------------------------------------------------------------
// Attributes by VARTYPE
//--------------------------------------------------------------------

static uint32_t BlueCVectorTag( const Be::VarEntry* entry )
{
	if( !entry->mIID )
	{
		return BLUE_VEC_TAG_NONE;
	}
	if( entry->mIID->IsEqual( BlueColorIID ) )
	{
		return BLUE_VEC_TAG_COLOR;
	}
	if( entry->mIID->IsEqual( BlueRotationIID ) )
	{
		return BLUE_VEC_TAG_QUATERNION;
	}
	if( entry->mIID->IsEqual( BlueMatrixIID ) )
	{
		return BLUE_VEC_TAG_MATRIX;
	}
	return BLUE_VEC_TAG_NONE;
}

// Whether an array attribute is a Matrix: by its IID, as the Python backend decides
// (BlueVariable.cpp); any other array of 16 floats is a plain array
template< typename Element >
static bool BlueCIsMatrix( size_t count, uint32_t tag )
{
	return std::is_same<Element, float>::value && count == 16 && tag == BLUE_VEC_TAG_MATRIX;
}

template< typename Element >
static bool BlueCWrapNumberArray( const Element* elements, size_t count, uint32_t elementType, uint32_t tag, BlueCValue& out, BlueCError* err )
{
	if( count <= 4 )
	{
		double components[4] = {};
		for( size_t i = 0; i < count; ++i )
		{
			components[i] = double( elements[i] );
		}
		BlueCWrapComponents( components, uint32_t( count ), elementType | tag, out );
		return true;
	}
	if( BlueCIsMatrix<Element>( count, tag ) )
	{
		return BlueCWrapMatrix( reinterpret_cast<const float*>( elements ), out, err );
	}
	return BlueCWrapArray( count, out, err, [&]( size_t i, BlueCValue& item ) {
		return BlueCWrap( elements[i], item, err );
	} );
}

// Assigns elements only once every component converts, so a failed set leaves the attribute as
// it was
template< typename Element >
static bool BlueCExtractNumberArray( const BlueCValue& in, Element* elements, size_t count, uint32_t tag, BlueCError* err )
{
	if( BlueCIsMatrix<Element>( count, tag ) )
	{
		return BlueCExtractMatrix( in, reinterpret_cast<float*>( elements ), BLUE_CABI_VALUE_INDEX, err );
	}
	std::vector<double> components( count );
	if( !BlueCExtractComponents( in, components.data(), count, std::is_integral<Element>::value, "vector", BLUE_CABI_VALUE_INDEX, err ) )
	{
		return false;
	}
	if( std::is_same<Element, float>::value && !BlueCCheckFloatRange( components.data(), count, BLUE_CABI_VALUE_INDEX, err ) )
	{
		return false;
	}
	for( size_t i = 0; i < count; ++i )
	{
		elements[i] = Element( components[i] );
	}
	return true;
}

static int32_t BlueCGetAttribute( IRoot* obj, const Be::VarEntry* entry, ptrdiff_t extraOffset, BlueCValue& out, BlueCError& err )
{
	Be::Var* value = BeMapMemberOffset( obj, entry, obj->ClassType(), extraOffset );
	bool ok = true;

	switch( entry->mType )
	{
	case Be::LONG:
		// Enums are LONG whatever their size
		if( entry->mSize == 1 )
		{
			BlueCWrapInt( *reinterpret_cast<const int8_t*>( value ), out );
		}
		else if( entry->mSize == 2 )
		{
			BlueCWrapInt( *reinterpret_cast<const int16_t*>( value ), out );
		}
		else
		{
			BlueCWrapInt( value->mLong, out );
		}
		break;
	case Be::ULONG:
		BlueCWrapUInt( value->mULong, out );
		break;
	case Be::SHORT:
		BlueCWrapUInt( value->mShort, out );
		break;
	case Be::BYTE:
		BlueCWrapUInt( value->mByte, out );
		break;
	case Be::INT64:
		BlueCWrapInt( value->mInt64, out );
		break;
	case Be::UINT64:
		BlueCWrapUInt( value->mUInt64, out );
		break;
	case Be::BOOL:
		BlueCWrapBool( value->mBool, out );
		break;
	case Be::FLOAT:
		BlueCWrapDouble( value->mFloat, out );
		break;
	case Be::DOUBLE:
		BlueCWrapDouble( value->mDouble, out );
		break;
	case Be::STDSTRING:
		ok = BlueCWrap( *reinterpret_cast<const std::string*>( value ), out, &err );
		break;
	case Be::STDWSTRING:
		ok = BlueCWrap( *reinterpret_cast<const std::wstring*>( value ), out, &err );
		break;
	case Be::SHAREDSTRING:
		ok = BlueCWrap( *reinterpret_cast<const BlueSharedString*>( value ), out, &err );
		break;
	case Be::SHAREDSTRINGW:
		ok = BlueCWrap( *reinterpret_cast<const BlueSharedStringW*>( value ), out, &err );
		break;
	case Be::CSTRING:
		ok = BlueCWrapString( value->mCharPtr ? value->mCharPtr : "", value->mCharPtr ? strlen( value->mCharPtr ) : 0, out, &err );
		break;
	case Be::WCSTRING:
		ok = BlueCWrapWString( value->mWCharPtr ? value->mWCharPtr : L"", value->mWCharPtr ? wcslen( value->mWCharPtr ) : 0, out, &err );
		break;
	case Be::CHARARRAY:
		ok = BlueCWrapString( reinterpret_cast<const char*>( value ), strnlen( reinterpret_cast<const char*>( value ), entry->mSize ), out, &err );
		break;
	case Be::FLOATARRAY:
		ok = BlueCWrapNumberArray( reinterpret_cast<const float*>( value ), entry->GetFloatArraySize(), BLUE_VEC_ELEMENT_FLOAT, BlueCVectorTag( entry ), out, &err );
		break;
	case Be::DOUBLEARRAY:
		ok = BlueCWrapNumberArray( reinterpret_cast<const double*>( value ), entry->GetDoubleArraySize(), BLUE_VEC_ELEMENT_DOUBLE, BlueCVectorTag( entry ), out, &err );
		break;
	case Be::INTARRAY:
		ok = BlueCWrapNumberArray( reinterpret_cast<const int*>( value ), entry->GetIntArraySize(), BLUE_VEC_ELEMENT_INT32, BLUE_VEC_TAG_NONE, out, &err );
		break;
	case Be::IROOT:
		// An object embedded in this one (RootParentLock and friends)
		BlueCWrapObject( reinterpret_cast<IRoot*>( value ), out );
		break;
	case Be::IROOTPTR:
		BlueCWrapObject( value->mIRootPtr, out );
		break;
	case Be::IROOTWEAKREF:
		BlueCWrapObject( *reinterpret_cast<const BlueWeakRefBase*>( value ), out );
		break;
#if BLUE_CABI_CALLBACKS
	case Be::SCRIPTCALLBACK:
		ok = BlueCWrap( *reinterpret_cast<const BlueScriptCallback*>( value ), out, &err );
		break;
#endif
	default:
		return BlueCSetError( &err, BLUE_E_UNSUPPORTED, "attribute '%s' has VARTYPE %d, which has no C-ABI conversion", entry->mName, int( entry->mType ) );
	}

	return ok ? int32_t( BLUE_OK ) : err.code;
}

// Name of a VARTYPE, for the unknown type names of attributes the C ABI cannot read
static const char* BlueCVarTypeName( Be::VARTYPE type )
{
	switch( type )
	{
	case Be::INVALID: return "INVALID";
	case Be::LONG: return "LONG";
	case Be::FLOAT: return "FLOAT";
	case Be::DOUBLE: return "DOUBLE";
	case Be::BOOL: return "BOOL";
	case Be::IROOT: return "IROOT";
	case Be::IROOTPTR: return "IROOTPTR";
	case Be::CHARARRAY: return "CHARARRAY";
	case Be::CSTRING: return "CSTRING";
	case Be::INT64: return "INT64";
	case Be::PYOBJECT: return "PYOBJECT";
	case Be::PYOBJECTPTR: return "PYOBJECTPTR";
	case Be::REFERENCE: return "REFERENCE";
	case Be::WCSTRING: return "WCSTRING";
	case Be::WREFERENCE: return "WREFERENCE";
	case Be::FLOATARRAY: return "FLOATARRAY";
	case Be::DOUBLEARRAY: return "DOUBLEARRAY";
	case Be::INTARRAY: return "INTARRAY";
	case Be::ROTREFERENCE: return "ROTREFERENCE";
	case Be::IROOTWEAKREF: return "IROOTWEAKREF";
	case Be::_PYPROPERTY: return "_PYPROPERTY";
	case Be::STDSTRING: return "STDSTRING";
	case Be::STDWSTRING: return "STDWSTRING";
	case Be::BINARYBLOCK: return "BINARYBLOCK";
	case Be::BYTE: return "BYTE";
	case Be::SHORT: return "SHORT";
	case Be::SCRIPTCALLBACK: return "SCRIPTCALLBACK";
	case Be::SHAREDSTRING: return "SHAREDSTRING";
	case Be::SHAREDSTRINGW: return "SHAREDSTRINGW";
	case Be::ULONG: return "ULONG";
	case Be::UINT64: return "UINT64";
	default: return nullptr;
	}
}

static const char* BlueCUnknownVarTypeName( const Be::VarEntry& entry, size_t count = 0 )
{
	std::string result = BLUE_CABI_UNKNOWN_TYPE_PREFIX;
	if( const char* name = BlueCVarTypeName( entry.mType ) )
	{
		result += name;
	}
	else
	{
		result += "VARTYPE" + std::to_string( int( entry.mType ) );
	}
	if( count )
	{
		result += "[" + std::to_string( count ) + "]";
	}
	return BlueCInternTypeName( result );
}

// The name of an array attribute as BlueCWrapNumberArray wraps it: a vector of 2 to 4 components
// (by element type, and for 4 floats by the IID's tag), a matrix, or else a list of numbers. One
// component, and 2 or 4 ints, are vectors with no name.
template< typename Element >
static const char* BlueCNumberArrayTypeName( const Be::VarEntry& entry, size_t count, uint32_t tag )
{
	static const char* const s_vectors[3][3] = {
		{ "vector2", "vector3", "vector4" },
		{ "vector2d", "vector3d", "vector4d" },
		{ nullptr, "vector3i", nullptr },
	};
	const size_t row = std::is_same<Element, float>::value ? 0 : std::is_same<Element, double>::value ? 1 : 2;

	if( count >= 2 && count <= 4 )
	{
		if( row == 0 && count == 4 && tag == BLUE_VEC_TAG_COLOR )
		{
			return "color";
		}
		if( row == 0 && count == 4 && tag == BLUE_VEC_TAG_QUATERNION )
		{
			return "quaternion";
		}
		const char* name = s_vectors[row][count - 2];
		return name ? name : BlueCUnknownVarTypeName( entry, count );
	}
	if( BlueCIsMatrix<Element>( count, tag ) )
	{
		return "matrix";
	}
	if( count > 4 )
	{
		return BlueCTypeName< std::vector<Element> >::Get();
	}
	return BlueCUnknownVarTypeName( entry, count );
}

const char* BlueCMemberTypeName( const Be::VarEntry& entry )
{
	if( entry.mCTypeName )
	{
		return entry.mCTypeName;
	}
	if( entry.mCGet || entry.mGetProperty )
	{
		// A property without a C getter
		return BlueCUnknownVarTypeName( entry );
	}

	// As BlueCGetAttribute wraps each VARTYPE
	switch( entry.mType )
	{
	case Be::LONG:
		return entry.mSize == 1 ? "int8" : entry.mSize == 2 ? "int16" : "int32";
	case Be::ULONG:
		return "uint32";
	case Be::SHORT:
		// short and unsigned short share this VARTYPE and are read as unsigned
		return "uint16";
	case Be::BYTE:
		// char and unsigned char share this VARTYPE and are read as unsigned
		return "uint8";
	case Be::INT64:
		return "int64";
	case Be::UINT64:
		return "uint64";
	case Be::BOOL:
		return "bool";
	case Be::FLOAT:
		return "float";
	case Be::DOUBLE:
		return "double";
	case Be::STDSTRING:
	case Be::STDWSTRING:
	case Be::SHAREDSTRING:
	case Be::SHAREDSTRINGW:
	case Be::CSTRING:
	case Be::WCSTRING:
	case Be::CHARARRAY:
		return "string";
	case Be::FLOATARRAY:
		return BlueCNumberArrayTypeName<float>( entry, entry.GetFloatArraySize(), BlueCVectorTag( &entry ) );
	case Be::DOUBLEARRAY:
		return BlueCNumberArrayTypeName<double>( entry, entry.GetDoubleArraySize(), BlueCVectorTag( &entry ) );
	case Be::INTARRAY:
		return BlueCNumberArrayTypeName<int>( entry, entry.GetIntArraySize(), BLUE_VEC_TAG_NONE );
	case Be::IROOT:
	case Be::IROOTPTR:
	case Be::IROOTWEAKREF:
		return BlueCObjectTypeName( entry.mIID );
#if BLUE_CABI_CALLBACKS
	case Be::SCRIPTCALLBACK:
		return "callback";
#endif
	default:
		return BlueCUnknownVarTypeName( entry );
	}
}

template< typename T >
static bool BlueCExtractRange( const BlueCValue& in, T& result, int64_t minValue, int64_t maxValue, BlueCError& err )
{
	int64_t v = 0;
	if( !BlueCExtractSigned( in, v, minValue, maxValue, "integer", BLUE_CABI_VALUE_INDEX, &err ) )
	{
		return false;
	}
	result = T( v );
	return true;
}

static int32_t BlueCSetObjectPointer( const Be::VarEntry* entry, Be::Var* value, const BlueCValue& in, BlueCError& err )
{
	IRoot* obj = nullptr;
	if( !BlueCExtractObject( in, obj, BLUE_CABI_VALUE_INDEX, &err ) )
	{
		return err.code;
	}

	IRoot* assigned = nullptr;
	if( obj && entry->mIID )
	{
		// Typed member: hold the interface it is declared as (QueryInterface locks it)
		obj->QueryInterface( *entry->mIID, (void**)&assigned, BEQI_NONE );
		if( !assigned )
		{
			return BlueCSetArgumentError( &err, BLUE_E_TYPE, BLUE_CABI_VALUE_INDEX, "object of class %s lacks interface %s",
										  BlueCClassNameOf( obj ), entry->mIID->GetName() );
		}
	}
	else if( obj )
	{
		assigned = obj;
		assigned->Lock();
	}

	IRoot* previous = value->mIRootPtr;
	value->mIRootPtr = assigned;
	if( previous )
	{
		previous->Unlock();
	}
	return BLUE_OK;
}

static int32_t BlueCSetWeakReference( const Be::VarEntry* entry, Be::Var* value, const BlueCValue& in, BlueCError& err )
{
	IRoot* obj = nullptr;
	if( !BlueCExtractObject( in, obj, BLUE_CABI_VALUE_INDEX, &err ) )
	{
		return err.code;
	}
	if( obj && entry->mIID )
	{
		IRootPtr check;
		obj->QueryInterface( *entry->mIID, (void**)&check, BEQI_NONE );
		if( !check )
		{
			return BlueCSetArgumentError( &err, BLUE_E_TYPE, BLUE_CABI_VALUE_INDEX, "object of class %s lacks interface %s",
										  BlueCClassNameOf( obj ), entry->mIID->GetName() );
		}
	}
	*reinterpret_cast<BlueWeakRefBase*>( value ) = obj;
	return BLUE_OK;
}

static int32_t BlueCSetAttribute( IRoot* obj, const Be::VarEntry* entry, ptrdiff_t extraOffset, const BlueCValue& in, BlueCError& err )
{
	if( ( entry->mEditFlags & Be::WRITE ) == 0 )
	{
		return BlueCSetError( &err, BLUE_E_READ_ONLY, "attribute '%s' is read-only", entry->mName );
	}

	Be::Var* value = BeMapMemberOffset( obj, entry, obj->ClassType(), extraOffset );
	bool ok = true;

	switch( entry->mType )
	{
	case Be::LONG:
		if( entry->mSize == 1 )
		{
			ok = BlueCExtractRange( in, *reinterpret_cast<int8_t*>( value ), INT8_MIN, INT8_MAX, err );
		}
		else if( entry->mSize == 2 )
		{
			ok = BlueCExtractRange( in, *reinterpret_cast<int16_t*>( value ), INT16_MIN, INT16_MAX, err );
		}
		else
		{
			ok = BlueCExtract( in, value->mLong, BLUE_CABI_VALUE_INDEX, &err );
		}
		break;
	case Be::ULONG:
		ok = BlueCExtract( in, value->mULong, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::SHORT:
		// short and unsigned short share this VARTYPE
		ok = BlueCExtractRange( in, value->mShort, INT16_MIN, UINT16_MAX, err );
		break;
	case Be::BYTE:
		// char and unsigned char share this VARTYPE
		ok = BlueCExtractRange( in, value->mByte, INT8_MIN, UINT8_MAX, err );
		break;
	case Be::INT64:
		ok = BlueCExtract( in, value->mInt64, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::UINT64:
		ok = BlueCExtract( in, value->mUInt64, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::BOOL:
		ok = BlueCExtract( in, value->mBool, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::FLOAT:
		ok = BlueCExtract( in, value->mFloat, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::DOUBLE:
		ok = BlueCExtract( in, value->mDouble, BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::STDSTRING:
		ok = BlueCExtract( in, *reinterpret_cast<std::string*>( value ), BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::STDWSTRING:
		ok = BlueCExtract( in, *reinterpret_cast<std::wstring*>( value ), BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::SHAREDSTRING:
		ok = BlueCExtract( in, *reinterpret_cast<BlueSharedString*>( value ), BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::SHAREDSTRINGW:
		ok = BlueCExtract( in, *reinterpret_cast<BlueSharedStringW*>( value ), BLUE_CABI_VALUE_INDEX, &err );
		break;
	case Be::CSTRING:
	{
		std::string s;
		ok = BlueCExtractString( in, s, BLUE_CABI_VALUE_INDEX, &err );
		if( ok )
		{
			CCP_FREE( value->mCharPtr );
			value->mCharPtr = CCP_STRDUP( "BlueCSetAttribute", s.c_str() );
		}
		break;
	}
	case Be::WCSTRING:
	{
		std::wstring s;
		ok = BlueCExtractWString( in, s, BLUE_CABI_VALUE_INDEX, &err );
		if( ok )
		{
			CCP_FREE( value->mWCharPtr );
			value->mWCharPtr = CCP_WSTRDUP( "BlueCSetAttribute", s.c_str() );
		}
		break;
	}
	case Be::CHARARRAY:
	{
		std::string s;
		ok = BlueCExtractString( in, s, BLUE_CABI_VALUE_INDEX, &err );
		if( ok && s.size() >= entry->mSize )
		{
			BlueCSetArgumentError( &err, BLUE_E_OVERFLOW, BLUE_CABI_VALUE_INDEX, "string of %zu bytes does not fit in %zu", s.size(), entry->mSize );
			ok = false;
		}
		if( ok )
		{
			memcpy( value, s.c_str(), s.size() + 1 );
		}
		break;
	}
	case Be::FLOATARRAY:
		ok = BlueCExtractNumberArray( in, reinterpret_cast<float*>( value ), entry->GetFloatArraySize(), BlueCVectorTag( entry ), &err );
		break;
	case Be::DOUBLEARRAY:
		ok = BlueCExtractNumberArray( in, reinterpret_cast<double*>( value ), entry->GetDoubleArraySize(), BlueCVectorTag( entry ), &err );
		break;
	case Be::INTARRAY:
		ok = BlueCExtractNumberArray( in, reinterpret_cast<int*>( value ), entry->GetIntArraySize(), BLUE_VEC_TAG_NONE, &err );
		break;
	case Be::IROOTPTR:
		if( BlueCSetObjectPointer( entry, value, in, err ) != BLUE_OK )
		{
			return err.code;
		}
		break;
	case Be::IROOTWEAKREF:
		if( BlueCSetWeakReference( entry, value, in, err ) != BLUE_OK )
		{
			return err.code;
		}
		break;
#if BLUE_CABI_CALLBACKS
	case Be::SCRIPTCALLBACK:
		ok = BlueCExtract( in, *reinterpret_cast<BlueScriptCallback*>( value ), BLUE_CABI_VALUE_INDEX, &err );
		break;
#endif
	default:
		// Includes IROOT: the Python backend copies into embedded objects, which the C ABI
		// does not offer
		return BlueCSetError( &err, BLUE_E_UNSUPPORTED, "attribute '%s' has VARTYPE %d, which cannot be assigned through the C ABI", entry->mName, int( entry->mType ) );
	}

	if( !ok )
	{
		return err.code;
	}

	if( entry->mEditFlags & Be::NOTIFY )
	{
		INotifyPtr notify = BlueCastPtr( obj );
		if( notify && !notify->OnModified( value ) )
		{
			return BlueCSetError( &err, BLUE_E_FAILED, "attribute '%s': OnModified rejected the change", entry->mName );
		}
	}
	return BLUE_OK;
}


//--------------------------------------------------------------------
// Members
//--------------------------------------------------------------------

static const Be::VarEntry* BlueCLookupMember( IRoot* obj, const char* name, size_t nameLength, ptrdiff_t& extraOffset, BlueCError& err )
{
	if( !obj )
	{
		BlueCSetError( &err, BLUE_E_SELF, "member access without an object" );
		return nullptr;
	}
	const Be::VarEntry* entry = BlueCFindMember( obj->ClassType(), name, nameLength, &extraOffset );
	if( !entry )
	{
		BlueCSetError( &err, BLUE_E_NOT_FOUND, "%s has no member '%.*s'", BlueCClassNameOf( obj ), int( name ? nameLength : 0 ), name ? name : "" );
	}
	return entry;
}

int32_t BlueCGetMember( IRoot* obj, const char* name, size_t nameLength, BlueCValue* out, BlueCError* err )
{
	return BlueCAbiDetail::Guard( out, err, [&]( BlueCValue& result, BlueCError& error ) -> int32_t {
		ptrdiff_t extraOffset = 0;
		const Be::VarEntry* entry = BlueCLookupMember( obj, name, nameLength, extraOffset, error );
		if( !entry )
		{
			return error.code;
		}

		if( entry->mCGet || entry->mGetProperty )
		{
			if( !( entry->mEditFlags & Be::READ ) )
			{
				return BlueCSetError( &error, BLUE_E_UNSUPPORTED, "property '%s' is not readable", entry->mName );
			}
			if( !entry->mCGet )
			{
				return BlueCSetError( &error, BLUE_E_UNSUPPORTED, "property '%s' has no C getter", entry->mName );
			}
			return entry->mCGet( reinterpret_cast<BlueObject*>( obj ), &result, &error );
		}
		return BlueCGetAttribute( obj, entry, extraOffset, result, error );
	} );
}

int32_t BlueCSetMember( IRoot* obj, const char* name, size_t nameLength, const BlueCValue* in, BlueCError* err )
{
	return BlueCAbiDetail::Guard( nullptr, err, [&]( BlueCValue&, BlueCError& error ) -> int32_t {
		if( !in )
		{
			return BlueCSetError( &error, BLUE_E_INVALID_ARGUMENT, "no value to set" );
		}
		ptrdiff_t extraOffset = 0;
		const Be::VarEntry* entry = BlueCLookupMember( obj, name, nameLength, extraOffset, error );
		if( !entry )
		{
			return error.code;
		}

		if( entry->mCGet || entry->mGetProperty )
		{
			if( !entry->mCSet )
			{
				return BlueCSetError( &error, BLUE_E_READ_ONLY, "property '%s' is read-only", entry->mName );
			}
			return entry->mCSet( reinterpret_cast<BlueObject*>( obj ), in, &error );
		}
		return BlueCSetAttribute( obj, entry, extraOffset, *in, error );
	} );
}
