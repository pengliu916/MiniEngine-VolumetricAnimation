// Port from Intel astroids demo
#include "LibraryHeader.h"
#include "Utility.h"
#include "Camera.h"

using namespace DirectX;

OrbitCamera::OrbitCamera()
{
	// Defaults
	mCenter = XMVectorSet( 0, 0, 0, 0 );
	mUp = XMVectorSet( 0, 1, 0, 0 );
	mRadius = 1.0f;
	mMinRadius = 1.0f;
	mMaxRadius = 1.0f;
	mLongAngle = 0.0f;
	mLatAngle = 0.0f;

	HRESULT hr;
	// Set up interaction context (i.e. touch input processing, etc)
	V( CreateInteractionContext( &mInteractionContext ) );
	V( SetPropertyInteractionContext( mInteractionContext, INTERACTION_CONTEXT_PROPERTY_FILTER_POINTERS, TRUE ) );

	{
		INTERACTION_CONTEXT_CONFIGURATION config[] =
		{
			{ INTERACTION_ID_MANIPULATION,
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_X |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_Y |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_SCALING |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_INERTIA |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_SCALING_INERTIA |
			INTERACTION_CONFIGURATION_FLAG_MANIPULATION_MULTIPLE_FINGER_PANNING
			},
		};

		V( SetInteractionConfigurationInteractionContext( mInteractionContext, ARRAYSIZE( config ), config ) );
	}
	V( SetInertiaParameterInteractionContext( mInteractionContext, INERTIA_PARAMETER_TRANSLATION_DECELERATION, 0.001f ) );
	V( RegisterOutputCallbackInteractionContext( mInteractionContext, OrbitCamera::StaticInteractionOutputCallback, this ) );
}


OrbitCamera::~OrbitCamera()
{
	DestroyInteractionContext( mInteractionContext );
}

void OrbitCamera::View( DirectX::XMVECTOR center, float radius, float minRadius, float maxRadius,
	float longAngle, float latAngle )
{
	mCenter = center;
	mRadius = radius;
	mMinRadius = minRadius;
	mMaxRadius = maxRadius;
	mLongAngle = longAngle;
	mLatAngle = latAngle;
	UpdateData();
}

void OrbitCamera::Projection( float fov, float aspect )
{
	float fovY = (aspect <= 1.0 ? fov : fov / aspect);
	mProjection = XMMatrixPerspectiveFovRH( fovY, aspect, 10000.0f, 0.1f );
	UpdateData();
}

void OrbitCamera::UpdateData()
{
	mEye = XMVectorSet(
		mRadius * sin( mLatAngle ) * cos( mLongAngle ),
		mRadius * cos( mLatAngle ),
		mRadius * sin( mLatAngle ) * sin( mLongAngle ),
		0.0f );

	mView = XMMatrixLookAtRH( mEye, mCenter, mUp );
}

void OrbitCamera::OrbitX( float angle )
{
	mLongAngle += angle;
	UpdateData();
}

void OrbitCamera::OrbitY( float angle )
{
	float limit = XM_PI * 0.01f;
	mLatAngle = max( limit, min( XM_PI - limit, mLatAngle + angle ) );
	UpdateData();
}

void OrbitCamera::ZoomRadius( float delta )
{
	mRadius = max( mMinRadius, min( mMaxRadius, mRadius + delta ) );
	UpdateData();
}

void OrbitCamera::ZoomRadiusScale( float delta )
{
	mRadius = max( mMinRadius, min( mMaxRadius, mRadius * delta ) );
	UpdateData();
}

void OrbitCamera::AddPointer( uint32_t pointerId )
{
	AddPointerInteractionContext( mInteractionContext, pointerId );
}

void OrbitCamera::ProcessPointerFrames( uint32_t pointerId, const POINTER_INFO* pointerInfo )
{
	ProcessPointerFramesInteractionContext( mInteractionContext, 1, 1, pointerInfo );
}

void OrbitCamera::RemovePointer( uint32_t pointerId )
{
	RemovePointerInteractionContext( mInteractionContext, pointerId );
}

void OrbitCamera::ProcessInertia()
{
	ProcessInertiaInteractionContext( mInteractionContext );
}

VOID CALLBACK OrbitCamera::StaticInteractionOutputCallback( VOID *clientData, const INTERACTION_CONTEXT_OUTPUT *output )
{
	auto camera = reinterpret_cast<OrbitCamera*>(clientData);
	camera->InteractionOutputCallback( output );
}

void OrbitCamera::InteractionOutputCallback( const INTERACTION_CONTEXT_OUTPUT *output )
{
	switch (output->interactionId)
	{
	case INTERACTION_ID_MANIPULATION: {
		auto delta = &output->arguments.manipulation.delta;
		OrbitX( delta->translationX * 0.007f );
		OrbitY( -delta->translationY * 0.007f );
		ZoomRadiusScale( 1.0f / delta->scale );

		break;
	}
	default:
		break;
	}
}
