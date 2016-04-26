#pragma once

#include "FXAA_SharedHeader.inl"
class ComputeContext;
class ColorBuffer;
namespace FXAA
{
	void CreateResource();
	void Resize();
	void Shutdown();
	void Render( ComputeContext& Context );
	void UpdateGUI();

	extern float ContrastThreeshold;
	extern float SubpixelRemoval;
	extern bool DebugDraw;
	extern ColorBuffer g_LumaBuffer;
}