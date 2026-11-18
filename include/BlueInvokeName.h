// Copyright (c) 2026 CCP Games
//
// BlueInvokeName.h: compile-time canonical type names for the Invoke hook (BlueInvoke.h).
//
// A type's canonical name is the name the hook hashes into sigHash and reports as argTypeNames,
// property typeName, BlueInvokeValue::type and enum table typeName. It is declared in the source,
// never derived from compiler-generated text (function signatures, typeid), so every compiler
// produces the same names and hashes.
//
//   BLUE_INVOKE_CANONICAL_NAME( Type, "Name" );
//
// declares it. It is a function declaration (never defined, only used in decltype), so it may
// repeat in one TU and is found by argument-dependent lookup wherever the hook needs it: write it
// at namespace scope after the type's declaration. Blue's forward-declaration macros
// (BLUE_DECLARE, BLUE_CLASS, BLUE_INTERFACE, BLUE_DECLARE_INTERFACE*, BlueTypes.h) already declare
// one for every Blue class and interface, named by the macro argument as Blue registers it. An
// enum used in a hooked method or property signature declares its own, spelled qualified (e.g.
// "Tr2CurveInterpolation::Type"); a hooked signature with an undeclared enum or Blue type is a
// compile error (BlueInvokeCanonicalName in BlueInvoke.h).
//
// C++17: the name travels as a char pack, at most BLUE_INVOKE_NAME_MAX - 1 characters.

#pragma once
#ifndef BlueInvokeName_h
#define BlueInvokeName_h

#include <cstddef>

// The exact type a name belongs to; no conversions, so a derived class never takes its base's name.
template <typename T>
struct BlueInvokeTypeTag
{
};

// The result of BlueInvokeCanonicalNameOf when no name is declared for the type.
struct BlueInvokeNoName
{
};
BlueInvokeNoName BlueInvokeCanonicalNameOf( ... );

template <char... C>
struct BlueInvokeNameChars
{
	static constexpr char value[sizeof...( C ) + 1] = { C..., '\0' };
};

#define BLUE_INVOKE_NAME_MAX 64

// Character i of string literal s, NUL past its end (the index is clamped so it stays in bounds).
#define BLUE_INVOKE_NAME_CH( s, i ) ( ( i ) < sizeof( s ) ? ( s )[( i ) < sizeof( s ) ? ( i ) : 0] : '\0' )

#define BLUE_INVOKE_NAME_CHARS( s ) \
	BLUE_INVOKE_NAME_CH( s, 0 ), BLUE_INVOKE_NAME_CH( s, 1 ), BLUE_INVOKE_NAME_CH( s, 2 ), BLUE_INVOKE_NAME_CH( s, 3 ), \
	BLUE_INVOKE_NAME_CH( s, 4 ), BLUE_INVOKE_NAME_CH( s, 5 ), BLUE_INVOKE_NAME_CH( s, 6 ), BLUE_INVOKE_NAME_CH( s, 7 ), \
	BLUE_INVOKE_NAME_CH( s, 8 ), BLUE_INVOKE_NAME_CH( s, 9 ), BLUE_INVOKE_NAME_CH( s, 10 ), BLUE_INVOKE_NAME_CH( s, 11 ), \
	BLUE_INVOKE_NAME_CH( s, 12 ), BLUE_INVOKE_NAME_CH( s, 13 ), BLUE_INVOKE_NAME_CH( s, 14 ), BLUE_INVOKE_NAME_CH( s, 15 ), \
	BLUE_INVOKE_NAME_CH( s, 16 ), BLUE_INVOKE_NAME_CH( s, 17 ), BLUE_INVOKE_NAME_CH( s, 18 ), BLUE_INVOKE_NAME_CH( s, 19 ), \
	BLUE_INVOKE_NAME_CH( s, 20 ), BLUE_INVOKE_NAME_CH( s, 21 ), BLUE_INVOKE_NAME_CH( s, 22 ), BLUE_INVOKE_NAME_CH( s, 23 ), \
	BLUE_INVOKE_NAME_CH( s, 24 ), BLUE_INVOKE_NAME_CH( s, 25 ), BLUE_INVOKE_NAME_CH( s, 26 ), BLUE_INVOKE_NAME_CH( s, 27 ), \
	BLUE_INVOKE_NAME_CH( s, 28 ), BLUE_INVOKE_NAME_CH( s, 29 ), BLUE_INVOKE_NAME_CH( s, 30 ), BLUE_INVOKE_NAME_CH( s, 31 ), \
	BLUE_INVOKE_NAME_CH( s, 32 ), BLUE_INVOKE_NAME_CH( s, 33 ), BLUE_INVOKE_NAME_CH( s, 34 ), BLUE_INVOKE_NAME_CH( s, 35 ), \
	BLUE_INVOKE_NAME_CH( s, 36 ), BLUE_INVOKE_NAME_CH( s, 37 ), BLUE_INVOKE_NAME_CH( s, 38 ), BLUE_INVOKE_NAME_CH( s, 39 ), \
	BLUE_INVOKE_NAME_CH( s, 40 ), BLUE_INVOKE_NAME_CH( s, 41 ), BLUE_INVOKE_NAME_CH( s, 42 ), BLUE_INVOKE_NAME_CH( s, 43 ), \
	BLUE_INVOKE_NAME_CH( s, 44 ), BLUE_INVOKE_NAME_CH( s, 45 ), BLUE_INVOKE_NAME_CH( s, 46 ), BLUE_INVOKE_NAME_CH( s, 47 ), \
	BLUE_INVOKE_NAME_CH( s, 48 ), BLUE_INVOKE_NAME_CH( s, 49 ), BLUE_INVOKE_NAME_CH( s, 50 ), BLUE_INVOKE_NAME_CH( s, 51 ), \
	BLUE_INVOKE_NAME_CH( s, 52 ), BLUE_INVOKE_NAME_CH( s, 53 ), BLUE_INVOKE_NAME_CH( s, 54 ), BLUE_INVOKE_NAME_CH( s, 55 ), \
	BLUE_INVOKE_NAME_CH( s, 56 ), BLUE_INVOKE_NAME_CH( s, 57 ), BLUE_INVOKE_NAME_CH( s, 58 ), BLUE_INVOKE_NAME_CH( s, 59 ), \
	BLUE_INVOKE_NAME_CH( s, 60 ), BLUE_INVOKE_NAME_CH( s, 61 ), BLUE_INVOKE_NAME_CH( s, 62 ), BLUE_INVOKE_NAME_CH( s, 63 )

// BLUE_INVOKE_CANONICAL_NAME( Type, "Name" ): see the top of this file.
#define BLUE_INVOKE_CANONICAL_NAME( T, NAME ) \
	static_assert( sizeof( NAME ) <= BLUE_INVOKE_NAME_MAX, "BlueInvoke: canonical name of " #T " is too long (BLUE_INVOKE_NAME_MAX)" ); \
	BlueInvokeNameChars<BLUE_INVOKE_NAME_CHARS( NAME )> BlueInvokeCanonicalNameOf( BlueInvokeTypeTag<T> )

#endif // BlueInvokeName_h
