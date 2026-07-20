#include "stdafx.h"
#include "freecam.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "CameraEffector.h"

// absolute-positioning cam effector, view pos+dir fed from lua, zero roll, fov untouched
class CFreeCamEffector : public CEffectorCam
{
	Fvector m_pos;
	Fvector m_dir;
	bool    m_alive;
public:
	CFreeCamEffector() : CEffectorCam(eCEFreeCam, 100000.f)
	{
		m_pos.set(0.f, 0.f, 0.f);
		m_dir.set(0.f, 0.f, 1.f);
		m_alive = true;
		// full-view override, don't inject the absolute delta into the hud viewmodel
		SetHudAffect(false);
	}

	void set_target(const Fvector& p, const Fvector& d) { m_pos = p; m_dir = d; }
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

	info.p = m_pos;
	info.d = fwd;
	info.n = up;
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
		e->set_alive(false);
		Msg("[freecam] released");
	}
}
