#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"

void CRender::render_main(bool deffered, bool zfill)
{
	PIX_EVENT(render_main);
	//	Msg						("---begin");
	marker ++;
	bool dont_test_sectors = Sectors.size() <= 1;
	// Calculate sector(s) and their objects
	if (pLastSector)
	{
		//!!!
		//!!! BECAUSE OF PARALLEL HOM RENDERING TRY TO DELAY ACCESS TO HOM AS MUCH AS POSSIBLE
		//!!!
		if(deffered)
		{
			// Traverse object database
			g_SpatialSpace->q_frustum(
				lstRenderablesMain,
				ISpatial_DB::O_ORDERED,
				STYPE_RENDERABLE + STYPE_RENDERABLESHADOW + STYPE_PARTICLE + STYPE_LIGHTSOURCE,
				ViewBase);//nearest sorting

			// Determine visibility for dynamic part of scene
			set_Object(0);
			u32 uID_LTRACK = 0xffffffff;
			if (phase==CRender::PHASE_NORMAL)
			{
				// update light-vis for current entity / actor
				if (CObject* O = g_pGameLevel->CurrentViewEntity())
				{
					if(!O->getDestroy())
					{
						if (CROS_impl* R = (CROS_impl*)O->ROS())
							R->update(O);
					}
				}

				if (lstRenderablesMain.size())
				{
					uLastLTRACK++;
					uID_LTRACK = uLastLTRACK%lstRenderablesMain.size();

					// update light-vis for selected entity
					// track lighting environment
					if (IRenderable* renderable = (IRenderable*)lstRenderablesMain[uID_LTRACK]->dcast_Renderable())
					{
						if (CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
							T->update(renderable);
					}
				}
			}
		}
		Fmatrix mftrans;
		if(zfill)
		{
			Fmatrix m_project;
			m_project.build_projection(
				deg2rad(Device.fFOV/* *Device.fASPECT*/),
				Device.fASPECT, VIEWPORT_NEAR,
				ps_r2_zfill * g_pGamePersistent->Environment().CurrentEnv->far_plane);
			mftrans.mul(m_project,Device.mView);
		}
		else
			mftrans = Device.mFullTransform;
		// Traverse sector/portal structure
		if (!dont_test_sectors)
		{
			PortalTraverser.traverse(
				pLastSector,
				ViewBase,
				Device.vCameraPosition,
				mftrans,
				CPortalTraverser::VQ_HOM + CPortalTraverser::VQ_SSA + CPortalTraverser::VQ_FADE
				//. disabled scissoring (HW.Caps.bScissor?CPortalTraverser::VQ_SCISSOR:0)	// generate scissoring info
			);
		}
		// Determine visibility for static geometry hierrarhy
		if(psDeviceFlags.test(rsDrawStatic))
		{
			if (dont_test_sectors)
			{
				CSector* sector = (CSector*)Sectors[0];
				set_Frustum(&ViewBase);
				add_Geometry(sector->root());
			}
			else
			{
				for (u32 s_it=0; s_it<PortalTraverser.r_sectors.size(); s_it++)
				{
					CSector* sector = (CSector*)PortalTraverser.r_sectors[s_it];
					dxRender_Visual* root = sector->root();
					for (u32 v_it=0; v_it<sector->r_frustums.size(); v_it++) {
						set_Frustum(&(sector->r_frustums[v_it]));
						add_Geometry(root);
					}
				}
			}
		}

		// Traverse frustums
		for (u32 o_it=0; o_it<lstRenderablesMain.size(); o_it++)
		{
			ISpatial* spatial = lstRenderablesMain[o_it].get();
			if (0==spatial) continue; spatial->spatial_updatesector();
			CSector* sector = (CSector*)spatial->spatial.sector;
			if (0==sector) continue;

			if ((spatial->spatial.type & STYPE_LIGHTSOURCE) && deffered)
			{
				// hud lightsource
				if(light* L = (light*)(spatial->dcast_Light()))
				{
					if(L->flags.bHudMode)
					{
						Lights.add_light(L);
						continue;
					}
				}
			}

			if(!HOM.visible(spatial->spatial.sphere)) continue;

			if ((spatial->spatial.type & STYPE_LIGHTSOURCE) && deffered)
			{
				// lightsource
				if (light* L = (light*)(spatial->dcast_Light()))
				{
					if (L->get_LOD()>EPS_L && !L->flags.bHudMode)
					{
						if (dont_test_sectors)
							Lights.add_light(L);
						else
						{
							for (u32 s_it = 0; s_it < L->m_sectors.size(); s_it++)
							{
								CSector* sector_ = (CSector*)L->m_sectors[s_it];
								if (PortalTraverser.i_marker == sector_->r_marker)
								{
									Lights.add_light(L);
									break;
								}
							}
						}
					}
				}
				continue;
			}
			if (dont_test_sectors)
			{
				if (spatial->spatial.type & STYPE_RENDERABLE && psDeviceFlags.test(rsDrawDynamic))
				{
					// renderable
					if (IRenderable* renderable = spatial->dcast_Renderable())
					{
						if (Device.vCameraPosition.distance_to(spatial->spatial.sphere.P) < (g_pGamePersistent->Environment().CurrentEnv->fog_distance))
						{
							if (Device.CalcSSADynamic(spatial->spatial.sphere.P, spatial->spatial.sphere.R) > 0.002f && Device.GetPerceivedDist(spatial->spatial.sphere.P) < 350.f)
							{
								if (deffered)
								{
									CKinematics* pKin = (CKinematics*)renderable->renderable.visual;
									if (pKin)
									{
										pKin->CalculateBones(TRUE);
										pKin->CalculateWallmarks();
									}
								}
								if (spatial->spatial.sphere.R > 1.f)
								{
									// Rendering
									set_Object(renderable);
									renderable->renderable_Render();
									set_Object(0);
								}
							}
							if (spatial->spatial.sphere.R <= 1.f)
							{
								// Rendering
								set_Object(renderable);
								renderable->renderable_Render();
								set_Object(0);
							}
						}
					}
				}
				if (spatial->spatial.type & STYPE_PARTICLE && !deffered)
				{
					// renderable
					if (IRenderable* renderable = spatial->dcast_Renderable())
					{
						// Rendering
						set_Object(renderable);
						renderable->renderable_Render();
						set_Object(0);
					}
				}
			}
			else
			{
				if (PortalTraverser.i_marker != sector->r_marker) continue; // inactive (untouched) sector
				for (u32 v_it=0; v_it<sector->r_frustums.size(); v_it++)
				{
					CFrustum& view = sector->r_frustums[v_it];
					if (!view.testSphere_dirty(spatial->spatial.sphere.P,spatial->spatial.sphere.R)) continue;
					if (spatial->spatial.type & STYPE_RENDERABLE && psDeviceFlags.test(rsDrawDynamic))
					{
						// renderable
						if (IRenderable* renderable = spatial->dcast_Renderable())
						{
							if (Device.vCameraPosition.distance_to(spatial->spatial.sphere.P) < (g_pGamePersistent->Environment().CurrentEnv->fog_distance))
							{
								if (Device.CalcSSADynamic(spatial->spatial.sphere.P, spatial->spatial.sphere.R) > 0.002f && Device.GetPerceivedDist(spatial->spatial.sphere.P) < 350.f)
								{
									if (deffered)
									{
										CKinematics* pKin = (CKinematics*)renderable->renderable.visual;
										if (pKin)
										{
											pKin->CalculateBones(TRUE);
											pKin->CalculateWallmarks();
										}
									}
									if (spatial->spatial.sphere.R > 1.f)
									{
										// Rendering
										set_Object(renderable);
										renderable->renderable_Render();
										set_Object(0);
									}
								}
								if (spatial->spatial.sphere.R <= 1.f)
								{
									// Rendering
									set_Object(renderable);
									renderable->renderable_Render();
									set_Object(0);
								}
							}
						}
					}
					if (spatial->spatial.type & STYPE_PARTICLE && !deffered)
					{
						// renderable
						if (IRenderable* renderable = spatial->dcast_Renderable())
						{
							// Rendering
							set_Object(renderable);
							renderable->renderable_Render();
							set_Object(0);
						}
					}
				}
			}
		}
		if (g_pGameLevel && (phase == PHASE_NORMAL))
		{
			PROF_EVENT("Render HUD");
			g_hud->Render_Last();
			if (g_hud->RenderActiveItemUIQuery())
				r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				r_dsgraph_render_cam_ui();
		}
	}
	else
	{
		set_Object(0);
		if (g_pGameLevel && (phase == PHASE_NORMAL))
		{
			PROF_EVENT("Render HUD");
			g_hud->Render_Last();
			if (g_hud->RenderActiveItemUIQuery())
				r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				r_dsgraph_render_cam_ui();
		}
	}
}

void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Main Render
	{
		Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		HW.pDevice->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	u32 C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::Render()
{
	PIX_EVENT(CRender_Render);

	VERIFY(0==mapDistort.size());

	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		m_bFirstFrameAfterReset = false;
		return;
	}

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	View = 0;
	if (!ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))
	{
		HOM.Enable();
		HOM.Render(ViewBase);
	}

	//******* Z-prefill calc - DEFERRER RENDERER
	if (ps_r2_ls_flags.test(R2FLAG_ZFILL))
	{
		PIX_EVENT(DEFER_Z_FILL);
		Device.Statistic->RenderCALC.Begin();
		float z_distance = ps_r2_zfill;
		Fmatrix m_zfill, m_project;
		m_project.build_projection(
			deg2rad(Device.fFOV/* *Device.fASPECT*/),
			Device.fASPECT, VIEWPORT_NEAR,
			z_distance * g_pGamePersistent->Environment().CurrentEnv->far_plane);
		m_zfill.mul(m_project, Device.mView);
		r_pmask(true, false); // enable priority "0"
		phase = PHASE_SMAP;
		render_main(false, true);
		r_pmask(true, false); // disable priority "1"
		Device.Statistic->RenderCALC.End();

		// flush
		Target->phase_scene_prepare();
		RCache.set_ColorWriteEnable(FALSE);
		r_dsgraph_render_graph(0);
		RCache.set_ColorWriteEnable();
	}
	else
	{
		Target->phase_scene_prepare();
	}


	//******* Main calc - DEFERRER RENDERER
	// Main calc
	Device.Statistic->RenderCALC.Begin();
	r_pmask(true, false, true); // enable priority "0",+ capture wmarks
	phase = PHASE_NORMAL;
	{
		PROF_EVENT("lights_spatial_move");
		for (light* L : v_all_lights)
			L->spatial_move();
	}
	render_main(true);
	r_pmask(true, false); // disable priority "1"
	Device.Statistic->RenderCALC.End();

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
	}

	BOOL split_the_scene_to_minimize_wait = FALSE;
	if (ps_r2_ls_flags.test(R2FLAG_EXP_SPLIT_SCENE)) split_the_scene_to_minimize_wait = TRUE;

	//******* Main render :: PART-0	-- first
	if (!split_the_scene_to_minimize_wait)
	{
		PIX_EVENT(DEFER_PART0_NO_SPLIT);
		// level, DO NOT SPLIT
		Target->phase_scene_begin();
		r_dsgraph_render_hud();
		r_dsgraph_render_graph(0);
		r_dsgraph_render_lods(true, true);
		if (Details) Details->Render();
		Target->phase_scene_end();
	}
	else
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();
		r_dsgraph_render_graph(0);
		Target->disable_aniso();
	}

	//******* Occlusion testing of volume-limited light-sources
	Target->phase_occq();
	LP_normal.clear();
	LP_pending.clear();
	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);
	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		// perform tests
		u32 count = 0;
		light_Package& LP = Lights.package;

		// stats
		stats.l_shadowed = LP.v_shadowed.size();
		stats.l_unshadowed = LP.v_point.size() + LP.v_spot.size();
		stats.l_total = stats.l_shadowed + stats.l_unshadowed;

		// perform tests
		count = _max(count, LP.v_point.size());
		count = _max(count, LP.v_spot.size());
		count = _max(count, LP.v_shadowed.size());
		for (u32 it = 0; it < count; it++)
		{
			if (it < LP.v_point.size())
			{
				light* L = LP.v_point[it];
				if(L->flags.bOccq&&!L->flags.bHudMode)
				{
					L->vis_prepare		();
					if (L->vis.pending)	LP_pending.v_point.push_back	(L);
					else				LP_normal.v_point.push_back		(L);
				}
				else
					LP_normal.v_point.push_back		(L);
			}
			if (it < LP.v_spot.size())
			{
				light* L = LP.v_spot[it];
				if(L->flags.bOccq&&!L->flags.bHudMode)
				{
					L->vis_prepare		();
					if (L->vis.pending)	LP_pending.v_spot.push_back		(L);
					else				LP_normal.v_spot.push_back		(L);
				}
				else
					LP_normal.v_spot.push_back		(L);
			}
			if (it < LP.v_shadowed.size())
			{
				light* L = LP.v_shadowed[it];
				if(L->flags.bOccq&&!L->flags.bHudMode)
				{
					L->vis_prepare		();
					if (L->vis.pending)	LP_pending.v_shadowed.push_back	(L);
					else				LP_normal.v_shadowed.push_back	(L);
				}
				else
					LP_normal.v_shadowed.push_back	(L);
			}
		}
	}
	LP_normal.sort();
	LP_pending.sort();

	//******* Main render :: PART-1 (second)
	if (split_the_scene_to_minimize_wait)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// skybox can be drawn here
		
		if (0)
		{
			if (!RImplementation.o.dx10_msaa)
				Target->u_setrt(Target->rt_Generic_0, Target->rt_Generic_1, 0, HW.pBaseZB);
			else
				Target->u_setrt(Target->rt_Generic_0_r, Target->rt_Generic_1, 0,
				                RImplementation.Target->rt_MSAADepth->pZRT);
			RCache.set_CullMode(CULL_NONE);
			RCache.set_Stencil(FALSE);

			// draw skybox
			RCache.set_ColorWriteEnable();
			//CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	FALSE				));
			RCache.set_Z(FALSE);
			g_pGamePersistent->Environment().RenderSky();
			//CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	TRUE				));
			RCache.set_Z(TRUE);
		}

		// level
		Target->phase_scene_begin();
		r_dsgraph_render_hud();
		r_dsgraph_render_lods(true, true);
		if (Details) Details->Render();
		Target->phase_scene_end();
	}

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// Update incremental shadowmap-visibility solver
	{
		PIX_EVENT(DEFER_FLUSH_OCCLUSION);
		u32 it = 0;
		for (it = 0; it < Lights_LastFrame.size(); it++)
		{
			if (0 == Lights_LastFrame[it]) continue ;
			try
			{
				Lights_LastFrame[it]->svis.flushoccq();
			}
			catch (...)
			{
				Msg("! Failed to flush-OCCq on light [%d] %X", it, *(u32*)(&Lights_LastFrame[it]));
			}
		}
		Lights_LastFrame.clear();
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}

	// Directional light - fucking sun
	if (bSUN)
	{
		PIX_EVENT(DEFER_SUN);
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
		Target->increment_light_marker();
		Target->accum_direct_blend();
	}

	{
		PIX_EVENT(DEFER_SELF_ILLUM);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos - 
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		RImplementation.r_dsgraph_render_emissive();
	}

	// Lighting, non dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
		Target->phase_accumulator();
		render_lights(LP_normal);
	}

	// Lighting, dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_OCCQ);
		render_lights(LP_pending);
	}

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		Target->phase_combine();
	}

	VERIFY(0==mapDistort.size());
}

void CRender::render_forward()
{
	VERIFY(0==mapDistort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	{
		// level
		r_pmask(false, true); // enable priority "1"
		phase = PHASE_NORMAL;
		render_main(false); //
		//	Igor: we don't want to render old lods on next frame.
		mapLOD.clear();
		CParticlesAsync::Wait();
		r_dsgraph_render_graph(1); // normal level, secondary priority
		PortalTraverser.fade_render(); // faded-portals
		r_dsgraph_render_sorted(); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3D10Texture2D* pBuffer = nullptr;
	HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (LPVOID*)&pBuffer);
	HW.pDevice->CopyResource((*RT)->pSurface, pBuffer);
	pBuffer->Release();
}

