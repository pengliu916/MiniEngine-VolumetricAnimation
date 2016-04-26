#include "LibraryHeader.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "DX12Framework.h"
#include "SamplerMngr.h"
#include "PipelineState.h"
#include "GPU_Profiler.h"
#include "GpuResource.h"
#include "imgui.h"
#include "FXAA.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace FXAA
{
	RootSignature RootSig;

	ComputePSO Pass1LdrCS;
	ComputePSO Color2LumaCS;
	ComputePSO ResolveWorkCS;
	ComputePSO Pass2HCS;
	ComputePSO Pass2VCS;
	ComputePSO Pass2HDebugCS;
	ComputePSO Pass2VDebugCS;

	ColorBuffer g_LumaBuffer;
	StructuredBuffer g_FXAAWorkQueueH;
	StructuredBuffer g_FXAAWorkQueueV;
	TypedBuffer g_FXAAColorQueueH( DXGI_FORMAT_R11G11B10_FLOAT );
	TypedBuffer g_FXAAColorQueueV( DXGI_FORMAT_R11G11B10_FLOAT );
	IndirectArgsBuffer IndirectParameters;

	float ContrastThreeshold = 0.2f;
	float SubpixelRemoval = 0.75f;
	bool DebugDraw = false;
}

void FXAA::CreateResource()
{
	RootSig.Reset( 3, 1 );
	RootSig.InitStaticSampler( 0, Graphics::g_SamplerLinearClampDesc );
	RootSig[0].InitAsConstants( 0, 4 );
	RootSig[1].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5 );
	RootSig[2].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6 );
	RootSig.Finalize();

	HRESULT hr;
	ComPtr<ID3DBlob> Pass1ComputeShader;
	ComPtr<ID3DBlob> Color2LumaComputeShader;
	ComPtr<ID3DBlob> ResolveWorkComputeShader;
	ComPtr<ID3DBlob> Pass2HComputeShader;
	ComPtr<ID3DBlob> Pass2VComputeShader;
	ComPtr<ID3DBlob> Pass2HDebugComputeShader;
	ComPtr<ID3DBlob> Pass2VDebugComputeShader;

	uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	D3D_SHADER_MACRO macro[] =
	{
		{"__hlsl"					,	"1"}, // 0
		{"Pass1"					,	"1"}, // 1
		{"ResolveWork"				,	"0"}, // 2
		{"Pass2"					,	"0"}, // 3
		{"VERTICAL_ORIENTATION"		,	"0"}, // 4
		{"DEBUG_OUTPUT"				,	"0"}, // 5
		{"Color2Luma"				,	"0"}, // 6
		{nullptr					,	nullptr}
	};
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Pass1ComputeShader ) );
	macro[1].Definition = "0"; // define Pass1 0
	macro[6].Definition = "1"; // define Color2Luma 1
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Color2LumaComputeShader ) );
	macro[6].Definition = "0"; // define Pass1 0
	macro[2].Definition = "1"; // define ResolveWork 1
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &ResolveWorkComputeShader ) );
	macro[2].Definition = "0"; // define ResolveWork 0
	macro[3].Definition = "1"; // define Pass2 1
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Pass2HComputeShader ) );
	macro[4].Definition = "1"; // define Vertical_Orientation 1
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Pass2VComputeShader ) );
	macro[5].Definition = "1"; // define Debug_Output 1
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Pass2VDebugComputeShader ) );
	macro[4].Definition = "0"; // define Vertical_Orientation 0
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "FXAA.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &Pass2HDebugComputeShader ) );

#define CreatePSO( ObjName, Shader)\
	ObjName.SetRootSignature(RootSig);\
	ObjName.SetComputeShader(Shader->GetBufferPointer(), Shader->GetBufferSize());\
	ObjName.Finalize();

	CreatePSO( Pass1LdrCS, Pass1ComputeShader );
	CreatePSO( Color2LumaCS, Color2LumaComputeShader);
	CreatePSO( ResolveWorkCS, ResolveWorkComputeShader );
	CreatePSO( Pass2HCS, Pass2HComputeShader );
	CreatePSO( Pass2VCS, Pass2VComputeShader );
	CreatePSO( Pass2HDebugCS, Pass2HDebugComputeShader );
	CreatePSO( Pass2VDebugCS, Pass2VDebugComputeShader );
#undef  CreatePSO

	__declspec(align(16)) const uint32_t initArgs[6] = {0,1,1,0,1,1};
	IndirectParameters.Create( L"FXAA Indirect Parameters", 2, sizeof( D3D12_DISPATCH_ARGUMENTS ), initArgs );
	g_FXAAWorkQueueH.Create( L"FXAA Horizontal Work Queue", 512 * 1024, 4 );
	g_FXAAWorkQueueV.Create( L"FXAA Vertical Work Queue", 512 * 1024, 4 );
	g_FXAAColorQueueH.Create( L"FXAA Horizontal Color Queue", 512 * 1024, 4 );
	g_FXAAColorQueueV.Create( L"FXAA Vertical Color Queue", 512 * 1024, 4 );

	Resize();
}

void FXAA::Resize()
{
	g_LumaBuffer.Destroy();
	g_LumaBuffer.Create( L"Luma Buffer", Graphics::g_SceneColorBuffer.GetWidth(), Graphics::g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R8_UNORM );
}

