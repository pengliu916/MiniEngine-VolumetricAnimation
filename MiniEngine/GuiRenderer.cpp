#include "LibraryHeader.h"
#include "imgui.h"
#include "CommandContext.h"
#include "GuiRenderer.h"
#include "DX12Framework.h"
#include "GpuResource.h"
#include "GPU_Profiler.h"
#include "Utility.h"
#include "LinearAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "SamplerMngr.h"
#include "FXAA.h"
#include "Graphics.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

using namespace DirectX;
using namespace Microsoft::WRL;

namespace
{
	GraphicsPSO				_graphicsPSO;
	RootSignature			_rootSignature;
	Texture					_texture;

	struct VERTEX_CONSTANT_BUFFER
	{
		float        mvp[4][4];
	};

	// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
	// If text or lines are blurry when integrating ImGui in your engine:
	// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
	void ImGuiRender( ImDrawData* draw_data, GraphicsContext& context )
	{
		{
			GPU_PROFILE( context, L"GUI" );
			context.SetRootSignature( _rootSignature );
			context.SetPipelineState( _graphicsPSO );

			size_t VBSize = draw_data->TotalVtxCount * sizeof( ImDrawVert );
			size_t IBSize = draw_data->TotalIdxCount * sizeof( ImDrawIdx );
			DynAlloc vb = context.m_CpuLinearAllocator.Allocate( VBSize );
			DynAlloc ib = context.m_CpuLinearAllocator.Allocate( IBSize );

			ImDrawVert* vtx_dst = (ImDrawVert*)vb.DataPtr;
			ImDrawIdx* idx_dst = (ImDrawIdx*)ib.DataPtr;
			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[n];
				memcpy( vtx_dst, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof( ImDrawVert ) );
				memcpy( idx_dst, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size() * sizeof( ImDrawIdx ) );
				vtx_dst += cmd_list->VtxBuffer.size();
				idx_dst += cmd_list->IdxBuffer.size();
			}

			D3D12_VERTEX_BUFFER_VIEW VBView;
			VBView.BufferLocation = vb.GpuAddress;
			VBView.SizeInBytes = (UINT)VBSize;
			VBView.StrideInBytes = sizeof( ImDrawVert );

			D3D12_INDEX_BUFFER_VIEW IBView;
			IBView.BufferLocation = ib.GpuAddress;
			IBView.SizeInBytes = (UINT)IBSize;
			IBView.Format = DXGI_FORMAT_R16_UINT;

			context.SetDynamicVB( 0, VBView );
			context.SetDynamicIB( IBView );

			float L = 0.0f;
			float R = ImGui::GetIO().DisplaySize.x;
			float B = ImGui::GetIO().DisplaySize.y;
			float T = 0.0f;
			float mvp[4][4] =
			{
				{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
				{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
				{ 0.0f,         0.0f,           0.5f,       0.0f },
				{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
			};
			context.SetDynamicConstantBufferView( 0, sizeof( mvp ), mvp );
			context.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			context.SetRenderTargets( 1, &Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx] );
			context.SetViewport( Graphics::g_DisplayPlaneViewPort );
			context.SetScisor( Graphics::g_DisplayPlaneScissorRect );
			// Setup viewport
			D3D12_VIEWPORT vp;
			memset( &vp, 0, sizeof( D3D12_VIEWPORT ) );
			vp.Width = ImGui::GetIO().DisplaySize.x;
			vp.Height = ImGui::GetIO().DisplaySize.y;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			context.SetViewport( vp );

			// Render command lists
			int vtx_offset = 0;
			int idx_offset = 0;
			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[n];
				for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.size(); cmd_i++)
				{
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
					if (pcmd->UserCallback)
					{
						pcmd->UserCallback( cmd_list, pcmd );
					}
					else
					{
						const D3D12_RECT r = {(LONG)pcmd->ClipRect.x, (LONG)pcmd->ClipRect.y, (LONG)pcmd->ClipRect.z, (LONG)pcmd->ClipRect.w};
						context.SetDynamicDescriptors( 1, 0, 1, (D3D12_CPU_DESCRIPTOR_HANDLE*)(pcmd->TextureId));
						context.SetScisor( r );
						context.DrawIndexed( pcmd->ElemCount, idx_offset, vtx_offset );
					}
					idx_offset += pcmd->ElemCount;
				}
				vtx_offset += cmd_list->VtxBuffer.size();
			}
		}
	}

	void CreateFontsTexture()
	{
		// Build texture atlas
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );

		_texture.Create( width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels );

		// Store our identifier
		io.Fonts->TexID = (void *)&(_texture.GetSRV());
	}
}

