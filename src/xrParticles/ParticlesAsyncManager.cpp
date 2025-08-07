#include "stdafx.h"
#include "ParticlesAsyncManager.h"

#include "../Include/xrRender/ParticleCustom.h"
#include "../Include/xrRender/RenderVisual.h"

static CParticlesAsync Instance;
static xr_task_group ParticleObjectTasks;

void CParticlesAsync::Play()
{
	PROF_EVENT();
	Instance.IsStarted = true;

	for (CParticlesObject* particle : Instance.Particles)
	{
		if (particle->m_bDead)
			continue;

		if (psDeviceFlags.test(mtParticles))
		{
			ParticleObjectTasks.run
			(
				[particle]()
				{
					Instance.UpdateParticle(particle);
				}
			);
		}
		else
		{
			Instance.UpdateParticle(particle);
		}
	}
}

void CParticlesAsync::Wait()
{
	if (psDeviceFlags.test(mtParticles))
	{
		PROF_EVENT("Particles Wait");
		ParticleObjectTasks.wait();
	}

	Instance.IsStarted = false;
}

bool CParticlesAsync::Push(CParticlesObject* Obj)
{
	Instance.Particles.push_back(Obj);

	return Instance.IsStarted;
}

void CParticlesAsync::Pop(CParticlesObject* Obj)
{
	Wait();

	Instance.Particles.remove(Obj);
}

void CParticlesAsync::ForceUpdate(CParticlesObject* Obj)
{
	if (!Instance.IsStarted)
		return;

	Instance.UpdateParticle(Obj);
}

void CParticlesAsync::UpdateParticle(CParticlesObject* particle) const
{
	PROF_THREAD("Particles Worker");
	u32 dt = Device.dwTimeGlobal - particle->dwLastTime;
	IParticleCustom* V = smart_cast<IParticleCustom*>(particle->renderable.visual);

	if (V == nullptr)
		return;

	PROF_EVENT("Particle OnFrame");
	V->OnFrame(dt);

	particle->dwLastTime = Device.dwTimeGlobal;
}