// HOM.cpp: implementation of the CHOM class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "HOM.h"
#include "occRasterizer.h"
#include "occ_engine.h"
#include "svp_console.h"
#include "../../xrEngine/GameFont.h"

#include "../../xrCore/profiler.h"

#include "dxRenderDeviceRender.h"

float psOSSR = .001f;

void __stdcall CHOM::MT_RENDER()
{
	PROF_EVENT("Render HOM");

	xrCriticalSectionGuard guard(m_mt_render_guard);
	const u32 current_frame = Device.dwFrame;

	bool b_main_menu_is_active = (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
	if (b_main_menu_is_active)
	{
		MT_frame_rendered.store(current_frame, std::memory_order_release);
		return;
	}

	if (MT_frame_rendered.load(std::memory_order_acquire) != current_frame)
	{
		if (ps_r__svp_stats && GetCurrentThreadId() == m_mt_render_registration_tid)
			++svp_stats_hom_main_thread; // ran on the queuing thread, the worker task never picked it up

		CFrustum ViewBase;
		ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
		Enable();
		Render(ViewBase);
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CHOM::CHOM()
{
	bEnabled = FALSE;
	m_pModel = 0;
	m_pTris = 0;
    MT_frame_rendered.store(0, std::memory_order_relaxed);
    m_mt_render_registration_tid = 0;
	m_occ_mode = 0;
	m_occ_mode_logged = -1;
	m_occ_res_logged = -1;
	m_occ_time_queries = false;
	m_occ_raster = &occ_engine_legacy();
	m_occ_masked = nullptr;
	m_occ_query = m_occ_raster;
#ifdef DEBUG
	Device.seqRender.Add(this,REG_PRIORITY_LOW-1000);
#endif
}

CHOM::~CHOM()
{
#ifdef DEBUG
	Device.seqRender.Remove(this);
#endif
}

#pragma pack(push,4)
struct HOM_poly
{
	Fvector v1, v2, v3;
	u32 flags;
};
#pragma pack(pop)

IC float Area(Fvector& v0, Fvector& v1, Fvector& v2)
{
	float e1 = v0.distance_to(v1);
	float e2 = v0.distance_to(v2);
	float e3 = v1.distance_to(v2);

	float p = (e1 + e2 + e3) / 2.f;
	return _sqrt(p * (p - e1) * (p - e2) * (p - e3));
}

void CHOM::Load()
{
	// Find and open file
	string_path fName;
	FS.update_path(fName, "$level$", "level.hom");
	if (!FS.exist(fName))
	{
		Msg(" WARNING: Occlusion map '%s' not found.", fName);
		return;
	}
	Msg("* Loading HOM: %s", fName);

	IReader* fs = FS.r_open(fName);
	IReader* S = fs->open_chunk(1);

	// Load tris and merge them
	CDB::Collector CL;
	while (!S->eof())
	{
		HOM_poly P;
		S->r(&P, sizeof(P));
		CL.add_face_packed_D(P.v1, P.v2, P.v3, P.flags, 0.01f);
	}

	// Determine adjacency
	xr_vector<u32> adjacency;
	CL.calc_adjacency(adjacency);

	// Create RASTER-triangles
	m_pTris = xr_alloc<occTri>(u32(CL.getTS()));
	for (u32 it = 0; it < CL.getTS(); it++)
	{
		CDB::TRI& clT = CL.getT()[it];
		occTri& rT = m_pTris[it];

		Fvector& v0 = CL.getV()[clT.verts[0]];
		Fvector& v1 = CL.getV()[clT.verts[1]];
		Fvector& v2 = CL.getV()[clT.verts[2]];

		rT.adjacent[0] = (0xffffffff == adjacency[3 * it + 0]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 0]);
		rT.adjacent[1] = (0xffffffff == adjacency[3 * it + 1]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 1]);
		rT.adjacent[2] = (0xffffffff == adjacency[3 * it + 2]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 2]);
		rT.flags = clT.dummy;
		rT.area = Area(v0, v1, v2);

		if (rT.area < EPS_L)
		{
			Msg("! Invalid HOM triangle (%f,%f,%f)-(%f,%f,%f)-(%f,%f,%f)", VPUSH(v0), VPUSH(v1), VPUSH(v2));
		}

		rT.plane.build(v0, v1, v2);
		rT.skip = 0;
		rT.center.add(v0, v1).add(v2).div(3.f);
	}

	// Create AABB-tree
	m_pModel = xr_new<CDB::MODEL>();
	m_pModel->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
	bEnabled = TRUE;
	occ_engine_moc_reserve(u32(CL.getTS()));
	S->close();
	FS.r_close(fs);

	if (ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))
	{
		// MT-HOM (@front), ahead of the grass MT_CALC so every occlusion query reads a finished frame
		Device.seqParallelRender.insert(Device.seqParallelRender.begin(), xr_make_delegate(this, &CHOM::MT_RENDER));
		m_mt_render_registration_tid = GetCurrentThreadId(); // the thread that queues the worker call
	}
}

