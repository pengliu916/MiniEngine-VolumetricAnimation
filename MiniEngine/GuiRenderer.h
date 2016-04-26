class GraphicsContext;
namespace GuiRenderer
{
	void	Initialize();
	void	Shutdown();
	void	NewFrame();

	// Use if you want to reset your rendering device without losing ImGui state.
	HRESULT	CreateResource();
	bool	OnEvent( MSG* msg );

	void	Render( GraphicsContext& gfxContext );
}