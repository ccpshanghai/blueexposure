// Copyright (c) 2026 CCP Games
//
// C-ABI backend: the composed type names of BlueCAbiTypeName.h, kept for the life of the process.

#include "include/BlueExposure.h"

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_set>

const char* BlueCInternTypeName( const std::string& name )
{
	// Never destroyed: names are handed out for the life of the process, so they must outlive
	// static destruction too. Set nodes keep their address when the set grows.
	static std::mutex* s_lock = new std::mutex;
	static std::unordered_set<std::string>* s_names = new std::unordered_set<std::string>;

	std::lock_guard<std::mutex> lock( *s_lock );
	return s_names->insert( name ).first->c_str();
}

static bool BlueCIsWordCharacter( char c )
{
	return std::isalnum( static_cast<unsigned char>( c ) ) || c == '_';
}

const char* BlueCUnknownTypeName( const char* typeidName )
{
	const std::string demangled = BlueCDemangle( typeidName );
	std::string name = BLUE_CABI_UNKNOWN_TYPE_PREFIX;
	for( size_t i = 0; i < demangled.size(); ++i )
	{
		const char c = demangled[i];
		if( c == ' ' && !( i > 0 && i + 1 < demangled.size() && BlueCIsWordCharacter( demangled[i - 1] ) && BlueCIsWordCharacter( demangled[i + 1] ) ) )
		{
			continue;
		}
		name += c;
	}
	return BlueCInternTypeName( name );
}

const char* BlueCComposeTypeName( const char* prefix, const char* const* parts, size_t count, const char* suffix )
{
	std::string name = prefix ? prefix : "";
	for( size_t i = 0; i < count; ++i )
	{
		if( i )
		{
			name += ',';
		}
		name += parts[i] ? parts[i] : "";
	}
	name += suffix ? suffix : "";
	return BlueCInternTypeName( name );
}

const char* BlueCObjectTypeName( const Be::IID* iid )
{
	const char* name = iid ? iid->GetName() : nullptr;
	return BlueCInternTypeName( std::string( "object:" ) + ( name ? name : "IRoot" ) );
}