void CHOM::Unload()
{
	xr_delete(m_pModel);
	xr_free(m_pTris);
	bEnabled = FALSE;
	occ_engine_moc_release();

	auto I = std::find(Device.seqParallelRender.begin(), Device.seqParallelRender.end(), xr_make_delegate(this, &CHOM::MT_RENDER));
	if (I != Device.seqParallelRender.end())
		Device.seqParallelRender.erase(I);
}

class pred_fb
{
public:
	occTri* m_pTris;
	Fvector camera;
public:
	pred_fb(occTri* _t) : m_pTris(_t)
	{
	}

	pred_fb(occTri* _t, Fvector& _c) : m_pTris(_t), camera(_c)
	{
	}

	ICF bool operator()(const CDB::RESULT& _1, const CDB::RESULT& _2) const
	{
		occTri& t0 = m_pTris[_1.id];
		occTri& t1 = m_pTris[_2.id];
		return camera.distance_to_sqr(t0.center) < camera.distance_to_sqr(t1.center);
	}

	ICF bool operator()(const CDB::RESULT& _1) const
	{
		occTri& T = m_pTris[_1.id];
		return T.skip > Device.dwFrame;
	}
};

void CHOM::Render_DB(CFrustum& base)
{
	// Query DB
	xrc.frustum_options(0);
	xrc.frustum_query(m_pModel, base);
	if (0 == xrc.r_count()) return;

	// Prepare
	CDB::RESULT* it = xrc.r_begin();
	CDB::RESULT* end = xrc.r_end();

	Fvector COP = Device.vCameraPosition;
	// only an engine driven by per-tri pixel feedback carries the skip cadence
	const bool skip_filter = m_occ_query->wants_skip_filter();
	if (skip_filter)
		end = std::remove_if(it, end, pred_fb(m_pTris));
	std::sort(it, end, pred_fb(m_pTris, COP));

	u32 _frame = Device.dwFrame;
	const bool tally = (ps_r__svp_stats != 0);
	if (tally)
	{
		svp_stats_hom_tris_in = u32(end - it);
		svp_stats_hom_tris_emitted = 0;
	}
#ifdef DEBUG
	tris_in_frame				= xrc.r_count();
#endif

	// Perfrom selection, sorting, culling
	for (; it != end; it++)
	{
		// Control skipping
		occTri& T = m_pTris[it->id];
		u32 next = _frame + ::Random.randI(3, 10);

		// Test for good occluder - should be improved :)
		if (!(T.flags || (T.plane.classify(COP) > 0)))
		{
			if (skip_filter)
				T.skip = next;
			continue;
		}
		if (tally)
			++svp_stats_hom_tris_emitted;

		// Access to triangle vertices
		CDB::TRI& t = m_pModel->get_tris()[it->id];
		Fvector* v = m_pModel->get_verts();
		const Fvector tri[3] = { v[t.verts[0]], v[t.verts[1]], v[t.verts[2]] };

		// the masked engine takes every facing tri, the legacy clip and pixel tests never gate it
		if (m_occ_masked)
			m_occ_masked->emit(T, tri);

		if (m_occ_raster && !m_occ_raster->emit(T, tri))
		{
			if (skip_filter)
				T.skip = next;
		}
	}
}