bool GuiRenderer::OnEvent( MSG* msg )
{
	ImGuiIO& io = ImGui::GetIO();
	switch (msg->message)
	{
	case WM_LBUTTONDOWN:
		io.MouseDown[0] = true;
		break;
	case WM_LBUTTONUP:
		io.MouseDown[0] = false;
		break;
	case WM_RBUTTONDOWN:
		io.MouseDown[1] = true;
		break;
	case WM_RBUTTONUP:
		io.MouseDown[1] = false;
		break;
	case WM_MBUTTONDOWN:
		io.MouseDown[2] = true;
		break;
	case WM_MBUTTONUP:
		io.MouseDown[2] = false;
		break;
	case WM_MOUSEWHEEL:
		io.MouseWheel += GET_WHEEL_DELTA_WPARAM( msg->wParam ) > 0 ? +1.0f : -1.0f;
		break;
	case WM_MOUSEMOVE:
		io.MousePos.x = (signed short)(msg->lParam);
		io.MousePos.y = (signed short)(msg->lParam >> 16);
		break;
	case WM_KEYDOWN:
		if (msg->wParam < 256)
			io.KeysDown[msg->wParam] = 1;
		break;
	case WM_KEYUP:
		if (msg->wParam < 256)
			io.KeysDown[msg->wParam] = 0;
		break;
	case WM_CHAR:
		// You can also use ToAscii()+GetKeyboardState() to retrieve characters.
		if (msg->wParam > 0 && msg->wParam < 0x10000)
			io.AddInputCharacter( (unsigned short)(msg->wParam) );
		break;
	}
	return io.WantCaptureMouse;
}

HRESULT GuiRenderer::CreateResource()
{
	HRESULT hr;
	_rootSignature.Reset( 2, 1 );
	_rootSignature.InitStaticSampler( 0, Graphics::g_SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL );
	_rootSignature[0].InitAsConstantBuffer( 0 );
	_rootSignature[1].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL );
	_rootSignature.Finalize( D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS );

	_graphicsPSO.SetRootSignature( _rootSignature );
	ComPtr<ID3DBlob> vertexShaderBlob;
	ComPtr<ID3DBlob> pixelShaderBlob;

	// Create the vertex shader
	{
		static const char* vertexShader =
			"cbuffer vertexBuffer : register(b0) \
			{\
			float4x4 ProjectionMatrix; \
			};\
			struct VS_INPUT\
			{\
			float2 pos : POSITION;\
			float4 col : COLOR0;\
			float2 uv  : TEXCOORD0;\
			};\
			\
			struct PS_INPUT\
			{\
			float4 pos : SV_POSITION;\
			float4 col : COLOR0;\
			float2 uv  : TEXCOORD0;\
			};\
			\
			PS_INPUT main(VS_INPUT input)\
			{\
			PS_INPUT output;\
			output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
			output.col = input.col;\
			output.uv  = input.uv;\
			return output;\
			}";

		VRET( D3DCompile( vertexShader, strlen( vertexShader ), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vertexShaderBlob, NULL ) );
		_graphicsPSO.SetVertexShader( vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() );

		// Create the input layout
		D3D12_INPUT_ELEMENT_DESC local_layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (size_t)(&((ImDrawVert*)0)->col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		_graphicsPSO.SetInputLayout( _countof( local_layout ), local_layout );
	}

	// Create the pixel shader
	{
		static const char* pixelShader =
			"struct PS_INPUT\
			{\
			float4 pos : SV_POSITION;\
			float4 col : COLOR0;\
			float2 uv  : TEXCOORD0;\
			};\
			sampler sampler0;\
			Texture2D texture0;\
			\
			float4 main(PS_INPUT input) : SV_Target\
			{\
			float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
			return out_col; \
			}";

		VRET( D3DCompile( pixelShader, strlen( pixelShader ), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &pixelShaderBlob, NULL ) );
		_graphicsPSO.SetPixelShader( pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() );
	}

	// Create the blending setup
	_graphicsPSO.SetBlendState( Graphics::g_BlendTraditional );
	_graphicsPSO.SetRasterizerState( Graphics::g_RasterizerTwoSided );
	_graphicsPSO.SetDepthStencilState( Graphics::g_DepthStateDisabled );
	_graphicsPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	_graphicsPSO.SetRenderTargetFormats( 1, &Graphics::g_pDisplayPlanes[0].GetFormat(), DXGI_FORMAT_UNKNOWN );
	_graphicsPSO.Finalize();

	CreateFontsTexture();

	return S_OK;
}

