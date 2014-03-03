/********************************************************************
Fluid3DCalculator.h: Encapsulates a 3D fluid simulation
being calculated on the GPU.

Author:	Valentin Hinov
Date: 18/2/2014
*********************************************************************/

#include "Fluid3DCalculator.h"
#include "../../display/D3DShaders/Fluid3DShaders.h"

#define READ 0
#define WRITE 1
#define WRITE2 2
#define WRITE3 3

// Simulation parameters
#define TIME_STEP 0.125f
#define IMPULSE_RADIUS 3.0f
#define INTERACTION_IMPULSE_RADIUS 7.0f
#define OBSTACLES_IMPULSE_RADIUS 5.0f
#define JACOBI_ITERATIONS 15
#define VEL_DISSIPATION 0.999f
#define DENSITY_DISSIPATION 0.999f
#define TEMPERATURE_DISSIPATION 0.99f
#define SMOKE_BUOYANCY 1.0f
#define SMOKE_WEIGHT 0.05f
#define AMBIENT_TEMPERATURE 0.0f
#define IMPULSE_TEMPERATURE 1.5f
#define IMPULSE_DENSITY 1.0f

using namespace Fluid3D;

Fluid3DCalculator::Fluid3DCalculator(Vector3 dimensions) : pD3dGraphicsObj(nullptr), 
	timeStep(TIME_STEP),
	macCormackEnabled(true),
	jacobiIterations(JACOBI_ITERATIONS),
	mVelocitySP(nullptr),
	mDensitySP(nullptr),
	mTemperatureSP(nullptr),
	mPressureSP(nullptr),
	mObstacleSP(nullptr),
	mDimensions(dimensions) {

}

Fluid3DCalculator::~Fluid3DCalculator() {
	if (mPressureSP) {
		delete [] mPressureSP;
		mPressureSP = nullptr;
	}
	if (mDensitySP) {
		delete [] mDensitySP;
		mDensitySP = nullptr;
	}
	if (mTemperatureSP) {
		delete [] mTemperatureSP;
		mTemperatureSP = nullptr;
	}
	if (mVelocitySP) {
		delete [] mVelocitySP;
		mVelocitySP = nullptr;
	}
	if (mObstacleSP) {
		delete [] mObstacleSP;
		mObstacleSP = nullptr;
	}

	pD3dGraphicsObj = nullptr;
}

