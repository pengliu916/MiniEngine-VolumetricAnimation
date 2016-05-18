#include "LibraryHeader.h"
#include "CommandContext.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "GPU_Profiler.h"
#include "DXHelper.h"
#include "GuiRenderer.h"
#include "FXAA.h"
#include <shellapi.h>

#include "Graphics.h"

namespace
{
	// temp variable for window resize
	uint32_t			_width;
	uint32_t			_height;

	// In multi-thread scenario, current thread may read old version of the following boolean due to 
	// unflushed cache etc. So to use flag in multi-thread cases, atomic bool is needed, and memory order semantic is crucial 
	std::atomic<bool>	_resize;
	bool				_terminated = false;
	bool				_hasError = false;

	HRESULT GetAssetsPath( _Out_writes_( pathSize ) WCHAR* path, UINT pathSize )
	{
		if (path == nullptr)
			return E_FAIL;
		DWORD size = GetModuleFileName( nullptr, path, pathSize );
		if (size == 0 || size == pathSize)
			return E_FAIL;
		WCHAR* lastSlash = wcsrchr( path, L'\\' );
		if (lastSlash)
			*(lastSlash + 1) = NULL;
		return S_OK;
	}
}

namespace Core
{
	uint64_t		g_tickesPerSecond = 0;
	uint64_t		g_lastFrameTickCount = 0;
	double			g_elapsedTime = 0;
	double			g_deltaTime = 0;

	Settings		g_config;
	HWND			g_hwnd;
	std::wstring	g_title = L"DX12Framework";
	std::wstring	g_assetsPath;
	wchar_t			g_strCustom[256] = L"";

	// Helper function for resolving the full path of assets.
	std::wstring GetAssetFullPath( LPCWSTR assetName ) { return g_assetsPath + assetName; }

	// Helper function for setting the window's title text.
	void SetCustomWindowText( LPCWSTR text )
	{
		std::wstring windowText = g_title + L" " + text;
		SetWindowText( g_hwnd, windowText.c_str() );
	}

	// Helper function for parsing any supplied command line args.
	void IDX12Framework::ParseCommandLineArgs()
	{
		int argc;
		LPWSTR *argv = CommandLineToArgvW( GetCommandLineW(), &argc );
		for (int i = 1; i < argc; ++i)
		{
			if (_wcsnicmp( argv[i], L"-warp", wcslen( argv[i] ) ) == 0 ||
				_wcsnicmp( argv[i], L"/warp", wcslen( argv[i] ) ) == 0)
				g_config.warpDevice = true;
		}
		LocalFree( argv );
	}

