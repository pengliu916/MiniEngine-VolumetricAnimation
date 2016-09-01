#include "stdafx.h"
#include "VolumetricAnimation.h"

_Use_decl_annotations_
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
	VolumetricAnimation application( 1024, 768, L"D3D12 Volumetric Animation" );
	return Core::Run( application, hInstance, nCmdShow );
}
