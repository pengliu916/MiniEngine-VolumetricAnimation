#pragma once
#include <interactioncontext.h>

class OrbitCamera
{
public:
	OrbitCamera();
	~OrbitCamera();

	void View( DirectX::XMVECTOR center,
		float radius, float minRadius, float maxRadius,
		float longAngle, float latAngle );

	// Uses the provided fov for the larger dimension
	void Projection( float fov, float aspect );

	DirectX::XMVECTOR const& Eye() const { return mEye; }
	DirectX::XMMATRIX const& Projection() const { return mProjection; }
	DirectX::XMMATRIX const& View() const { return mView; }

	void AddPointer( uint32_t pointerId );
	void ProcessPointerFrames( uint32_t pointerId, const POINTER_INFO* pointerInfo );
	void ProcessInertia();
	void RemovePointer( uint32_t pointerId );

	void OrbitX( float angle );
	void OrbitY( float angle );
	void ZoomRadius( float delta );
	void ZoomRadiusScale( float delta );

private:
	void UpdateData();
	static VOID CALLBACK StaticInteractionOutputCallback( VOID *clientData, const INTERACTION_CONTEXT_OUTPUT *output );
	void InteractionOutputCallback( const INTERACTION_CONTEXT_OUTPUT *output );

	DirectX::XMVECTOR	mCenter;
	DirectX::XMVECTOR	mUp;
	float				mMinRadius;
	float				mMaxRadius;

	float				mLatAngle;
	float				mLongAngle;
	float				mRadius;

	DirectX::XMVECTOR	mEye;
	DirectX::XMMATRIX	mView;
	DirectX::XMMATRIX	mProjection;

	HINTERACTIONCONTEXT	mInteractionContext;
};
