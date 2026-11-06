// Copyright (c) 2026 CCP Games
//
// libblue_invoke: the process-wide Invoke registry (BlueInvoke.h). One shared library per process;
// a static copy would give each DSO its own empty registry. No dependency on CcpCore or Python.

#define BLUE_INVOKE_BUILDING 1
#define BLUE_INVOKE_C_API_ONLY 1
#include "BlueInvoke.h"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	struct Entry
	{
		std::string classPath;
		const void* classInfo;
		std::string capability;
		const void* table;
	};

	struct Registry
	{
		std::mutex m;
		std::vector<const Entry*> order;                                  // registration order
		std::unordered_map<std::string, const Entry*> byKey;              // classPath '\0' capability
		std::map<std::pair<const void*, std::string>, const Entry*> byInfo;
		std::vector<const Entry*> duplicates;                             // first wins; recorded here
	};

	// Deliberately leaked: no static-init-order problem, never destroyed (callers may run at exit).
	Registry& R()
	{
		static Registry& r = *new Registry;
		return r;
	}

	std::string Key( const char* classPath, const char* capability )
	{
		std::string k( classPath );
		k.push_back( '\0' );
		k.append( capability );
		return k;
	}
}

extern "C" {

BLUE_INVOKE_API int BlueInvokeRegister( const char* classPath, const void* classInfo, const char* capability, const void* table )
{
	if( !classPath || !capability || !table )
	{
		return BLUE_INVOKE_REGISTER_INVALID;
	}
	Registry& r = R();
	std::lock_guard<std::mutex> lock( r.m );
	const Entry* e = new Entry{ classPath, classInfo, capability, table };
	auto inserted = r.byKey.emplace( Key( classPath, capability ), e );
	if( !inserted.second )
	{
		r.duplicates.push_back( e );
		// A duplicate path with its own ClassInfo (another DSO's class) stays reachable by that info.
		if( classInfo )
		{
			r.byInfo.emplace( std::make_pair( classInfo, e->capability ), e );
		}
		return BLUE_INVOKE_REGISTER_DUPLICATE;
	}
	r.order.push_back( e );
	if( classInfo )
	{
		r.byInfo.emplace( std::make_pair( classInfo, e->capability ), e );
	}
	return BLUE_INVOKE_REGISTER_OK;
}

BLUE_INVOKE_API const void* BlueInvokeFind( const char* classPath, const char* capability )
{
	if( !classPath || !capability )
	{
		return nullptr;
	}
	Registry& r = R();
	std::lock_guard<std::mutex> lock( r.m );
	auto it = r.byKey.find( Key( classPath, capability ) );
	return it == r.byKey.end() ? nullptr : it->second->table;
}

BLUE_INVOKE_API const void* BlueInvokeFindByInfo( const void* classInfo, const char* capability )
{
	if( !classInfo || !capability )
	{
		return nullptr;
	}
	Registry& r = R();
	std::lock_guard<std::mutex> lock( r.m );
	auto it = r.byInfo.find( std::make_pair( classInfo, std::string( capability ) ) );
	return it == r.byInfo.end() ? nullptr : it->second->table;
}

namespace
{
	void Visit( std::vector<const Entry*> Registry::*list, BlueInvokeForEachFn fn, void* user )
	{
		if( !fn )
		{
			return;
		}
		std::vector<const Entry*> snapshot;
		{
			Registry& r = R();
			std::lock_guard<std::mutex> lock( r.m );
			snapshot = r.*list;
		}
		// Called without the lock, so fn may call BlueInvokeFind. Entries are never freed.
		for( const Entry* e : snapshot )
		{
			fn( user, e->classPath.c_str(), e->classInfo, e->capability.c_str(), e->table );
		}
	}
}

BLUE_INVOKE_API void BlueInvokeForEach( BlueInvokeForEachFn fn, void* user )
{
	Visit( &Registry::order, fn, user );
}

BLUE_INVOKE_API void BlueInvokeForEachDuplicate( BlueInvokeForEachFn fn, void* user )
{
	Visit( &Registry::duplicates, fn, user );
}

BLUE_INVOKE_API const char* BlueInvokeBuildId( void )
{
	return BLUE_INVOKE_BUILD_ID;
}

BLUE_INVOKE_API uint32_t BlueInvokeDuplicateCount( void )
{
	Registry& r = R();
	std::lock_guard<std::mutex> lock( r.m );
	return static_cast<uint32_t>( r.duplicates.size() );
}

} // extern "C"
