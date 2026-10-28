/* Copyright (c) 2026 CCP Games
 *
 * Compiled as C11: BlueCAbi.h must be valid C, and its layout as the C compiler sees it must
 * match the C++ one (TestCAbiValues.cpp compares them). */

#include "BlueCAbi.h"

#include <stddef.h>

enum
{
	LAYOUT_VALUE_SIZE,
	LAYOUT_VALUE_KIND,
	LAYOUT_VALUE_AUX,
	LAYOUT_VALUE_U,
	LAYOUT_VALUE_STR_LEN,
	LAYOUT_VALUE_CALLBACK,
	LAYOUT_VALUE_VEC,
	LAYOUT_VALUE_ARR_COUNT,
	LAYOUT_VALUE_MAP_COUNT,
	LAYOUT_ERROR_SIZE,
	LAYOUT_ERROR_MESSAGE,
	LAYOUT_ERROR_MESSAGE_LEN,
	LAYOUT_METHOD_DEF_SIZE,
	LAYOUT_CALLBACK_SIZE,
	LAYOUT_COUNT
};

size_t BlueCAbiTestCLayout( size_t* out, size_t capacity )
{
	size_t layout[LAYOUT_COUNT];
	size_t i;

	layout[LAYOUT_VALUE_SIZE] = sizeof( BlueCValue );
	layout[LAYOUT_VALUE_KIND] = offsetof( BlueCValue, kind );
	layout[LAYOUT_VALUE_AUX] = offsetof( BlueCValue, aux );
	layout[LAYOUT_VALUE_U] = offsetof( BlueCValue, u );
	layout[LAYOUT_VALUE_STR_LEN] = offsetof( BlueCValue, u.str.len );
	layout[LAYOUT_VALUE_CALLBACK] = offsetof( BlueCValue, u.callback );
	layout[LAYOUT_VALUE_VEC] = offsetof( BlueCValue, u.vec.v );
	layout[LAYOUT_VALUE_ARR_COUNT] = offsetof( BlueCValue, u.arr.count );
	layout[LAYOUT_VALUE_MAP_COUNT] = offsetof( BlueCValue, u.map.count );
	layout[LAYOUT_ERROR_SIZE] = sizeof( BlueCError );
	layout[LAYOUT_ERROR_MESSAGE] = offsetof( BlueCError, message );
	layout[LAYOUT_ERROR_MESSAGE_LEN] = offsetof( BlueCError, message_len );
	layout[LAYOUT_METHOD_DEF_SIZE] = sizeof( BlueCMethodDef );
	layout[LAYOUT_CALLBACK_SIZE] = sizeof( BlueCCallback );

	for( i = 0; i < LAYOUT_COUNT && i < capacity; ++i )
	{
		out[i] = layout[i];
	}
	return LAYOUT_COUNT;
}

/* Arguments built by C code, to call a thunk with: ( string, number ) */
void BlueCAbiTestCMakeArguments( BlueCValue* out, const char* text, size_t textLength, double number )
{
	out[0].kind = BLUE_VK_STRING;
	out[0].aux = 0;
	out[0].u.str.ptr = text;
	out[0].u.str.len = textLength;

	out[1].kind = BLUE_VK_DOUBLE;
	out[1].aux = 0;
	out[1].u.d = number;
}
