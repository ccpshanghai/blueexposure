// Copyright (c) 2026 CCP Games
//
// C-ABI backend: the name of a C++ type as the C ABI sees it, for describing methods, functions
// and members to bindings (signatures, metadata).
//
//   BlueCTypeName<T>::Get()        the name, a NUL-terminated string that lives as long as the
//                                  process (built once; composed names are interned, but compare
//                                  names with strcmp: a fixed one may be a separate literal)
//   BlueCIsConvertible<T>          whether T converts, deeply: a container converts only if its
//                                  items do (BlueCIsSupported<T> looks at T alone)
//
// T is a type without const and reference, as for BlueCConvert<T>. The specialisations follow
// BlueCConvert's, so a type has a name exactly when it has a conversion; every other type is
// "unknown:" followed by its demangled C++ name. A module that adds a BlueCConvert
// specialisation adds a BlueCTypeName one next to it.
//
// The names (a closed vocabulary; no spaces, except inside an unknown name):
//   void                                   only as a return type
//   bool
//   int8 int16 int32 int64                 signed integers by size; enums, as the signed integer
//                                          of their underlying type's size (they are INT values)
//   uint8 uint16 uint32 uint64             unsigned integers by size
//   float double
//   string                                 every narrow and wide string type
//   bytes                                  BYTES; no C++ type has it, only hand-written signatures
//   vector2 vector3 vector4 vector2d vector3d vector4d vector3i color quaternion matrix
//   callback                               BlueScriptCallback, when C-ABI is the only backend
//   object:<IID>                           pointer or smart pointer to a Blue class or interface;
//                                          always nullable. <IID> is its IID name (IList,
//                                          IBlueDict or IBlueStructureList for the list types)
//   list<T>                                std::vector, std::list
//   map<K,V>                               std::map
//   tuple<A,B>                             std::pair
//   optional<T>                            Be::Optional<T>, Be::OptionalWithDefaultValue<T, v>
//   unknown:<C++ type>                     no (deep) C-ABI conversion

#pragma once
#ifndef BlueCAbiTypeName_h
#define BlueCAbiTypeName_h

#include "BlueCAbiValue.h"
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#define BLUE_CABI_UNKNOWN_TYPE_PREFIX "unknown:"

// name, kept for the life of the process; the same text always gives the same pointer
BLUEIMPORT const char* BlueCInternTypeName( const std::string& name );

// "unknown:" and the demangled typeid name, interned. Spaces next to punctuation are dropped
// ("std::vector<Foo,std::allocator<Foo>>"); those between two words stay ("unsigned int").
BLUEIMPORT const char* BlueCUnknownTypeName( const char* typeidName );

// prefix, then parts[0, count) separated by commas, then suffix; interned
BLUEIMPORT const char* BlueCComposeTypeName( const char* prefix, const char* const* parts, size_t count, const char* suffix );

// "object:" and the IID name (IRoot for null), interned
BLUEIMPORT const char* BlueCObjectTypeName( const Be::IID* iid );

// Whether a type name is a known one (not "unknown:...")
inline bool BlueCIsKnownTypeName( const char* name )
{
	return name && strncmp( name, BLUE_CABI_UNKNOWN_TYPE_PREFIX, sizeof( BLUE_CABI_UNKNOWN_TYPE_PREFIX ) - 1 ) != 0;
}


//--------------------------------------------------------------------
// BlueCTypeName
//--------------------------------------------------------------------

// Fallback: no C-ABI conversion
template< typename T, typename Enable = void >
struct BlueCTypeName
{
	static const bool convertible = false;

	static const char* Get()
	{
		static const char* const s_name = BlueCUnknownTypeName( typeid( T ).name() );
		return s_name;
	}
};

template< typename T >
struct BlueCIsConvertible : std::integral_constant<bool, BlueCTypeName<T>::convertible>
{
};

// Base of the names of convertible types
struct BlueCKnownTypeName
{
	static const bool convertible = true;
};

template<> struct BlueCTypeName< void > : BlueCKnownTypeName { static const char* Get() { return "void"; } };
template<> struct BlueCTypeName< bool > : BlueCKnownTypeName { static const char* Get() { return "bool"; } };

// Integers, as BlueCConvert splits them: by signedness, then size
template< bool isSigned, size_t size >
struct BlueCIntegerTypeName
{
	static const char* Get()
	{
		if( size == 1 )
		{
			return isSigned ? "int8" : "uint8";
		}
		if( size == 2 )
		{
			return isSigned ? "int16" : "uint16";
		}
		if( size == 4 )
		{
			return isSigned ? "int32" : "uint32";
		}
		return isSigned ? "int64" : "uint64";
	}
};

template< typename T >
struct BlueCTypeName< T, typename std::enable_if< std::is_integral<T>::value && !std::is_same<T, bool>::value >::type >
	: BlueCKnownTypeName, BlueCIntegerTypeName< std::is_signed<T>::value, sizeof( T ) >
{
};

// BlueCConvert wraps every enum as a signed integer (INT)
template< typename T >
struct BlueCTypeName< T, typename std::enable_if< std::is_enum<T>::value >::type >
	: BlueCKnownTypeName, BlueCIntegerTypeName< true, sizeof( typename std::underlying_type<T>::type ) >
{
};

template< typename T >
struct BlueCTypeName< T, typename std::enable_if< std::is_floating_point<T>::value >::type > : BlueCKnownTypeName
{
	static const char* Get() { return sizeof( T ) < sizeof( double ) ? "float" : "double"; }
};


//--------------------------------------------------------------------
// Strings
//--------------------------------------------------------------------

