#pragma once

#include "HUDRecon.h"
#include "HUDCrosshair.h"

class CHUDManager;
class CLAItem;

struct SPickParam;

struct TargetCrosshair
{
	CHUDCrosshair crosshair;
	Fvector pos;
	float opacity;
};

class CHUDTarget
{
private:
	bool m_bShowCrosshair;

	CHUDRecon HUDRecon;
	TargetCrosshair m_crosshairActive;
	TargetCrosshair m_crosshairCamera;
	TargetCrosshair m_crosshairWeaponNear;
	TargetCrosshair m_crosshairWeaponFar;
	TargetCrosshair m_crosshairDeviceNear;
	TargetCrosshair m_crosshairDeviceFar;

	ui_shader shaderWire;

private:
	float GetUIDist(const SPickParam& pp) const;
	float GetTargetOpacity(const SPickParam& pp) const;
	void IntegratePosition(const SPickParam& pp, TargetCrosshair& crosshair);
	void IntegrateOpacity(const SPickParam& pp, TargetCrosshair& crosshair);
	void RenderNearCrosshair(const SPickParam& pp, TargetCrosshair& crosshair);
	void RenderFarCrosshair(const SPickParam& pp, TargetCrosshair& crosshair, bool draw_recon);
	void RenderAimLine(Fvector va, const TargetCrosshair& crosshair_near, const TargetCrosshair& crosshair_far);

public:
	CHUDTarget();
	~CHUDTarget();
	void Render();
	void Load();
	CHUDCrosshair& GetHUDCrosshair() { return m_crosshairActive.crosshair; }
	void ShowCrosshair(bool b);
};
