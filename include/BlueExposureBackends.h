// Copyright (c) 2026 CCP Games
//
// Normalises the script backend switches. After this header both BLUE_WITH_PYTHON and
// BLUE_WITH_CABI are defined to 0 or 1, so they can be tested with #if.
//
// The CMake build always defines both. When neither is defined (and neither is the legacy
// BLUE_NO_EXPOSURE), Python is assumed, as before the C-ABI backend existed. With both off,
// BLUE_NO_EXPOSURE is defined so headers fall back to the stubs they had for it.

#pragma once
#ifndef BlueExposureBackends_h
#define BlueExposureBackends_h

#if !defined( BLUE_WITH_PYTHON )
#if defined( BLUE_NO_EXPOSURE ) || defined( BLUE_WITH_CABI )
#define BLUE_WITH_PYTHON 0
#else
#define BLUE_WITH_PYTHON 1
#endif
#endif

#if !defined( BLUE_WITH_CABI )
#define BLUE_WITH_CABI 0
#endif

#if !BLUE_WITH_PYTHON && !BLUE_WITH_CABI
#if !defined( BLUE_NO_EXPOSURE )
#define BLUE_NO_EXPOSURE 1
#endif
#elif defined( BLUE_NO_EXPOSURE ) && BLUE_NO_EXPOSURE
#error "BLUE_NO_EXPOSURE cannot be combined with BLUE_WITH_PYTHON or BLUE_WITH_CABI"
#endif

#endif // BlueExposureBackends_h