struct BlueCStringTypeName : BlueCKnownTypeName
{
	static const char* Get() { return "string"; }
};

template<> struct BlueCTypeName< std::string > : BlueCStringTypeName {};
template<> struct BlueCTypeName< BlueCStringArg > : BlueCStringTypeName {};
template<> struct BlueCTypeName< const char* > : BlueCStringTypeName {};
template<> struct BlueCTypeName< char* > : BlueCStringTypeName {};
template<> struct BlueCTypeName< std::wstring > : BlueCStringTypeName {};
template<> struct BlueCTypeName< CastableStdWstring > : BlueCStringTypeName {};
template<> struct BlueCTypeName< const wchar_t* > : BlueCStringTypeName {};
template<> struct BlueCTypeName< wchar_t* > : BlueCStringTypeName {};
template<> struct BlueCTypeName< BlueSharedString > : BlueCStringTypeName {};
template<> struct BlueCTypeName< BlueSharedStringW > : BlueCStringTypeName {};


//--------------------------------------------------------------------
// Vector types
//--------------------------------------------------------------------

template<> struct BlueCTypeName< Vector2 > : BlueCKnownTypeName { static const char* Get() { return "vector2"; } };
template<> struct BlueCTypeName< Vector3 > : BlueCKnownTypeName { static const char* Get() { return "vector3"; } };
template<> struct BlueCTypeName< Vector4 > : BlueCKnownTypeName { static const char* Get() { return "vector4"; } };
template<> struct BlueCTypeName< Vector2d > : BlueCKnownTypeName { static const char* Get() { return "vector2d"; } };
template<> struct BlueCTypeName< Vector3d > : BlueCKnownTypeName { static const char* Get() { return "vector3d"; } };
template<> struct BlueCTypeName< Vector4d > : BlueCKnownTypeName { static const char* Get() { return "vector4d"; } };
template<> struct BlueCTypeName< Vector3i > : BlueCKnownTypeName { static const char* Get() { return "vector3i"; } };
template<> struct BlueCTypeName< Color > : BlueCKnownTypeName { static const char* Get() { return "color"; } };
template<> struct BlueCTypeName< Quaternion > : BlueCKnownTypeName { static const char* Get() { return "quaternion"; } };
template<> struct BlueCTypeName< Matrix > : BlueCKnownTypeName { static const char* Get() { return "matrix"; } };


//--------------------------------------------------------------------
// Blue objects and callbacks
//--------------------------------------------------------------------

// Pointer to a Blue class or interface; the IID is the one MAP_ATTRIBUTE records for it
template< typename T >
struct BlueCTypeName< T, typename std::enable_if< is_pointer_to_blue<T>::type::value >::type > : BlueCKnownTypeName
{
	static const char* Get()
	{
		static const char* const s_name = BlueCObjectTypeName( GetBlueIID<T>() );
		return s_name;
	}
};

template< typename T > struct BlueCTypeName< BlueBasicPtr<T> > : BlueCTypeName< T* > {};
template< typename T > struct BlueCTypeName< BluePtr<T> > : BlueCTypeName< T* > {};

#if BLUE_CABI_CALLBACKS
template<> struct BlueCTypeName< BlueScriptCallback > : BlueCKnownTypeName { static const char* Get() { return "callback"; } };
#endif


//--------------------------------------------------------------------
// Containers: known only when every item type is
//--------------------------------------------------------------------

// Container's name, as prefix<Items...>, or unknown:<Container> if an item type is unknown
template< typename Container, typename... Items >
struct BlueCContainerTypeName
{
	static const bool convertible = ( BlueCTypeName<Items>::convertible && ... );

	static const char* Get( const char* prefix )
	{
		if( !convertible )
		{
			return BlueCUnknownTypeName( typeid( Container ).name() );
		}
		const char* const parts[] = { BlueCTypeName<Items>::Get()... };
		return BlueCComposeTypeName( prefix, parts, sizeof...( Items ), ">" );
	}
};

template< typename T, typename A >
struct BlueCTypeName< std::vector<T, A> > : BlueCContainerTypeName< std::vector<T, A>, T >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< std::vector<T, A>, T >::Get( "list<" );
		return s_name;
	}
};

template< typename T, typename A >
struct BlueCTypeName< std::list<T, A> > : BlueCContainerTypeName< std::list<T, A>, T >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< std::list<T, A>, T >::Get( "list<" );
		return s_name;
	}
};

template< typename T1, typename T2 >
struct BlueCTypeName< std::pair<T1, T2> > : BlueCContainerTypeName< std::pair<T1, T2>, T1, T2 >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< std::pair<T1, T2>, T1, T2 >::Get( "tuple<" );
		return s_name;
	}
};

template< typename K, typename V, typename C, typename A >
struct BlueCTypeName< std::map<K, V, C, A> > : BlueCContainerTypeName< std::map<K, V, C, A>, K, V >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< std::map<K, V, C, A>, K, V >::Get( "map<" );
		return s_name;
	}
};

template< typename T >
struct BlueCTypeName< Be::Optional<T> > : BlueCContainerTypeName< Be::Optional<T>, T >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< Be::Optional<T>, T >::Get( "optional<" );
		return s_name;
	}
};

template< typename T, T defaultValue >
struct BlueCTypeName< Be::OptionalWithDefaultValue<T, defaultValue> > : BlueCContainerTypeName< Be::OptionalWithDefaultValue<T, defaultValue>, T >
{
	static const char* Get()
	{
		static const char* const s_name = BlueCContainerTypeName< Be::OptionalWithDefaultValue<T, defaultValue>, T >::Get( "optional<" );
		return s_name;
	}
};

#endif // BlueCAbiTypeName_h
