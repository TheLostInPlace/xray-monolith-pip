#include "stdafx.h"
#include "HUDTarget.h"

#include "player_hud.h"
#include "HUDManager.h"
#include "HUDItem.h"
#include "Actor.h"

#define C_DEFAULT	D3DCOLOR_RGBA(0xff,0xff,0xff,0x80)

static float lerp(float a, float b, float t)
{
	clamp(t, 0.f, 1.f);
	return a * (1 - t) + b * t;
}

CHUDTarget::CHUDTarget()
{
	shaderWire->create("hud\\crosshair");
	Load();
	m_bShowCrosshair = false;
}

CHUDTarget::~CHUDTarget()
{
}

float crosshair_occluded_opacity = .6f;
float crosshair_occlusion_fade_rate = 20.f;
float crosshair_distance_lerp_rate = 40.f;

void CHUDTarget::Load()
{
	m_crosshairActive.crosshair.Load();
	m_crosshairCamera.crosshair.Load();
	m_crosshairWeaponNear.crosshair.Load();
	m_crosshairWeaponFar.crosshair.Load();
	m_crosshairDeviceNear.crosshair.Load();
	m_crosshairDeviceFar.crosshair.Load();
}

void CHUDTarget::ShowCrosshair(bool b)
{
	m_bShowCrosshair = b;
}

extern ENGINE_API BOOL g_bRendering;
u32 g_crosshair_color = C_DEFAULT;

float CHUDTarget::GetUIDist(const SPickParam& pp) const
{
	return pp.result.range;
}

float CHUDTarget::GetTargetOpacity(const SPickParam& pp) const
{
	if (pp.barrel_blocked)
	{
		return 1.f;
	}

	// If the barrel is not occluded...
	// Test whether the aim point is occluded
	Fvector dir = Fvector().sub(
		Fvector().add(pp.defs.start, Fvector().mul(pp.defs.dir, GetUIDist(pp))),
		Device.vCameraPosition
	);

	float dist = dir.magnitude();
	dir.normalize();
	SPickParam op = SPickParam();
	op.defs.start = Device.vCameraPosition;
	op.defs.dir = dir;
	op.defs.range = dist * 0.99f;
	if (!HUD().DoPick(op))
	{
		return 1.f;
	}

	// If it is, apply fade
	return crosshair_occluded_opacity;
}

void CHUDTarget::IntegratePosition(const SPickParam& pp, TargetCrosshair& crosshair)
{
	// Transform ray start and direction into camera space
	Fvector pos = pp.defs.start;
	Fvector dir = pp.defs.dir;
	Device.mView.transform_tiny(pos);
	Device.mView.transform_dir(dir);

	float dist = GetUIDist(pp);
	Fvector target;
	if (dist > 0.f)
		target = Fvector().add(pos, Fvector().mul(dir, dist));
	else
		target = Fvector().add(pos, Fvector().mul(dir, pp.barrel_dist));

	// Interpolate crosshair position toward target
	if (psHUD_Flags.is(HUD_CROSSHAIR_DISTANCE_LERP))
	{
		float zFar = g_pGamePersistent->Environment().CurrentEnv->far_plane;
		float fac = 1 - (target.z / zFar);
		float t = Device.fTimeDelta * (fac + crosshair_distance_lerp_rate);
		clamp(t, 0.f, 1.f);
		crosshair.pos.lerp(crosshair.pos, target, t);
	}
	else
		crosshair.pos = target;
}

void CHUDTarget::IntegrateOpacity(const SPickParam& pp, TargetCrosshair& crosshair)
{
	float opacity_target = GetTargetOpacity(pp);

	// Interpolate opacity offset toward target
	crosshair.opacity = lerp(crosshair.opacity, opacity_target, Device.fTimeDelta * crosshair_occlusion_fade_rate);
}

