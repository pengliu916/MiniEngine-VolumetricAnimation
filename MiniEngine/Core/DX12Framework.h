#pragma once
// To use this framework, create new project in this solution
// 1. right click new project's References and add UtilityLibrary as reference
// 2. open project property and open configuration manager delete all 
//    Win32 configurations and for each configuration only build 
//    UtilityLibrary and the new project not any others
// 3. go to Configuration Properties -> General, change Target Platform
//    Version to 10.0+
// 4. go to C/C++ -> General, add '../UtilityLibrary' to Include Directories
// 5. go to C/C++ -> Preprocessor add '_CRT_SECURE_NO_WARNINGS;NDEBUG;_NDEBUG;'
//    to Release and '_CRT_SECURE_NO_WARNINGS;_DEBUG;DEBUG;' to Debug
// 6. go to C/C++ -> Precompiled Headers change Precompiled Header to 'Use'
//    and for stdafx.cpp change it to 'Create'
// 7. go to Linker -> Input, add 'd3d12.lib;dxgi.lib;d3dcompiler.lib;dxguid.lib;'
// 8. for .hlsl files change it item type to Custom Build Tool, and change the 
//    Content attribute to Yes
// 9. go to hlsl files' Configuration Properties -> Custom Build Tool -> General
//    add Command Line 'copy %(Identity) "$(OutDir)" >NUL'
//    add Outputs '$(OutDir)\%(Identity)' and Treat Output As Content 'Yes'

#include "DXHelper.h"

class CommandContext;
namespace Core
{
	const uint32_t				NUM_RTV = 64;
	const uint32_t				NUM_DSV = 64;
	const uint32_t				NUM_SMP = 128;
	const uint32_t				NUM_CSU = 128;

	struct Settings
	{
		bool					enableFullScreen = false;
		bool					warpDevice = false;
		DXGI_SWAP_CHAIN_DESC1	swapChainDesc = {};

		// Free to be changed after init
		//Vsync
		bool					vsync = false;
		bool					FXAA = true;
		bool					showPerf = true;
	};

	class IDX12Framework
	{
	public:
		// Framework interface for rendering loop
		virtual void ParseCommandLineArgs();
		virtual void OnInit() {};
		virtual void OnConfiguration() {};
		virtual HRESULT OnCreateResource() = 0;
		virtual HRESULT OnSizeChanged() = 0;
		virtual void OnUpdate() = 0;
		virtual void OnRender( CommandContext& EngineContext ) = 0;
		virtual void OnDestroy() = 0;
		virtual bool OnEvent( MSG* msg ) = 0;
	};

	int Run( IDX12Framework& application, HINSTANCE hInstance, int nCmdShow );
	std::wstring GetAssetFullPath( LPCWSTR assetName );

	extern uint64_t		g_tickesPerSecond;		// CPU tickes per second
	extern uint64_t		g_lastFrameTickCount;	// Total CPU tickes until last frame
	extern double		g_elapsedTime;			// Elapsed time since program start
	extern double		g_deltaTime;			// Elapsed time since last frame
	extern Settings     g_config;				// gfx settings
	extern HWND         g_hwnd;					// Window handle.
	extern std::wstring g_title;
	extern std::wstring g_assetsPath;			// Root assets path.   
	extern wchar_t      g_strCustom[256];		// temp variable for display GPU timing
}