#pragma once

 
// Detect platform
#if defined( WIN32 ) || defined( _WINDOWS ) || defined( _WIN32 )
#	if !defined( PLATFORM_WIN )
#		define PLATFORM_WIN
#	endif
#define CORE_WINDOWS
#define CORE_WINDOWS_API
#define PLATFORM_DESKTOP
#elif defined( __ANDROID__ )
#	if !defined( PLATFORM_ANDROID )
#		define PLATFORM_ANDROID
#	endif
#define CORE_LINUX
#define CORE_LINUX_API
#define PLATFORM_MOBILE
#elif defined( __EMSCRIPTEN__ )
#	if !defined( PLATFORM_EMSCRIPTEN )
#		define PLATFORM_EMSCRIPTEN
#	endif
#define CORE_LINUX
#define CORE_LINUX_API
#define PLATFORM_WEB
#else
#	if !defined( PLATFORM_LINUX )
#		define PLATFORM_LINUX
#	endif
#define CORE_LINUX
#define CORE_LINUX_API
#define PLATFORM_DESKTOP
#endif

#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_CORE_DLL
    #ifdef __GNUC__
      #define CORE_PUBLIC __attribute__ ((dllexport))
    #else
      #define CORE_PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define CORE_PUBLIC __attribute__ ((dllimport))
    #else
      #define CORE_PUBLIC __declspec(dllimport)
    #endif
  #endif
  #define CORE_LOCAL
#else
  #if __GNUC__ >= 4
    #define CORE_PUBLIC __attribute__ ((visibility ("default")))
    #define CORE_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define CORE_PUBLIC
    #define CORE_LOCAL
  #endif
#endif


typedef uint8_t   u8;     // 8-bit  unsigned (0 to 255)
typedef uint16_t  u16;    // 16-bit unsigned (0 to 65,535)
typedef uint32_t  u32;    // 32-bit unsigned (0 to 4,294,967,295)
typedef uint64_t  u64;    // 64-bit unsigned (0 to 18,446,744,073,709,551,615)

typedef int8_t    s8;     // 8-bit  signed (-128 to 127)
typedef int16_t   s16;    // 16-bit signed (-32,768 to 32,767)
typedef int32_t   s32;    // 32-bit signed (-2,147,483,648 to 2,147,483,647)
typedef int64_t   s64;    // 64-bit signed

typedef char      c8;     // Character (8-bit)


typedef float     f32;    // 32-bit float (IEEE 754)
typedef double    f64;    // 64-bit float (IEEE 754)

#define DEBUG

#if defined(DEBUG)
	#include <assert.h>
	#define DEBUG_BREAK_IF( _CONDITION_ ) SDL_assert( !(_CONDITION_) );
#else
	#define DEBUG_BREAK_IF( _CONDITION_ )
#endif


#define VALIDATE_ASSERT(condition, message) \
    {                                              \
        if (!(condition))                          \
        {                                          \
            SDL_assert(condition && message);          \
        }                                          \
    }

 #define GLSL(src) "#version 300 es\n precision highp float;\n precision highp int;\n" #src