	// Main message handler for the sample.
	LRESULT CALLBACK WindowProc( HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam )
	{
		// Handle destroy/shutdown messages.
		switch (message)
		{
		case WM_CREATE:
		{
			// Save a pointer to the DXSample passed in to CreateWindow.
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams) );
		}
		return 0;

		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED)
			{
				RECT clientRect = {};
				GetClientRect( hWnd, &clientRect );
				_width = clientRect.right - clientRect.left;
				_height = clientRect.bottom - clientRect.top;
				if (g_config.swapChainDesc.Width != _width || g_config.swapChainDesc.Height != _height)
					_resize.store( true, std::memory_order_release );
			}
			return 0;

		case WM_KEYDOWN:
			if (lParam & (1 << 30)) {
				// Ignore repeats
				return 0;
			}
			switch (wParam) {
				//case VK_SPACE:
			case VK_ESCAPE:
				SendMessage( hWnd, WM_CLOSE, 0, 0 );
				return 0;
				// toggle vsync;
			case 'V':
				g_config.vsync = !g_config.vsync;
				PRINTINFO( "Vsync is %s", g_config.vsync ? "on" : "off" );
				return 0;
			} // Switch on key code
			return 0;

		case WM_GETMINMAXINFO:
		{
			MINMAXINFO* mmi = (MINMAXINFO*)lParam;
			mmi->ptMinTrackSize.x = 640;
			mmi->ptMinTrackSize.y = 480;
			return 0;
		}

		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;
		}

		// Handle any messages the switch statement didn't.
		return DefWindowProc( hWnd, message, wParam, lParam );
	}


	void FrameworkOnConfig( IDX12Framework& application )
	{
		// Giving app a chance to modify the framework level resource settings
		application.OnConfiguration();
	}

	void FrameworkInit( IDX12Framework& application )
	{
		MsgPrinting::Init();

		PRINTINFO( L"%s start", g_title.c_str() );

		HRESULT hr;
		WCHAR assetsPath[512];
		V( GetAssetsPath( assetsPath, _countof( assetsPath ) ) );
		g_assetsPath = assetsPath;

		Graphics::Init();
		application.ParseCommandLineArgs();
		application.OnInit();
	}

	HRESULT FrameworkCreateResource( IDX12Framework& application )
	{
		HRESULT hr;
		// Initialize framework level graphics resource
		VRET( Graphics::CreateResource() );
		// Initialize the sample. OnInit is defined in each child-implementation of DXSample.
		VRET( application.OnCreateResource() );

		return hr;
	}

	void FrameworkUpdate( IDX12Framework& application )
	{
		GuiRenderer::NewFrame();
		application.OnUpdate();
	}

	void FrameworkRender( IDX12Framework& application )
	{
		CommandContext& EngineContext = CommandContext::Begin( L"EngineContext" );
		application.OnRender( EngineContext );
		if(g_config.FXAA)
			FXAA::Render( EngineContext.GetComputeContext() );
		Graphics::Present( EngineContext );
	}

	void FrameworkDestory( IDX12Framework& application )
	{
		Graphics::g_cmdListMngr.IdleGPU();
		application.OnDestroy();
		Graphics::Shutdown();
		MsgPrinting::Destory();
	}

	void FrameworkHandleEvent( IDX12Framework& application, MSG* msg )
	{
		bool GuiIsUsingInput = GuiRenderer::OnEvent( msg );
		if (!GuiIsUsingInput)
			application.OnEvent( msg );
	}

	void FrameworkResize( IDX12Framework& application )
	{
		g_config.swapChainDesc.Width = _width;
		g_config.swapChainDesc.Height = _height;

		Graphics::Resize();

		application.OnSizeChanged();
		PRINTINFO( "Window resize to %d x %d", g_config.swapChainDesc.Width, g_config.swapChainDesc.Height );
	}

	void RenderLoop( IDX12Framework& application )
	{
		SetThreadName( "Render Thread" );

		QueryPerformanceFrequency( (LARGE_INTEGER*)&g_tickesPerSecond );
		QueryPerformanceCounter( (LARGE_INTEGER*)&g_lastFrameTickCount );

		// main loop
		double frameTime = 0.0;

		while (!_terminated && !_hasError)
		{
			// Get time delta
			uint64_t count;
			QueryPerformanceCounter( (LARGE_INTEGER*)&count );
			g_deltaTime = (double)(count - g_lastFrameTickCount) / g_tickesPerSecond;
			g_elapsedTime += g_deltaTime;
			g_lastFrameTickCount = count;

			// Maintaining absolute time sync is not important in this demo so we can err on the "smoother" side
			double alpha = 0.1f;
			frameTime = alpha * g_deltaTime + (1.0f - alpha) * frameTime;

			// Update GUI
			{
				wchar_t buffer[512];
				swprintf( buffer, 512, L"-%4.1f ms  %.0f fps %s", 1000.f * frameTime, 1.0f / frameTime, g_strCustom );
				SetCustomWindowText( buffer );
			}

			if (_resize.load( std::memory_order_acquire ))
			{
				_resize.store( false, std::memory_order_relaxed );
				FrameworkResize( application );
			}
			FrameworkUpdate( application );
			FrameworkRender( application );
		}
		FrameworkDestory( application );
	}

	int Run( IDX12Framework& application, HINSTANCE hInstance, int nCmdShow )
	{
		SetThreadName( "UI Thread" );

		FrameworkInit( application );
		FrameworkOnConfig( application );

		// Initialize the window class.
		WNDCLASSEX windowClass = {0};
		windowClass.cbSize = sizeof( WNDCLASSEX );
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = hInstance;
		windowClass.hCursor = LoadCursor( NULL, IDC_ARROW );
		windowClass.lpszClassName = L"WindowClass1";
		RegisterClassEx( &windowClass );

		LONG top = 300;
		LONG left = 300;

#if ATTACH_CONSOLE
		RECT rect;
		HWND con_hwnd = GetConsoleWindow();
		if (GetWindowRect( con_hwnd, &rect ))
		{
			top = rect.top + GetSystemMetrics( SM_CYFRAME ) + GetSystemMetrics( SM_CYCAPTION ) +
				GetSystemMetrics( SM_CXPADDEDBORDER );
			left = rect.right;
		}
#endif

		RECT windowRect = {left, top, left + static_cast<LONG>(g_config.swapChainDesc.Width),
			top + static_cast<LONG>(g_config.swapChainDesc.Height)};
		AdjustWindowRect( &windowRect, WS_OVERLAPPEDWINDOW, FALSE );

		// Create the window and store a handle to it.
		g_hwnd = CreateWindowEx( NULL, L"WindowClass1", g_title.c_str(), WS_OVERLAPPEDWINDOW,
			windowRect.left, windowRect.top, windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top, NULL, NULL, hInstance, nullptr );

		// SwapChain need hwnd, so have to place it after window creation
		FrameworkCreateResource( application );

		g_title += (g_config.warpDevice ? L" (WARP)" : L"");

		// Start the master render thread
		std::thread renderThread( RenderLoop, std::ref( application ) );
		thread_guard g( renderThread );

		// Bring up the window
		ShowWindow( g_hwnd, nCmdShow );

		// Enable mouse interaction
		EnableMouseInPointer( TRUE );

		// Main sample loop.
		MSG msg = {0};
		while (msg.message != WM_QUIT)
		{
			// Process any messages in the queue.
			if (PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ))
			{
				TranslateMessage( &msg );
				DispatchMessage( &msg );

				// Pass events into our sample.
				FrameworkHandleEvent( application, &msg );
			}
		}
		_terminated = true;
		// Return this part of the WM_QUIT message to Windows.
		return static_cast<char>(msg.wParam);
	}
}