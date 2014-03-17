/********************************************************************
Fluid3DCalculator.h: Encapsulates a 3D fluid simulation
being calculated on the GPU.

Author:	Valentin Hinov
Date: 18/2/2014
*********************************************************************/

#ifndef _FLUID3DCALCULATOR_H
#define _FLUID3DCALCULATOR_H

#include <vector>
#include <memory>
#include "../AtlInclude.h"

#include "../../display/D3DGraphicsObject.h"
#include "FluidSettings.h"

struct ShaderParams;

namespace Fluid3D {

class AdvectionShader;
class ImpulseShader;
class JacobiShader ;
class DivergenceShader;
class SubtractGradientShader;
class BuoyancyShader;
class VorticityShader;
class ConfinementShader;

class Fluid3DCalculator {
public:
	Fluid3DCalculator(FluidSettings fluidSettings);
	~Fluid3DCalculator();

	bool Initialize(_In_ D3DGraphicsObject* d3dGraphicsObj, HWND hwnd);
	void Process();

	ID3D11ShaderResourceView * GetVolumeTexture() const;

	const FluidSettings &GetFluidSettings() const;
	FluidSettings * const GetFluidSettingsPointer() const;
	void SetFluidSettings(const FluidSettings &fluidSettings);

private:
	bool InitShaders(HWND hwnd);
	bool InitShaderParams(HWND hwnd);
	bool InitBuffersAndSamplers();

	void Advect(ShaderParams *target, SystemAdvectionType_t advectionType, float dissipation);
	void RefreshConstantImpulse();
	void ApplyBuoyancy();
	void ComputeVorticityConfinement();
	void CalculatePressureGradient();

	enum DissipationBufferType_t {
		DENSITY, VELOCITY, TEMPERATURE
	};
	void UpdateAdvectionBuffer(float dissipation, float timeModifier);
	void UpdateGeneralBuffer();
	void UpdateImpulseBuffer(Vector3& point, float amount, float radius);

	int  GetUpdateDirtyFlags(const FluidSettings &newSettings) const;
private:
	D3DGraphicsObject* pD3dGraphicsObj;

	FluidSettings mFluidSettings;

	std::unique_ptr<AdvectionShader>			mAdvectionShader;
	std::unique_ptr<AdvectionShader>			mMacCormarckAdvectionShader;
	std::unique_ptr<ImpulseShader>				mImpulseShader;
	std::unique_ptr<VorticityShader>			mVorticityShader;
	std::unique_ptr<ConfinementShader>			mConfinementShader;
	std::unique_ptr<JacobiShader>				mJacobiShader;
	std::unique_ptr<DivergenceShader>			mDivergenceShader;
	std::unique_ptr<SubtractGradientShader>		mSubtractGradientShader;
	std::unique_ptr<BuoyancyShader>				mBuoyancyShader;

	ShaderParams* mVelocitySP;
	ShaderParams* mDensitySP;
	ShaderParams* mTemperatureSP;
	ShaderParams* mPressureSP;
	std::unique_ptr<ShaderParams>			mObstacleSP;
	std::unique_ptr<ShaderParams>			mVorticitySP;
	std::unique_ptr<ShaderParams>			mDivergenceSP;
	CComPtr<ID3D11RenderTargetView>			mPressureRenderTargets[2];

	CComPtr<ID3D11Buffer>					mInputBufferGeneral;
	CComPtr<ID3D11Buffer>					mInputBufferImpulse;
	CComPtr<ID3D11Buffer>					mInputBufferAdvection;
	CComPtr<ID3D11SamplerState>				mSampleState;
};

}

#endif