void CHUDTarget::RenderNearCrosshair(const SPickParam& pp, TargetCrosshair& crosshair)
{
	IntegratePosition(pp, crosshair);
	IntegrateOpacity(pp, crosshair);

	BOOL b_do_rendering = (psHUD_Flags.is(HUD_CROSSHAIR | HUD_CROSSHAIR_RT | HUD_CROSSHAIR_RT2));
	if (!b_do_rendering)
		return;

	VERIFY(g_bRendering);

	// Construct aim point matrix
	Fmatrix mat_aim = Fmatrix().identity();
	mat_aim.mulB_43(Device.mInvView);
	mat_aim.mulB_43(Fmatrix().translate(crosshair.pos));

	// Readout color
	u32 color_readout = C_DEFAULT;

	HUDRecon.SetTransform(mat_aim);
	HUDRecon.SetColor(color_readout);
	HUDRecon.OnRender(GetUIDist(pp));

	if (m_bShowCrosshair)
	{
		// Use the crosshair color unless the readout color is non-default
		color_readout = HUDRecon.GetColor();
		u32 color_crosshair = color_readout == C_DEFAULT ? g_crosshair_color : color_readout;

		// Modulate color alpha
		DWORD alpha_mask = 0xff000000;
		color_crosshair = (color_crosshair | alpha_mask)

			& ((DWORD)(alpha_mask * crosshair.opacity) | (~alpha_mask));

		// If firepos is active
		if (HUD().FireposActive())
		{
			// Rotate the crosshair
			Fvector hpb_barrel, hpb_cam;
			pp.barrel_matrix.getHPB(hpb_barrel);
			Device.mInvView.getHPB(hpb_cam);
			mat_aim.mulB_43(Fmatrix().setHPB(0, 0, hpb_barrel.z - hpb_cam.z));
		}

		// Update the crosshair's transform and color, and draw it
		crosshair.crosshair.SetTransform(mat_aim);
		crosshair.crosshair.SetColor(color_crosshair);
		crosshair.crosshair.OnRender();
	}
}

void CHUDTarget::RenderFarCrosshair(const SPickParam& pp, TargetCrosshair& crosshair, bool draw_recon)
{
	// Transform ray start and direction into camera space
	Fvector pos = pp.barrel_matrix.c;
	Fvector dir = pp.barrel_matrix.k;
	Device.hud_to_world(pos);
	Device.hud_to_world_dir(dir);
	Device.mView.transform_tiny(pos);
	Device.mView.transform_dir(dir);

	float zFar = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	crosshair.pos = Fvector().add(pos, Fvector().mul(dir, zFar));

	crosshair.opacity = 1.f;
	//crosshair.opacity = crosshair_occluded_opacity;

	BOOL b_do_rendering = (psHUD_Flags.is(HUD_CROSSHAIR | HUD_CROSSHAIR_RT | HUD_CROSSHAIR_RT2));
	if (!b_do_rendering)
		return;

	VERIFY(g_bRendering);

	// Construct aim point matrix
	Fmatrix mat_aim = Fmatrix().identity();
	mat_aim.mulB_43(Device.mInvView);
	mat_aim.mulB_43(Fmatrix().translate(crosshair.pos));

	// Readout color
	u32 color_readout = C_DEFAULT;

	if (draw_recon)
	{
		HUDRecon.SetTransform(mat_aim);
		HUDRecon.SetColor(color_readout);
		HUDRecon.OnRender(zFar);
	}

	if (m_bShowCrosshair)
	{
		// Use the crosshair color unless the readout color is non-default
		color_readout = HUDRecon.GetColor();
		u32 color_crosshair = color_readout == C_DEFAULT ? g_crosshair_color : color_readout;

		// Modulate color alpha
		DWORD alpha_mask = 0xff000000;
		color_crosshair = (color_crosshair | alpha_mask)

			& ((DWORD)(alpha_mask * crosshair.opacity) | (~alpha_mask));

		// If firepos is active
		if (HUD().FireposActive())
		{
			// Rotate the crosshair
			Fvector hpb_barrel, hpb_cam;
			pp.barrel_matrix.getHPB(hpb_barrel);
			Device.mInvView.getHPB(hpb_cam);
			mat_aim.mulB_43(Fmatrix().setHPB(0, 0, hpb_barrel.z - hpb_cam.z));
		}

		// Update the crosshair's transform and color, and draw it
		crosshair.crosshair.SetTransform(mat_aim);
		crosshair.crosshair.SetColor(color_crosshair);
		crosshair.crosshair.OnRender();
	}
}