bool Fluid3DCalculator::Initialize(_In_ D3DGraphicsObject* d3dGraphicsObj, HWND hwnd) {
	pD3dGraphicsObj = d3dGraphicsObj;

	mForwardAdvectionShader = unique_ptr<AdvectionShader>(new AdvectionShader(AdvectionShader::ADVECTION_TYPE_FORWARD));
	bool result = mForwardAdvectionShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mBackwardAdvectionShader = unique_ptr<AdvectionShader>(new AdvectionShader(AdvectionShader::ADVECTION_TYPE_BACKWARD));
	result = mBackwardAdvectionShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mMacCormarckAdvectionShader = unique_ptr<AdvectionShader>(new AdvectionShader(AdvectionShader::ADVECTION_TYPE_MACCORMARCK));
	result = mMacCormarckAdvectionShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mImpulseShader = unique_ptr<ImpulseShader>(new ImpulseShader());
	result = mImpulseShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mJacobiShader = unique_ptr<JacobiShader>(new JacobiShader());
	result = mJacobiShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mDivergenceShader = unique_ptr<DivergenceShader>(new DivergenceShader());
	result = mDivergenceShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mSubtractGradientShader = unique_ptr<SubtractGradientShader>(new SubtractGradientShader());
	result = mSubtractGradientShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	mBuoyancyShader = unique_ptr<BuoyancyShader>(new BuoyancyShader());
	result = mBuoyancyShader->Initialize(pD3dGraphicsObj->GetDevice(),hwnd);
	if (!result) {
		return false;
	}

	// Create the velocity shader params
	CComPtr<ID3D11Texture3D> velocityText[4];
	mVelocitySP = new ShaderParams[4];
	D3D11_TEXTURE3D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(D3D11_TEXTURE3D_DESC));
	textureDesc.Width = (UINT) mDimensions.x;
	textureDesc.Height = (UINT) mDimensions.y;
	textureDesc.Depth = (UINT) mDimensions.z;
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;	// 3 components for velocity in 3D + alpha
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	for (int i = 0; i < 4; ++i) {
		HRESULT hr = pD3dGraphicsObj->GetDevice()->CreateTexture3D(&textureDesc, NULL, &velocityText[i]);
		if (FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the velocity Texture Object", L"Error", MB_OK);
			return false;
		}
		hr = pD3dGraphicsObj->GetDevice()->CreateShaderResourceView(velocityText[i], NULL, &mVelocitySP[i].mSRV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the velocity SRV", L"Error", MB_OK);
			return false;

		}
		hr = pD3dGraphicsObj->GetDevice()->CreateUnorderedAccessView(velocityText[i], NULL, &mVelocitySP[i].mUAV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the velocity UAV", L"Error", MB_OK);
			return false;
		}
	}

	// Create the density shader params
	CComPtr<ID3D11Texture3D> densityText[4];
	mDensitySP = new ShaderParams[4];
	textureDesc.Format = DXGI_FORMAT_R16_FLOAT;
	for (int i = 0; i < 4; ++i) {
		HRESULT hr = pD3dGraphicsObj->GetDevice()->CreateTexture3D(&textureDesc, NULL, &densityText[i]);
		if (FAILED(hr)){
			MessageBox(hwnd, L"Could not create the density Texture Object", L"Error", MB_OK);
			return false;
		}
		// Create the SRV and UAV.
		hr = pD3dGraphicsObj->GetDevice()->CreateShaderResourceView(densityText[i], NULL, &mDensitySP[i].mSRV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the density SRV", L"Error", MB_OK);
			return false;
		}
		hr = pD3dGraphicsObj->GetDevice()->CreateUnorderedAccessView(densityText[i], NULL, &mDensitySP[i].mUAV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the density UAV", L"Error", MB_OK);
			return false;
		}
	}

	// Create the temperature shader params
	CComPtr<ID3D11Texture3D> temperatureText[4];
	mTemperatureSP = new ShaderParams[4];
	for (int i = 0; i < 4; ++i) {
		HRESULT hr = pD3dGraphicsObj->GetDevice()->CreateTexture3D(&textureDesc, NULL, &temperatureText[i]);
		if (FAILED(hr)){
			MessageBox(hwnd, L"Could not create the temperature Texture Object", L"Error", MB_OK);
			return false;
		}
		// Create the SRV and UAV.
		hr = pD3dGraphicsObj->GetDevice()->CreateShaderResourceView(temperatureText[i], NULL, &mTemperatureSP[i].mSRV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the temperature SRV", L"Error", MB_OK);
			return false;
		}

		hr = pD3dGraphicsObj->GetDevice()->CreateUnorderedAccessView(temperatureText[i], NULL, &mTemperatureSP[i].mUAV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the temperature UAV", L"Error", MB_OK);
			return false;
		}
	}

	// Create divergence shader params
	CComPtr<ID3D11Texture3D> divergenceText;
	mDivergenceSP = unique_ptr<ShaderParams>(new ShaderParams());
	HRESULT hresult = pD3dGraphicsObj->GetDevice()->CreateTexture3D(&textureDesc, NULL, &divergenceText);
	// Create the SRV and UAV.
	hresult = pD3dGraphicsObj->GetDevice()->CreateShaderResourceView(divergenceText, NULL, &mDivergenceSP->mSRV);
	if(FAILED(hresult)) {
		MessageBox(hwnd, L"Could not create the divergence SRV", L"Error", MB_OK);
		return false;
	}
	hresult = pD3dGraphicsObj->GetDevice()->CreateUnorderedAccessView(divergenceText, NULL, &mDivergenceSP->mUAV);
	if(FAILED(hresult)) {
		MessageBox(hwnd, L"Could not create the divergence UAV", L"Error", MB_OK);
		return false;
	}

	// Create pressure shader params and render targets
	mPressureSP = new ShaderParams[2];
	CComPtr<ID3D11Texture3D> pressureText[2];	
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	for (int i = 0; i < 2; ++i) {
		HRESULT hr = pD3dGraphicsObj->GetDevice()->CreateTexture3D(&textureDesc, NULL, &pressureText[i]);
		if (FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the pressure Texture Object", L"Error", MB_OK);
			return false;
		}
		// Create the SRV and UAV.
		hr = pD3dGraphicsObj->GetDevice()->CreateShaderResourceView(pressureText[i], NULL, &mPressureSP[i].mSRV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the pressure SRV", L"Error", MB_OK);
			return false;
		}

		hr = pD3dGraphicsObj->GetDevice()->CreateUnorderedAccessView(pressureText[i], NULL, &mPressureSP[i].mUAV);
		if(FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the pressure UAV", L"Error", MB_OK);
			return false;
		}
		// Create the render target
		hr = pD3dGraphicsObj->GetDevice()->CreateRenderTargetView(pressureText[i], NULL, &mPressureRenderTargets[i]);
		if (FAILED(hr)) {
			MessageBox(hwnd, L"Could not create the pressure Render Target", L"Error", MB_OK);
			return false;
		}
	}

	// Create the constant buffers
	D3D11_BUFFER_DESC inputBufferDesc;
	inputBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	inputBufferDesc.ByteWidth = sizeof(InputBufferGeneral);
	inputBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	inputBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	inputBufferDesc.MiscFlags = 0;
	inputBufferDesc.StructureByteStride = 0;
	// General buffer
	hresult = pD3dGraphicsObj->GetDevice()->CreateBuffer(&inputBufferDesc, NULL, &mInputBufferGeneral);
	if(FAILED(hresult)) {
		return false;
	}
	// Dissipation buffer
	inputBufferDesc.ByteWidth = sizeof(InputBufferDissipation);
	hresult = pD3dGraphicsObj->GetDevice()->CreateBuffer(&inputBufferDesc, NULL, &mInputBufferDissipation);
	if(FAILED(hresult)) {
		return false;
	}
	// Impulse buffer
	inputBufferDesc.ByteWidth = sizeof(InputBufferImpulse);
	hresult = pD3dGraphicsObj->GetDevice()->CreateBuffer(&inputBufferDesc, NULL, &mInputBufferImpulse);
	if(FAILED(hresult)) {
		return false;
	}

	// Create the sampler
	D3D11_SAMPLER_DESC samplerDesc;
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	hresult = pD3dGraphicsObj->GetDevice()->CreateSamplerState(&samplerDesc, &mSampleState);
	if(FAILED(result)) {
		return false;
	}

	return true;
}

