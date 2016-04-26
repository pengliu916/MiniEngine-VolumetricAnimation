#pragma once
#include <windows.h>
#include <string>
#include <stdlib.h>
#include <io.h>
#include <iostream>
#include <fcntl.h>
#include <thread>
#include <comdef.h>
#include <tchar.h>

#include "MsgPrinting.h"

#pragma warning(disable: 4996)

#define STRINGIFY(x) #x
#define __FILENAME__ (wcsrchr (_T(__FILE__), L'\\') ? wcsrchr (_T(__FILE__), L'\\') + 1 : _T(__FILE__))

#define ARRAY_COUNT(X) (sizeof(X)/sizeof((X)[0]))

inline void SetThreadName( const char* Name )
{
	// http://msdn.microsoft.com/en-us/library/xcb2z8hs(v=vs.110).aspx
#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // must be 0x1000
		LPCSTR szName; // pointer to name (in user addr space)
		DWORD dwThreadID; // thread ID (-1=caller thread)
		DWORD dwFlags; // reserved for future use, must be zero
	} THREADNAME_INFO;
#pragma pack(pop)

	THREADNAME_INFO info;
	{
		info.dwType = 0x1000;
		info.szName = Name;
		info.dwThreadID = (DWORD)-1;
		info.dwFlags = 0;
	}
	__try
	{
		RaiseException( 0x406D1388, 0, sizeof( info ) / sizeof( ULONG_PTR ), (ULONG_PTR*)&info );
	}
	__except (EXCEPTION_CONTINUE_EXECUTION)
	{
	}
}

template <typename T> __forceinline bool IsAligned( T value, size_t alignment )
{
	return 0 == ((size_t)value & (alignment - 1));
}
template <typename T> __forceinline T AlignUpWithMask( T value, size_t mask )
{
	return (T)(((size_t)value + mask) & ~mask);
}

template <typename T> __forceinline T AlignDownWithMask( T value, size_t mask )
{
	return (T)((size_t)value & ~mask);
}

template <typename T> __forceinline T AlignUp( T value, size_t alignment )
{
	return AlignUpWithMask( value, alignment - 1 );
}

template <typename T> __forceinline T AlignDown( T value, size_t alignment )
{
	return AlignDownWithMask( value, alignment - 1 );
}

template <typename T> __forceinline T DivideByMultiple( T value, size_t alignment )
{
	return (T)((value + alignment - 1) / alignment);
}

class CriticalSectionScope
{
public:
	explicit CriticalSectionScope( CRITICAL_SECTION *cs ) :m_cs( cs ) { EnterCriticalSection( m_cs ); }
	~CriticalSectionScope() { LeaveCriticalSection( m_cs ); }
	CriticalSectionScope( CriticalSectionScope const & ) = delete;
	CriticalSectionScope& operator=( CriticalSectionScope const& ) = delete;
private:
	CRITICAL_SECTION *m_cs;
};

class thread_guard
{
public:
	thread_guard( std::thread& _t ) :t( _t ) {}
	~thread_guard() { if (t.joinable())t.join(); }

	thread_guard( thread_guard const& ) = delete;
	thread_guard& operator=( thread_guard const& ) = delete;
private:
	std::thread& t;
};

#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x) { hr = (x); if( FAILED(hr) ) { Trace( __FILENAME__, (DWORD)__LINE__, hr, L###x); __debugbreak(); } }
#endif //#ifndef V
#ifndef VRET
#define VRET(x) { hr = (x); if( FAILED(hr) ) { return Trace( __FILENAME__, (DWORD)__LINE__, hr, L###x); __debugbreak(); } }
#endif //#ifndef VRET
#else
#ifndef V
#define V(x)           { hr = (x); }
#endif //#ifndef V
#ifndef VRET
#define VRET(x)           { hr = (x); if( FAILED(hr) ) { return hr; } }
#endif //#ifndef VRET
#endif //#if defined(DEBUG) || defined(_DEBUG)

inline HRESULT Trace( const wchar_t* strFile, DWORD dwLine, HRESULT hr, const wchar_t* strMsg )
{
	wchar_t szBuffer[MsgPrinting::MAX_MSG_LENGTH];
	int offset = 0;
	if (strFile) offset += wsprintf( szBuffer, L"line %u in file %s\n", dwLine, strFile );

	offset += wsprintf( szBuffer + offset, L"Calling: %s failed!\n ", strMsg );
	_com_error err( hr );
	wsprintf( szBuffer + offset, err.ErrorMessage() );
	PRINTERROR( szBuffer );
	return hr;
}

#ifdef ASSERT
#undef ASSERT
#endif


#if defined(RELEASE)
#define ASSERT(isTrue)
#else
#define ASSERT(isTrue) \
	if(!(bool)(isTrue)){ \
		PRINTERROR("Assertion failed in" STRINGIFY(__FILENAME__) " @ " STRINGIFY(__LINE__)"\n \t \'"#isTrue"\' is false."); \
		__debugbreak(); \
	}
#endif

//--------------------------------------------------------------------------------------
// Profiling/instrumentation support
//--------------------------------------------------------------------------------------
#define  DXDebugName(x)  DX_SetDebugName(x.Get(),L###x)
// Use DX_SetDebugName() to attach names to D3D objects for use by 
// SDKDebugLayer, PIX's object table, etc.
#if defined(PROFILE) || defined(DEBUG)
inline void DX_SetDebugName( _In_ IDXGIObject* pObj, _In_z_ const WCHAR* pwcsName )
{
	if (pObj) pObj->SetPrivateData( WKPDID_D3DDebugObjectNameW, (uint32_t)wcslen( pwcsName ) * 2, pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12Device* pObj, _In_z_ const WCHAR* pwcsName )
{
	if (pObj) pObj->SetName( pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12Resource* pObj, _In_z_ const WCHAR* pwcsName )
{
	if (pObj) pObj->SetName( pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12DeviceChild* pObj, _In_z_ const WCHAR* pwcsName )
{
	if (pObj)pObj->SetName( pwcsName );
}
#else
#define DX_SetDebugName( pObj, pwcsName )
#endif

//--------------------------------------------------------------------------------------
// FNV-1 Hash (32bit)
//--------------------------------------------------------------------------------------
inline size_t HashIterate( size_t Next, size_t CurrentHash = 2166136261U /*Offset for 32bit version*/ )
{
	return 16777619U/*32bit FNV_Prim*/ * CurrentHash ^ Next;
}

template<typename T> inline size_t HashRange( const T* Begin, const T* End, size_t InitialVal = 2166136261U )
{
	size_t Val = InitialVal;
	while (Begin < End)
		Val = HashIterate( (size_t)*Begin++, Val );
	return Val;
}

template<typename T> inline size_t HashStateArray( const T* StateDesc, size_t Count, size_t InitialVal = 2166136261U )
{
	static_assert((sizeof( T ) & 3) == 0, "State object is not word-aligned");
	return HashRange( (UINT*)StateDesc, (UINT*)(StateDesc + Count), InitialVal );
}

template<typename T> inline size_t HashState( const T* StateDesc, size_t InitialVal = 2166136261U )
{
	static_assert((sizeof( T ) & 3) == 0, "State object is not word-aligned");
	return HashRange( (UINT*)StateDesc, (UINT*)(StateDesc + 1), InitialVal );
}