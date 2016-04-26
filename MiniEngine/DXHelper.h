#pragma once
#include "Utility.h"

//#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))

inline HRESULT ReadDataFromFile( LPCWSTR filename, byte** data, UINT* size )
{
	using namespace Microsoft::WRL;

	CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {0};
	extendedParams.dwSize = sizeof( CREATEFILE2_EXTENDED_PARAMETERS );
	extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
	extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
	extendedParams.lpSecurityAttributes = nullptr;
	extendedParams.hTemplateFile = nullptr;

	Wrappers::FileHandle file(
		CreateFile2( filename, GENERIC_READ, FILE_SHARE_READ,
			OPEN_EXISTING, &extendedParams )
		);

	if (file.Get() == INVALID_HANDLE_VALUE)
		return E_FAIL;

	FILE_STANDARD_INFO fileInfo = {0};
	if (!GetFileInformationByHandleEx( file.Get(), FileStandardInfo,
		&fileInfo, sizeof( fileInfo ) ))
		return E_FAIL;

	if (fileInfo.EndOfFile.HighPart != 0)
		return E_FAIL;

	*data = reinterpret_cast<byte*>(malloc( fileInfo.EndOfFile.LowPart ));
	*size = fileInfo.EndOfFile.LowPart;

	if (!ReadFile( file.Get(), *data, fileInfo.EndOfFile.LowPart, nullptr, nullptr ))
		return E_FAIL;

	return S_OK;
}