// picks this frame's engines once, no query ever reads the request
void CHOM::latch_engine(float near_w)
{
	const int mode = (ps_r__hom_engine < 0) ? 0 : ((ps_r__hom_engine > 2) ? 2 : ps_r__hom_engine);
	const int res = (ps_r__hom_moc_res < 0) ? 0 : ((ps_r__hom_moc_res > 2) ? 2 : ps_r__hom_moc_res);

	m_occ_mode = mode;
	m_occ_time_queries = (ps_r__svp_stats >= 2);
	m_occ_raster = (1 == mode) ? nullptr : &occ_engine_legacy();
	m_occ_masked = (0 == mode) ? nullptr : &occ_engine_moc();
	m_occ_query = (1 == mode) ? m_occ_masked : m_occ_raster;

	u32 rw = 0, rh = 0;
	if (m_occ_masked)
	{
		occ_engine_moc_set_res(res);
		occ_engine_moc_preset_size(res, rw, rh);
	}
	svp_stats_hom_engine = u32(mode);
	svp_stats_hom_res_w = rw;
	svp_stats_hom_res_h = rh;

	if (mode != m_occ_mode_logged || res != m_occ_res_logged)
	{
		m_occ_mode_logged = mode;
		m_occ_res_logged = res;
		// no masked buffer under engine 0 so there is no resolution to name
		if (m_occ_masked)
			Msg("* [MOC] engine %d res %ux%u near %.3f", mode, rw, rh, near_w);
		else
			Msg("* [MOC] engine %d res n/a near %.3f", mode, near_w);
	}
}

void CHOM::Render(CFrustum& base)
{
	if (!bEnabled) return;

	// clip space near from the projection, w carries view z under it
	const float pz = -Device.mProject._33;
	const float near_w = (_abs(pz) > EPS) ? (Device.mProject._43 / pz) : 0.f;
	latch_engine(near_w);

	// the occlusion frame starts here on whichever thread renders it, the latch just wrote engine and res
	// a menu frame never reaches this so the counters keep the last rendered frame
	svp_stats_hom_tris_in = 0;
	svp_stats_hom_tris_emitted = 0;
	svp_stats_hom_render_us = 0;
	svp_stats_hom_test_ticks = 0;

	Device.Statistic->RenderCALC_HOM.Begin();
	const u64 t0 = CPU::QPC();
	if (m_occ_raster) m_occ_raster->begin_frame(Device.mFullTransform, Device.vCameraPosition, near_w);
	if (m_occ_masked) m_occ_masked->begin_frame(Device.mFullTransform, Device.vCameraPosition, near_w);
	Render_DB(base);
	if (m_occ_raster) m_occ_raster->end_frame();
	if (m_occ_masked) m_occ_masked->end_frame();
	svp_stats_hom_render_us = u32((CPU::QPC() - t0) * 1000000ull / CPU::qpc_freq);
	MT_frame_rendered.store(Device.dwFrame, std::memory_order_release);
	Device.Statistic->RenderCALC_HOM.End();
}

// shadow compare bookkeeping, legacy answers and the masked result only feeds the divergence tally
ICF void hom_shadow_note(BOOL primary, BOOL other)
{
	++svp_stats_hom_shadow_queries;
	if (!!primary != !!other)
		++svp_stats_hom_disagree;
}

BOOL CHOM::query_box(const Fbox& B)
{
	const u64 t0 = m_occ_time_queries ? CPU::QPC() : 0;
	BOOL result = m_occ_query->test_box(B);
	if (2 == m_occ_mode)
		hom_shadow_note(result, m_occ_masked->test_box(B));
	if (m_occ_time_queries)
		svp_stats_hom_test_ticks += CPU::QPC() - t0;
	return result;
}

BOOL CHOM::query_poly(const Fvector* v, u32 n)
{
	const u64 t0 = m_occ_time_queries ? CPU::QPC() : 0;
	BOOL result = m_occ_query->test_poly(v, n);
	if (2 == m_occ_mode)
		hom_shadow_note(result, m_occ_masked->test_poly(v, n));
	if (m_occ_time_queries)
		svp_stats_hom_test_ticks += CPU::QPC() - t0;
	return result;
}

BOOL CHOM::visible(Fbox3& B)
{
	if (!bEnabled) return TRUE;
	if (B.contains(Device.vCameraPosition)) return TRUE;
	return query_box(B);
}

BOOL CHOM::visible(Fbox2& B, float depth)
{
	// reads the legacy buffer directly, it is frozen under any other engine and has no caller
	VERIFY(0 == m_occ_mode);
	if (!bEnabled) return TRUE;
	return Raster.test(B.min.x, B.min.y, B.max.x, B.max.y, depth);
}

BOOL CHOM::visible(Fsphere& S)
{
	Fbox B;
	B.setb(S.P,Fvector().set(S.R, S.R, S.R));
	return visible(B);
}