void GuiRenderer::Initialize()
{
	ImGuiIO& io = ImGui::GetIO();
	io.KeyMap[ImGuiKey_Tab] = VK_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array that we will update during the application lifetime.
	io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	io.KeyMap[ImGuiKey_Home] = VK_HOME;
	io.KeyMap[ImGuiKey_End] = VK_END;
	io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';

	//io.RenderDrawListsFn = ImGuiRender;  // Alternatively you can set this to NULL and call ImGui::GetDrawData() after ImGui::Render() to get the same ImDrawData pointer.
	//io.ImeWindowHandle = g_hWnd;
}

void GuiRenderer::Shutdown()
{
	ImGui::Shutdown();
	//g_hWnd = (HWND)0;
}

void GuiRenderer::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();

	// Setup display size (every frame to accommodate for window resizing)
	//RECT rect;
	//GetClientRect(g_hWnd, &rect);
	//io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
	io.DisplaySize = ImVec2( (float)(Core::g_config.swapChainDesc.Width), (float)(Core::g_config.swapChainDesc.Height) );

	// Setup time step
	io.DeltaTime = (float)Core::g_deltaTime;

	// Read keyboard modifiers inputs
	io.KeyCtrl = (GetKeyState( VK_CONTROL ) & 0x8000) != 0;
	io.KeyShift = (GetKeyState( VK_SHIFT ) & 0x8000) != 0;
	io.KeyAlt = (GetKeyState( VK_MENU ) & 0x8000) != 0;
	io.KeySuper = false;
	// io.KeysDown : filled by WM_KEYDOWN/WM_KEYUP events
	// io.MousePos : filled by WM_MOUSEMOVE events
	// io.MouseDown : filled by WM_*BUTTON* events
	// io.MouseWheel : filled by WM_MOUSEWHEEL events

	// Hide OS mouse cursor if ImGui is drawing it
	SetCursor( io.MouseDrawCursor ? NULL : LoadCursor( NULL, IDC_ARROW ) );

	// Start the frame
	ImGui::NewFrame();
}

void GuiRenderer::Render( GraphicsContext& gfxContext )
{
	static bool showEnginePenal = false;
	if (ImGui::Begin( "Engine Penal", &showEnginePenal ))
	{
		Graphics::UpdateGUI();
		FXAA::UpdateGUI();
	}
	ImGui::End();
	ImGui::Render();
	ImGuiRender( ImGui::GetDrawData(), gfxContext );
}