void CHUDTarget::RenderAimLine(Fvector va, const TargetCrosshair& crosshair_near, const TargetCrosshair& crosshair_far)
{
	BOOL b_do_rendering = (psHUD_Flags.is(HUD_CROSSHAIR | HUD_CROSSHAIR_RT | HUD_CROSSHAIR_RT2));
	if (!b_do_rendering)
		return;

	VERIFY(g_bRendering);

	Fvector2 scr_size = {
		float(::Render->getTarget()->get_width()),
		float(::Render->getTarget()->get_height())
	};

	UIRender->StartPrimitive(2, IUIRender::ptLineStrip, UI().m_currentPointType);

	Device.hud_to_world(va);
	Fvector vb = crosshair_near.pos;
	Device.mInvView.transform_tiny(vb);
	Fvector vc = crosshair_far.pos;
	Device.mInvView.transform_tiny(vc);

	Device.mFullTransform.transform(va);
	Device.mFullTransform.transform(vb);
	Device.mFullTransform.transform(vc);

	va.x = (va.x + 1.f) * 0.5f * scr_size.x;
	va.y = (-va.y + 1.f) * 0.5f * scr_size.y;
	vb.x = (vb.x + 1.f) * 0.5f * scr_size.x;
	vb.y = (-vb.y + 1.f) * 0.5f * scr_size.y;
	vc.x = (vc.x + 1.f) * 0.5f * scr_size.x;
	vc.y = (-vc.y + 1.f) * 0.5f * scr_size.y;

	UIRender->PushPoint(va.x, va.y, 0, C_DEFAULT, 0, 0);
	UIRender->PushPoint(vb.x, vb.y, 0, C_DEFAULT, 0, 0);
	UIRender->PushPoint(vc.x, vc.y, 0, C_DEFAULT, 0, 0);

	UIRender->SetShader(*shaderWire);
	UIRender->FlushPrimitive();
}

void CHUDTarget::Render()
{
	CObject* O = Level().CurrentEntity();
	if (0 == O) return;
	CEntity* E = smart_cast<CEntity*>(O);
	if (0 == E) return;

	//RenderNearCrosshair(Actor()->GetPick(), m_crosshairActive);

	RenderFarCrosshair(HUD().GetPick(), m_crosshairCamera, true);
	
	auto pWeapon = g_player_hud->attached_item(0);
	if (pWeapon)
	{
		auto pItem = pWeapon->m_parent_hud_item;
		if (pItem)
		{
			RenderNearCrosshair(pItem->GetPick(), m_crosshairWeaponNear);
			RenderFarCrosshair(pItem->GetPick(), m_crosshairWeaponFar, false);
			RenderAimLine(pItem->GetPick().barrel_matrix.c, m_crosshairWeaponNear, m_crosshairWeaponFar);
		}
	}

	auto pDevice = g_player_hud->attached_item(1);
	if (pDevice)
	{
		auto pItem = pDevice->m_parent_hud_item;
		if (pItem)
		{
			RenderNearCrosshair(pItem->GetPick(), m_crosshairDeviceNear);
			RenderFarCrosshair(pItem->GetPick(), m_crosshairDeviceFar, false);
			RenderAimLine(pItem->GetPick().barrel_matrix.c, m_crosshairDeviceNear, m_crosshairDeviceFar);
		}
	}
}
