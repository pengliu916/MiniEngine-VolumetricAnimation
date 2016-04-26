#include "LibraryHeader.h"

#include "MsgPrinting.h"

#pragma warning(disable: 4996)


namespace
{
	// console window size in character 
	extern const SHORT CONSOLE_WINDOW_WIDTH = 100;
	extern const SHORT CONSOLE_WINDOW_HEIGHT = 30;

	WORD g_defaultWinConsoleAttrib;

	void ResizeConsole( HANDLE hConsole, SHORT xSize, SHORT ySize )
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi; // Hold Current Console Buffer Info 
		BOOL bSuccess;
		SMALL_RECT srWindowRect;         // Hold the New Console Size 
		COORD coordScreen;

		bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
		g_defaultWinConsoleAttrib = csbi.wAttributes;

		// Get the Largest Size we can size the Console Window to 
		coordScreen = GetLargestConsoleWindowSize( hConsole );

		// Define the New Console Window Size and Scroll Position 
		srWindowRect.Right = (SHORT)(min( xSize, coordScreen.X ) - 1);
		srWindowRect.Bottom = (SHORT)(min( ySize, coordScreen.Y ) - 1);
		srWindowRect.Left = (SHORT)0;
		srWindowRect.Top = (SHORT)0;

		// Define the New Console Buffer Size    
		coordScreen.X = xSize;
		coordScreen.Y = MsgPrinting::MAX_CONSOLE_LINES;

		// If the Current Buffer is Larger than what we want, Resize the 
		// Console Window First, then the Buffer 
		if ((DWORD)csbi.dwSize.X * csbi.dwSize.Y > (DWORD)xSize * ySize)
		{
			bSuccess = SetConsoleWindowInfo( hConsole, TRUE, &srWindowRect );
			bSuccess = SetConsoleScreenBufferSize( hConsole, coordScreen );
		}
		// If the Current Buffer is Smaller than what we want, Resize the 
		// Buffer First, then the Console Window 
		if ((DWORD)csbi.dwSize.X * csbi.dwSize.Y < (DWORD)xSize * ySize)
		{
			bSuccess = SetConsoleScreenBufferSize( hConsole, coordScreen );
			bSuccess = SetConsoleWindowInfo( hConsole, TRUE, &srWindowRect );
		}
		// If the Current Buffer *is* the Size we want, Don't do anything! 
		return;
	}
}

namespace MsgPrinting
{
	CRITICAL_SECTION outputCS;

	void Init()
	{
		// Initialize output critical section
		InitializeCriticalSection( &outputCS );
#if ATTACH_CONSOLE
		AttachConsole();
#endif
	}

	void Destory()
	{
		// Delete output critical section
		DeleteCriticalSection( &outputCS );
	}

	void ConsoleColorSet( int colorcode )
	{
		HANDLE stdout_handle;
		CONSOLE_SCREEN_BUFFER_INFO info;
		WORD attrib = 0;

		stdout_handle = GetStdHandle( STD_OUTPUT_HANDLE );
		GetConsoleScreenBufferInfo( stdout_handle, &info );

		switch (colorcode)
		{
		case CONTXTCOLOR_RED:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_RED;
			break;
		case CONTXTCOLOR_GREEN:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_GREEN;
			break;
		case CONTXTCOLOR_YELLOW:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN;
			break;
		case CONTXTCOLOR_BLUE:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_BLUE;
			break;
		case CONTXTCOLOR_AQUA:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		case CONTXTCOLOR_FUSCHIA:
			attrib = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_BLUE;
			break;
		default:
			attrib = g_defaultWinConsoleAttrib;
			break;
		}
		SetConsoleTextAttribute( stdout_handle, attrib );
	}

	void PrintMsg( MessageType msgType, const wchar_t* szFormat, ... )
	{
		wchar_t szBuff[MAX_MSG_LENGTH];
		size_t preStrLen = 0;
		wchar_t wcsWarning[] = L"[ WARN\t]: ";
		wchar_t wcsError[] = L"[ ERROR\t]: ";
		wchar_t wcsInfo[] = L"[ INFO\t]: ";
		switch (msgType)
		{
		case MSG_WARNING:
			preStrLen = wcslen( wcsWarning );
			wcsncpy( szBuff, wcsWarning, preStrLen );
			break;
		case MSG_ERROR:
			preStrLen = wcslen( wcsError );
			wcsncpy( szBuff, wcsError, preStrLen );
			break;
		case MSG_INFO:
			preStrLen = wcslen( wcsInfo );
			wcsncpy( szBuff, wcsInfo, preStrLen );
			break;
		}
		va_list ap;
		va_start( ap, szFormat );
		_vswprintf( szBuff + preStrLen, szFormat, ap );
		va_end( ap );
		size_t length = wcslen( szBuff );
		assert( length < MAX_MSG_LENGTH - 1 );
		szBuff[length] = L'\n';
		szBuff[length + 1] = L'\0';
		wprintf( szBuff );
		fflush( stdout );
		OutputDebugString( szBuff );
	}

	void PrintMsg( MessageType msgType, const char* szFormat, ... )
	{
		char szBuff[MAX_MSG_LENGTH];
		size_t preStrLen = 0;
		char strWarning[] = "[ WARN\t]: ";
		char strError[] = "[ ERROR\t]: ";
		char strInfo[] = "[ INFO\t]: ";
		switch (msgType)
		{
		case MSG_WARNING:
			preStrLen = strlen( strWarning );
			strncpy( szBuff, strWarning, preStrLen );
			break;
		case MSG_ERROR:
			preStrLen = strlen( strError );
			strncpy( szBuff, strError, preStrLen );
			break;
		case MSG_INFO:
			preStrLen = strlen( strInfo );
			strncpy( szBuff, strInfo, preStrLen );
			break;
		}
		va_list ap;
		va_start( ap, szFormat );
		vsprintf( szBuff + preStrLen, szFormat, ap );
		va_end( ap );
		size_t length = strlen( szBuff );
		assert( length < MAX_MSG_LENGTH - 1 );
		szBuff[length] = '\n';
		szBuff[length + 1] = '\0';
		printf( szBuff );
		fflush( stdout );
		OutputDebugStringA( szBuff );
	}

	void AttachConsole() {
		bool has_console = ::AttachConsole( ATTACH_PARENT_PROCESS ) == TRUE;
		if (!has_console)
		{
			// We weren't launched from a console, so make one.
			has_console = AllocConsole() == TRUE;
		}
		if (!has_console)
			return;
		for (auto &file : {stdout, stderr})
		{
			freopen( "CONOUT$", "w", file );
			setvbuf( file, nullptr, _IONBF, 0 );
		}
		ResizeConsole( GetStdHandle( STD_OUTPUT_HANDLE ), CONSOLE_WINDOW_WIDTH, CONSOLE_WINDOW_HEIGHT );
	}
}