void FXAA::Shutdown()
{
	IndirectParameters.Destroy();
	g_LumaBuffer.Destroy();
	g_FXAAWorkQueueH.Destroy();
	g_FXAAWorkQueueV.Destroy();
	g_FXAAColorQueueH.Destroy();
	g_FXAAColorQueueV.Destroy();
}

void FXAA::Render( ComputeContext& Context )
{
	GPU_PROFILE( Context, L"FXAA" );
	Context.SetRootSignature( RootSig );
	Context.SetConstants( 0, 1.f / Graphics::g_SceneColorBuffer.GetWidth(), 1.f / Graphics::g_SceneColorBuffer.GetHeight(), ContrastThreeshold, SubpixelRemoval );

	// Pass1
	Context.ResetCounter( g_FXAAWorkQueueH );
	Context.ResetCounter( g_FXAAWorkQueueV );

	Context.TransitionResource( Graphics::g_SceneColorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	Context.TransitionResource( g_FXAAWorkQueueH, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.TransitionResource( g_FXAAWorkQueueV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.TransitionResource( g_FXAAColorQueueH, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.TransitionResource( g_FXAAColorQueueV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.TransitionResource( g_LumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

	D3D12_CPU_DESCRIPTOR_HANDLE Pass1UAVs[] =
	{
		g_FXAAWorkQueueH.GetUAV(),
		g_FXAAColorQueueH.GetUAV(),
		g_FXAAWorkQueueV.GetUAV(),
		g_FXAAColorQueueV.GetUAV(),
		g_LumaBuffer.GetUAV()
	};

	Context.SetPipelineState( Pass1LdrCS );
	Context.SetDynamicDescriptors( 1, 0, _countof( Pass1UAVs ), Pass1UAVs );
	Context.SetDynamicDescriptors( 2, 0, 1, &Graphics::g_SceneColorBuffer.GetSRV() );

	Context.Dispatch2D( Graphics::g_SceneColorBuffer.GetWidth(), Graphics::g_SceneColorBuffer.GetHeight() );
	// Pass2
	Context.SetPipelineState( ResolveWorkCS );
	Context.TransitionResource( IndirectParameters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.SetDynamicDescriptors( 1, 0, 1, &IndirectParameters.GetUAV() );
	Context.SetDynamicDescriptors( 1, 1, 1, &g_FXAAWorkQueueH.GetUAV() );
	Context.SetDynamicDescriptors( 1, 2, 1, &g_FXAAWorkQueueV.GetUAV() );
	Context.SetDynamicDescriptors( 2, 0, 1, &g_FXAAWorkQueueH.GetCounterSRV( Context ) );
	Context.SetDynamicDescriptors( 2, 1, 1, &g_FXAAWorkQueueV.GetCounterSRV( Context ) );

	Context.Dispatch( 1, 1, 1 );

	Context.TransitionResource( Graphics::g_SceneColorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	Context.TransitionResource( IndirectParameters, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT );
	Context.TransitionResource( g_FXAAWorkQueueH, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	Context.TransitionResource( g_FXAAColorQueueH, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	Context.TransitionResource( g_FXAAWorkQueueV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	Context.TransitionResource( g_FXAAColorQueueV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );

	Context.SetDynamicDescriptors( 1, 0, 1, &Graphics::g_SceneColorBuffer.GetUAV() );

	D3D12_CPU_DESCRIPTOR_HANDLE Pass2SRVs[] =
	{
		g_LumaBuffer.GetSRV(),
		Graphics::g_SceneColorBuffer.GetSRV(),
		g_FXAAWorkQueueH.GetSRV(),
		g_FXAAColorQueueH.GetSRV(),
	};
	Context.SetDynamicDescriptors( 2, 0, _countof( Pass2SRVs ), Pass2SRVs );

	if (DebugDraw)
	{
		Context.SetPipelineState( Color2LumaCS );
		Context.Dispatch2D( Graphics::g_SceneColorBuffer.GetWidth(), Graphics::g_SceneColorBuffer.GetHeight() );
	}
	// The final phase involves processing pixels on the work queues and writing them
	// back into the color buffer. Because the two source pixels required for linearly
	// blending are held in the work queue, this does not require also sampling from
	// the target color buffer (i.e. no read/modify/write, just write.)

	Context.SetPipelineState( DebugDraw ? Pass2HDebugCS : Pass2HCS );
	Context.DispatchIndirect( IndirectParameters, 0 );

	Context.SetDynamicDescriptors( 2, 2, 1, &g_FXAAWorkQueueV.GetSRV() );
	Context.SetDynamicDescriptors( 2, 3, 1, &g_FXAAColorQueueV.GetSRV() );

	Context.SetPipelineState( DebugDraw ? Pass2VDebugCS : Pass2VCS );
	Context.DispatchIndirect( IndirectParameters, 12 );
}

void FXAA::UpdateGUI()
{
	if (ImGui::CollapsingHeader( "FXAA" ))
	{
		ImGui::Checkbox( "Enable FXAA", &Core::g_config.FXAA );
		ImGui::SliderFloat( "Contrast Threshold", &ContrastThreeshold, 0.05f, 0.5f );
		ImGui::SliderFloat( "Subpixel Removal", &SubpixelRemoval, 0.f, 1.f );
		ImGui::Checkbox( "Debug Draw", &DebugDraw );
	}
}