#include "stdafx.h"
#include "freecam.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "CameraEffector.h"

// absolute-positioning cam effector, view pos+dir fed from lua, optional fov and roll override
class CFreeCamEffector : public CEffectorCam
{
	Fvector m_pos;
	Fvector m_dir;
	float   m_fov;
	float   m_roll;
	bool    m_alive;
public:
	CFreeCamEffector() : CEffectorCam(eCEFreeCam, 100000.f)
	{
		m_pos.set(0.f, 0.f, 0.f);
		m_dir.set(0.f, 0.f, 1.f);
		m_fov = 0.f;
		m_roll = 0.f;
		m_alive = true;
		// full-view override, don't inject the absolute delta into the hud viewmodel
		SetHudAffect(false);
	}

	void set_target(const Fvector& p, const Fvector& d) { m_pos = p; m_dir = d; }
	void set_fov(float deg) { m_fov = deg; }
	void set_roll(float deg) { m_roll = deg; }
	void set_alive(bool v) { m_alive = v; }

	virtual BOOL Valid() { return m_alive ? TRUE : FALSE; }
	virtual bool AbsolutePositioning() { return true; }
	virtual BOOL ProcessCam(SCamEffectorInfo& info);
};

BOOL CFreeCamEffector::ProcessCam(SCamEffectorInfo& info)
{
	if (!m_alive)
		return FALSE;

	Fvector fwd = m_dir;
	if (fwd.magnitude() < EPS)
		fwd.set(0.f, 0.f, 1.f);
	fwd.normalize();

	// zero-roll basis from world up, world X fallback when dir is near-vertical
	Fvector world_up;
	world_up.set(0.f, 1.f, 0.f);
	Fvector right;
	right.crossproduct(world_up, fwd);
	if (right.magnitude() < EPS)
		right.set(1.f, 0.f, 0.f);
	right.normalize();

	Fvector up;
	up.crossproduct(fwd, right);
	up.normalize();

	if (!fis_zero(m_roll))
	{
		// rotate the right/up pair about fwd by the stored roll
		float rad = deg2rad(m_roll);
		float c = _cos(rad), s = _sin(rad);
		Fvector r2, u2;
		r2.mul(right, c).mad(up, s);
		u2.mul(up, c).mad(right, -s);
		right = r2;
		up = u2;
	}

	info.p = m_pos;
	info.d = fwd;
	info.n = up;
	if (m_fov > 0.f)
		info.fFov = m_fov;
	return TRUE;
}

// the actor cam manager owns and deletes effectors, so the live handle is whatever
// it currently holds for our type, a cached ptr would dangle across a level change
static CFreeCamEffector* find_effector(CCameraManager& cm)
{
	return smart_cast<CFreeCamEffector*>(cm.GetCamEffector(eCEFreeCam));
}

void freecam_set(float px, float py, float pz, float dx, float dy, float dz)
{
	CActor* a = Actor();
	if (!a)
		return;

	CCameraManager& cm = a->Cameras();
	CFreeCamEffector* e = find_effector(cm);
	if (!e)
	{
		e = xr_new<CFreeCamEffector>();
		cm.AddCamEffector(e);
		Msg("[freecam] activated");
	}

	Fvector p, d;
	p.set(px, py, pz);
	d.set(dx, dy, dz);
	e->set_target(p, d);
	e->set_alive(true);
}

void freecam_release()
{
	CActor* a = Actor();
	if (!a)
		return;

	CFreeCamEffector* e = find_effector(a->Cameras());
	if (e)
	{
		e->set_roll(0.f);
		e->set_alive(false);
		Msg("[freecam] released");
	}
}

bool freecam_active()
{
	CActor* a = Actor();
	if (!a)
		return false;

	CFreeCamEffector* e = find_effector(a->Cameras());
	return e != NULL && e->Valid() == TRUE;
}

// fov clamp mirrors the "fov" console command bounds, console_commands.cpp:2664
static const float FREECAM_FOV_MIN = 5.f;
static const float FREECAM_FOV_MAX = 180.f;

void freecam_set_fov(float deg)
{
	CActor* a = Actor();
	if (!a)
		return;

	CFreeCamEffector* e = find_effector(a->Cameras());
	if (!e)
		return;

	if (deg <= 0.f)
	{
		e->set_fov(0.f);
		return;
	}

	clamp(deg, FREECAM_FOV_MIN, FREECAM_FOV_MAX);
	e->set_fov(deg);
}

void freecam_set_roll(float deg)
{
	CActor* a = Actor();
	if (!a)
		return;

	CFreeCamEffector* e = find_effector(a->Cameras());
	if (!e)
		return;

	e->set_roll(deg);
}
