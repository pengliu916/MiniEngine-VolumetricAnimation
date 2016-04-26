#pragma once
#ifdef _DEBUG
#define ATTACH_CONSOLE 1
#endif
namespace MsgPrinting
{
	const WORD MAX_CONSOLE_LINES = 500;
	const WORD MAX_MSG_LENGTH = 1024;

	extern CRITICAL_SECTION outputCS;

	enum MessageType
	{
		MSG_WARNING = 0,
		MSG_ERROR,
		MSG_INFO,
		MSGTYPECOUNT,
	};

	enum consoleTextColor
	{
		CONTXTCOLOR_DEFAULT = 0,
		CONTXTCOLOR_RED,
		CONTXTCOLOR_GREEN,
		CONTXTCOLOR_YELLOW,
		CONTXTCOLOR_BLUE,
		CONTXTCOLOR_AQUA,
		CONTXTCOLOR_FUSCHIA,
		CONTEXTCOLORCOUNT,
	};

	void Init();
	void Destory();
	void ConsoleColorSet( int colorcode );
	void PrintMsg( MessageType msgType, const wchar_t* szFormat, ... );
	void PrintMsg( MessageType msgType, const char* szFormat, ... );
	void AttachConsole();
}

#define PRINTWARN(fmt,...) \
{ \
	CriticalSectionScope lock( &MsgPrinting::outputCS ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_YELLOW ); \
	MsgPrinting::PrintMsg( MsgPrinting::MSG_WARNING, fmt, __VA_ARGS__ ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_DEFAULT ); \
} 

#define PRINTERROR(fmt,...) \
{ \
	CriticalSectionScope lock( &MsgPrinting::outputCS ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_RED ); \
	MsgPrinting::PrintMsg( MsgPrinting::MSG_ERROR, fmt, __VA_ARGS__ ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_DEFAULT ); \
} 

#define PRINTINFO(fmt,...) \
{ \
	CriticalSectionScope lock( &MsgPrinting::outputCS ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_GREEN ); \
	MsgPrinting::PrintMsg( MsgPrinting::MSG_INFO, fmt, __VA_ARGS__ ); \
	MsgPrinting::ConsoleColorSet( MsgPrinting::CONTXTCOLOR_DEFAULT ); \
} 