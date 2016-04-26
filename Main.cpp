#include "stdafx.h"
#include "VolumetricAnimation.h"

_Use_decl_annotations_
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
	VolumetricAnimation application( 1280, 720, L"D3D12 Volumetric Animation" );
	return Core::Run( application, hInstance, nCmdShow );
}
