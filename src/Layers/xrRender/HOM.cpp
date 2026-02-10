// HOM.cpp: implementation of the CHOM class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "HOM.h"
#include "occRasterizer.h"
#include "../../xrEngine/GameFont.h"

#include "../../xrCore/profiler.h"

#include "dxRenderDeviceRender.h"

float psOSSR = .001f;

void __stdcall CHOM::MT_RENDER()
{
	PROF_EVENT("Render HOM");

	bool b_main_menu_is_active = (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
	if (MT_frame_rendered != Device.dwFrame && !b_main_menu_is_active)
	{
        Fmatrix mSaved = Device.mFullTransform;
		CFrustum ViewBase;
		ViewBase.CreateFromMatrix(mSaved, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
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
	S->close();
	FS.r_close(fs);

	if (ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))
	{
		// MT-details (@front)
		//Device.seqParallelRender.push_back(fastdelegate::FastDelegate0<>(Details, &CDetailManager::MT_CALC));

		// MT-HOM (@front)
		Device.seqParallelRender.push_back(xr_make_delegate(this, &CHOM::MT_RENDER));
	}
}

void CHOM::Unload()
{
	xr_delete(m_pModel);
	xr_free(m_pTris);
	bEnabled = FALSE;

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
    using namespace DirectX;

    // 1. Update projection matrices using DirectXMath
    float view_dim = occ_dim_0;

    // Viewport matrix: maps NDC to screen coords [0..view_dim]
    XMMATRIX m_viewport = XMMatrixSet(
        view_dim / 2.f, 0.0f, 0.0f, 0.0f,
        0.0f, -view_dim / 2.f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        view_dim / 2.f, view_dim / 2.f, 0.0f, 1.0f
    );

    XMMATRIX m_viewport_01 = XMMatrixSet(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    // Load Engine's FullTransform
    XMMATRIX m_full_xform = XMLoadFloat4x4((const XMFLOAT4X4*)&Device.mFullTransform);

    // Combine matrices
    XMMATRIX combined = XMMatrixMultiply(m_full_xform, m_viewport);
    XMMATRIX combined_01 = XMMatrixMultiply(m_full_xform, m_viewport_01);

    // Store back to Fmatrix (keeping compatibility with the rest of the engine)
    XMStoreFloat4x4((XMFLOAT4X4*)&m_xform, combined);
    XMStoreFloat4x4((XMFLOAT4X4*)&m_xform_01, combined_01);

    // 2. Query DB and Sort (standard logic)
    xrc.frustum_options(0);
    xrc.frustum_query(m_pModel, base);
    if (0 == xrc.r_count()) return;

    CDB::RESULT* it = xrc.r_begin();
    CDB::RESULT* end = xrc.r_end();

    Fvector COP = Device.vCameraPosition;
    end = std::remove_if(it, end, pred_fb(m_pTris));
    std::sort(it, end, pred_fb(m_pTris, COP));

    // 3. Clipping Setup
    CFrustum clip;
    clip.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_NEAR);
    sPoly src, dst;
    u32 _frame = Device.dwFrame;

    // 4. Rasterization Loop
    for (; it != end; it++)
    {
        occTri& T = m_pTris[it->id];
        u32 next = _frame + ::Random.randI(3, 10);

        // Plane classification (Front-face culling)
        if (!(T.flags || (T.plane.classify(COP) > 0)))
        {
            T.skip = next;
            continue;
        }

        CDB::TRI& t = m_pModel->get_tris()[it->id];
        Fvector* v = m_pModel->get_verts();

        src.clear();
        dst.clear();
        src.push_back(v[t.verts[0]]);
        src.push_back(v[t.verts[1]]);
        src.push_back(v[t.verts[2]]);

        sPoly* P = clip.ClipPoly(src, dst);
        if (!P || P->size() < 3)
        {
            T.skip = next;
            continue;
        }

        // 5. Transform and Rasterize using SIMD
        u32 pixels = 0;
        int limit = int(P->size()) - 1;

        // Pre-load the transform for this triangle's vertices
        for (int v_idx = 1; v_idx < limit; v_idx++)
        {
            // Transform 3 vertices of the triangle fan
            // Note: X-Ray uses Fvector4 p in its internal FVF/Transform logic usually, 
            // but here we map directly to occTri::raster
            auto project_to_raster = [&](int raster_idx, const Fvector& vert)
            {
                XMVECTOR vIn = XMVectorSet(vert.x, vert.y, vert.z, 1.0f);
                XMVECTOR vOut = XMVector4Transform(vIn, combined);

                // Perspective divide
                XMVECTOR w_inv = XMVectorReciprocal(XMVectorSplatW(vOut));
                XMVECTOR projected = XMVectorMultiply(vOut, w_inv);

                XMStoreFloat3((XMFLOAT3*)&T.raster[raster_idx], projected);
            };

            project_to_raster(0, (*P)[0]);
            project_to_raster(1, (*P)[v_idx]);
            project_to_raster(2, (*P)[v_idx + 1]);

            pixels += Raster.rasterize(&T);
        }

        if (0 == pixels)
        {
            T.skip = next;
            continue;
        }
    }
}

void CHOM::Render(CFrustum& base)
{
	if (!bEnabled) return;

	Device.Statistic->RenderCALC_HOM.Begin();
	Raster.clear();
	Render_DB(base);
	Raster.propagade();
	MT_frame_rendered = Device.dwFrame;
	Device.Statistic->RenderCALC_HOM.End();
}

IC BOOL _visible(Fbox& B, Fmatrix& m_xform_01)
{
    using namespace DirectX;

    // 1. Load matrix once
    XMMATRIX M = XMLoadFloat4x4((const XMFLOAT4X4*)&m_xform_01);

    // 2. Setup initial Min/Max registers with extreme values
    XMVECTOR vMin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
    XMVECTOR vMax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    // 3. Define the 8 corners
    XMVECTOR corners[8];
    corners[0] = XMVectorSet(B.min.x, B.min.y, B.min.z, 1.0f);
    corners[1] = XMVectorSet(B.min.x, B.min.y, B.max.z, 1.0f);
    corners[2] = XMVectorSet(B.max.x, B.min.y, B.max.z, 1.0f);
    corners[3] = XMVectorSet(B.max.x, B.min.y, B.min.z, 1.0f);
    corners[4] = XMVectorSet(B.min.x, B.max.y, B.min.z, 1.0f);
    corners[5] = XMVectorSet(B.min.x, B.max.y, B.max.z, 1.0f);
    corners[6] = XMVectorSet(B.max.x, B.max.y, B.max.z, 1.0f);
    corners[7] = XMVectorSet(B.max.x, B.max.y, B.min.z, 1.0f);

    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR p = XMVector4Transform(corners[i], M);

        // Early exit: if any point's Z is behind near plane, consider visible
        if (XMVectorGetZ(p) < EPS) return TRUE;

        // 4. Perspective Divide using SIMD
        // Splat W to all components and multiply by reciprocal
        XMVECTOR w_inv = XMVectorReciprocal(XMVectorSplatW(p));
        XMVECTOR projected = XMVectorMultiply(p, w_inv);

        // 5. Update Min/Max registers (Branchless!)
        vMin = XMVectorMin(vMin, projected);
        vMax = XMVectorMax(vMax, projected);
    }

    // 6. Final extraction for the Rasterizer test
    XMFLOAT4 fMin, fMax;
    XMStoreFloat4(&fMin, vMin);
    XMStoreFloat4(&fMax, vMax);

    return Raster.test(fMin.x, fMin.y, fMax.x, fMax.y, fMin.z);
}

BOOL CHOM::visible(Fbox3& B)
{
	if (!bEnabled) return TRUE;
	if (B.contains(Device.vCameraPosition)) return TRUE;
	return _visible(B, m_xform_01);
}

BOOL CHOM::visible(Fbox2& B, float depth)
{
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
	BOOL result = _visible(vis.box, m_xform_01);
	u32 delay = 1;
	if (result)
	{
		// visible	- delay next test
		delay = ::Random.randI(5 * 2, 5 * 5);
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
    if (P.empty()) return FALSE; // Safety check

    using namespace DirectX;

    // 1. Load the projection matrix once
    XMMATRIX M = XMLoadFloat4x4((const XMFLOAT4X4*)&m_xform_01);

    // 2. Initialize Min/Max registers
    XMVECTOR vMin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
    XMVECTOR vMax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    // 3. Iterate through all vertices in the polygon
    for (const auto& vert : P)
    {
        // Load vertex into SIMD register
        XMVECTOR vIn = XMVectorSet(vert.x, vert.y, vert.z, 1.0f);
        
        // Transform
        XMVECTOR vOut = XMVector4Transform(vIn, M);

        // Near-plane clipping check (Z check)
        // Extract Z via Splat for a quick comparison
        if (XMVectorGetZ(vOut) < EPS) return TRUE;

        // 4. Perspective Divide (W-divide)
        XMVECTOR w_inv = XMVectorReciprocal(XMVectorSplatW(vOut));
        XMVECTOR projected = XMVectorMultiply(vOut, w_inv);

        // 5. Branchless Min/Max update
        vMin = XMVectorMin(vMin, projected);
        vMax = XMVectorMax(vMax, projected);
    }

    // 6. Extract results for the Rasterizer test
    XMFLOAT4 fMin, fMax;
    XMStoreFloat4(&fMin, vMin);
    XMStoreFloat4(&fMax, vMax);

    // Note: fMin.z represents the "minz" (closest depth) of the polygon
    return Raster.test(fMin.x, fMin.y, fMax.x, fMax.y, fMin.z);
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
		F.OutNext			("  visible:  %2d", tris_in_frame_visible);
		F.OutNext			("  frustum:  %2d", tris_in_frame);
		F.OutNext			("    total:  %2d", m_pModel->get_tris_count());
	}
}
#endif