void Fluid3DCalculator::Process() {
	pD3dGraphicsObj->GetDeviceContext()->CSSetSamplers(0,1,&(mSampleState.p));

	// Set the general constant buffer
	SetGeneralBuffer();

	// Advect velocity against itself
	SetDissipationBuffer(VEL_DISSIPATION);
	Advect(mVelocitySP);

	//Advect temperature against velocity
	SetDissipationBuffer(TEMPERATURE_DISSIPATION);
	Advect(mTemperatureSP);

	// Advect density against velocity
	SetDissipationBuffer(DENSITY_DISSIPATION);
	Advect(mDensitySP);

	int resultBuffer = macCormackEnabled ? WRITE : WRITE2;
	swap(mVelocitySP[READ],mVelocitySP[resultBuffer]);
	swap(mTemperatureSP[READ],mTemperatureSP[resultBuffer]);
	swap(mDensitySP[READ],mDensitySP[resultBuffer]);

	//Determine how the temperature of the fluid changes the velocity
	mBuoyancyShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],&mTemperatureSP[READ],&mDensitySP[READ],&mVelocitySP[WRITE]);
	swap(mVelocitySP[READ],mVelocitySP[WRITE]);

	RefreshConstantImpulse();

	// Calculate the divergence of the velocity
	mDivergenceShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],mDivergenceSP.get());

	CalculatePressureGradient();

	//Use the pressure texture that was last computed. This computes divergence free velocity
	mSubtractGradientShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],&mPressureSP[READ],&mVelocitySP[WRITE]);
	std::swap(mVelocitySP[READ],mVelocitySP[WRITE]);
}

void Fluid3DCalculator::Advect(ShaderParams *target) {
	mForwardAdvectionShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],&target[READ],&target[WRITE2]);
	if (macCormackEnabled) {
		mBackwardAdvectionShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],&target[WRITE2],&target[WRITE3]);
		ShaderParams advectArrayDens[3] = {target[WRITE2], target[WRITE3], target[READ]};
		mMacCormarckAdvectionShader->Compute(pD3dGraphicsObj,&mVelocitySP[READ],advectArrayDens,&target[WRITE]);
	}
}

