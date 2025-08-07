#pragma once
#include "../xrGame/ParticlesObject.h"

class CParticlesObject;

class CParticlesAsync
{
public:
	static void Play();
	static void Wait();

	static bool Push(CParticlesObject* Obj);
	static void Pop(CParticlesObject* Obj);
	
	static void ForceUpdate(CParticlesObject* Obj);
private:
	void UpdateParticle(CParticlesObject* particle) const;

private:
	xr_list<CParticlesObject*> Particles;
	volatile bool IsStarted = false;
};