BOOL CHOM::visible(vis_data& vis)
{
	if (Device.dwFrame < vis.hom_frame) return TRUE; // not at this time :)
	if (!bEnabled) return TRUE; // return - everything visible

	// Now, the test time comes
	// 0. The object was hidden, and we must prove that each frame	- test		| frame-old, tested-new, hom_res = false;
	// 1. The object was visible, but we must to re-check it		- test		| frame-new, tested-???, hom_res = true;
	// 2. New object slides into view								- delay test| frame-old, tested-old, hom_res = ???;
	u32 frame_current = Device.dwFrame;
	// u32	frame_prev		= frame_current-1;

#ifdef DEBUG
	Device.Statistic->RenderCALC_HOM.Begin	();
#endif
	BOOL result = query_box(vis.box);
	u32 delay = 1;
	if (result)
	{
		// visible	- delay next test, hashed per object and frame so worker and main queries never share a draw
		u32 h = u32(uintptr_t(&vis) >> 4) ^ (frame_current * 2654435761u);
		h ^= h >> 15; h *= 2246822519u;
		h ^= h >> 13; h *= 3266489917u;
		h ^= h >> 16;
		delay = 10 + (h % 15);
	}
	else
	{
		// hidden	- shedule to next frame
	}
	vis.hom_frame = frame_current + delay;
	vis.hom_tested = frame_current;
#ifdef DEBUG
	Device.Statistic->RenderCALC_HOM.End	();
#endif

	return result;
}

BOOL CHOM::visible(sPoly& P)
{
	if (!bEnabled) return TRUE;
	return query_poly(&P.front(), P.size());
}

void CHOM::Disable()
{
	bEnabled = FALSE;
}

void CHOM::Enable()
{
	bEnabled = m_pModel ? TRUE : FALSE;
}

#ifdef DEBUG
void CHOM::OnRender	()
{
	// the pixel dump reads the legacy buffer, it is stale under any other engine
	if (0 == m_occ_mode)
		Raster.on_dbg_render();

	if (psDeviceFlags.is(rsOcclusionDraw)){
		if (m_pModel){
			DEFINE_VECTOR		(FVF::L,LVec,LVecIt);
			static LVec	poly;	poly.resize(m_pModel->get_tris_count()*3);
			static LVec	line;	line.resize(m_pModel->get_tris_count()*6);
			for (int it=0; it<m_pModel->get_tris_count(); it++){
				CDB::TRI* T		= m_pModel->get_tris()+it;
				Fvector* verts	= m_pModel->get_verts();
				poly[it*3+0].set(*(verts+T->verts[0]),0x80FFFFFF);
				poly[it*3+1].set(*(verts+T->verts[1]),0x80FFFFFF);
				poly[it*3+2].set(*(verts+T->verts[2]),0x80FFFFFF);
				line[it*6+0].set(*(verts+T->verts[0]),0xFFFFFFFF);
				line[it*6+1].set(*(verts+T->verts[1]),0xFFFFFFFF);
				line[it*6+2].set(*(verts+T->verts[1]),0xFFFFFFFF);
				line[it*6+3].set(*(verts+T->verts[2]),0xFFFFFFFF);
				line[it*6+4].set(*(verts+T->verts[2]),0xFFFFFFFF);
				line[it*6+5].set(*(verts+T->verts[0]),0xFFFFFFFF);
			}
			RCache.set_xform_world(Fidentity);
			// draw solid
			Device.SetNearer(TRUE);
			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_Draw		(D3DPT_TRIANGLELIST,&*poly.begin(),poly.size()/3);
			Device.SetNearer(FALSE);
			// draw wire
			if (bDebug){
				RImplementation.rmNear();
			}else{
				Device.SetNearer(TRUE);
			}
			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_Draw		(D3DPT_LINELIST,&*line.begin(),line.size()/2);
			if (bDebug){
				RImplementation.rmNormal();
			}else{
				Device.SetNearer(FALSE);
			}
		}
	}
}
void CHOM::stats()
{
	if (m_pModel){
		CGameFont& F		= *Device.Statistic->Font();
		F.OutNext			(" **** HOM-occ ****");
		if (0 == m_occ_mode)
			F.OutNext		("  visible:  %2d", occ_legacy_dbg_visible());
		else
			F.OutNext		("  occ moc");
		F.OutNext			("  frustum:  %2d", tris_in_frame);
		F.OutNext			("    total:  %2d", m_pModel->get_tris_count());
	}
}
#endif