void Fluid3DCalculator::RefreshConstantImpulse() {
	//refresh the impulse of the density and temperature
	SetImpulseBuffer(Vector4(mDimensions.x*0.5f,mDimensions.y,mDimensions.z*0.5f,0),Vector4(IMPULSE_TEMPERATURE,IMPULSE_TEMPERATURE,IMPULSE_TEMPERATURE,0), IMPULSE_RADIUS);
	mImpulseShader->Compute(pD3dGraphicsObj,&mTemperatureSP[READ],&mTemperatureSP[WRITE]);
	swap(mTemperatureSP[READ],mTemperatureSP[WRITE]);

	SetImpulseBuffer(Vector4(mDimensions.x*0.5f,mDimensions.y,mDimensions.z*0.5f,0),Vector4(IMPULSE_DENSITY,IMPULSE_DENSITY,IMPULSE_DENSITY,0), IMPULSE_RADIUS);
	mImpulseShader->Compute(pD3dGraphicsObj,&mDensitySP[READ],&mDensitySP[WRITE]);
	swap(mDensitySP[READ],mDensitySP[WRITE]);
}

void Fluid3DCalculator::CalculatePressureGradient() {
	ID3D11DeviceContext* context = pD3dGraphicsObj->GetDeviceContext();

	// clear pressure texture to prepare for Jacobi
	float clearCol[4] = {0.0f,0.0f,0.0f,0.0f};
	context->ClearRenderTargetView(mPressureRenderTargets[READ], clearCol);

	// perform Jacobi on pressure field
	int i;
	for (i = 0; i < jacobiIterations; ++i) {		
		mJacobiShader->Compute(pD3dGraphicsObj,
			&mPressureSP[READ],
			mDivergenceSP.get(),
			&mPressureSP[WRITE]);

		swap(mPressureSP[READ],mPressureSP[WRITE]);
	}
}

void Fluid3DCalculator::SetGeneralBuffer() {
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	InputBufferGeneral* dataPtr;

	ID3D11DeviceContext *context = pD3dGraphicsObj->GetDeviceContext();

	// Lock the screen size constant buffer so it can be written to.
	HRESULT result = context->Map(mInputBufferGeneral, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if(FAILED(result)) {
		throw std::runtime_error(std::string("Fluid2DEffect: failed to map buffer in SetGeneralBuffer function"));
	}

	dataPtr = (InputBufferGeneral*)mappedResource.pData;
	dataPtr->fTimeStep = TIME_STEP;
	dataPtr->fBuoyancy = SMOKE_BUOYANCY;
	dataPtr->fDensityWeight	= SMOKE_WEIGHT;
	dataPtr->fAmbientTemperature = AMBIENT_TEMPERATURE;
	dataPtr->vDimensions = mDimensions;
	dataPtr->padding10 = 0.0f;

	context->Unmap(mInputBufferGeneral,0);

	// Set the buffer inside the compute shader
	context->CSSetConstantBuffers(0,1,&(mInputBufferGeneral.p));
}

void Fluid3DCalculator::SetDissipationBuffer(float dissipation) {
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	InputBufferDissipation* dataPtr;

	ID3D11DeviceContext *context = pD3dGraphicsObj->GetDeviceContext();

	// Lock the screen size constant buffer so it can be written to.
	HRESULT result = context->Map(mInputBufferDissipation, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if(FAILED(result)) {
		throw std::runtime_error(std::string("Fluid2DEffect: failed to map buffer in SetDissipationBuffer function"));
	}

	dataPtr = (InputBufferDissipation*)mappedResource.pData;
	dataPtr->fDissipation = dissipation;	
	dataPtr->padding1 = Vector3();

	context->Unmap(mInputBufferDissipation,0);

	// Set the buffer inside the compute shader
	context->CSSetConstantBuffers(1,1,&(mInputBufferDissipation.p));
}

void Fluid3DCalculator::SetImpulseBuffer(Vector4& point, Vector4& amount, float radius) {
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	InputBufferImpulse* dataPtr;

	ID3D11DeviceContext *context = pD3dGraphicsObj->GetDeviceContext();

	// Lock the screen size constant buffer so it can be written to.
	HRESULT result = context->Map(mInputBufferImpulse, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if(FAILED(result)) {
		throw std::runtime_error(std::string("Fluid2DEffect: failed to map buffer in SetImpulseBuffer function"));
	}

	dataPtr = (InputBufferImpulse*)mappedResource.pData;
	dataPtr->vPoint	    = point;
	dataPtr->vFillColor	= amount;
	dataPtr->fRadius    = radius;	
	dataPtr->padding2   = Vector3();

	context->Unmap(mInputBufferImpulse,0);

	// Set the buffer inside the compute shader
	context->CSSetConstantBuffers(2,1,&(mInputBufferImpulse.p));
}

ID3D11ShaderResourceView * Fluid3D::Fluid3DCalculator::GetVolumeTexture() const {
	return mDensitySP[READ].mSRV;
}