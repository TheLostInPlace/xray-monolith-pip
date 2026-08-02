#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "StateManager/dx10ShaderResourceStateCache.h"

// Vars to store wind prev frame data ( Motion vectors )
static u32 prev_frame = -1;
static float prev_time = 0;
static Fvector4	prev_dir1 = { 0, 0, 0 }, prev_dir2 = { 0, 0, 0 };

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

static D3DVERTEXELEMENT9 dwDecl[] =
{
	{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos
	{0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // uv
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertHW
{
	float x, y, z;
	short u, v, t, mid;
};
#pragma pack(pop)

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

float GoToValue(float& current, float go_to)
{
	float diff = abs(current - go_to);

	float r_value = Device.fTimeDelta;

	if (diff - r_value <= 0)
	{
		current = go_to;
		return 0;
	}

	return current < go_to ? r_value : -r_value;
}

#ifdef USE_DX11
// reserved vs resource slot for the instance records, the grass vs itself only binds s_waves at t0
enum { DETAIL_INSTANCE_SRV_SLOT = 3 };

// f32 -> f16 round to nearest, inputs are tame normals and unit scalars
ICF u16 dm_f32tof16(float v)
{
	union { float f; u32 u; } c;
	c.f = v;
	u32 sign = (c.u >> 16) & 0x8000u;
	s32 exp = s32((c.u >> 23) & 0xFFu) - 127 + 15;
	u32 mant = c.u & 0x7FFFFFu;
	if (exp <= 0) return u16(sign);            // underflow -> signed zero
	if (exp >= 31) return u16(sign | 0x7BFFu); // overflow -> max finite half
	u32 h = sign | (u32(exp) << 10) | (mant >> 13);
	h += (mant >> 12) & 1u;                    // round up
	return u16(h);
}

#pragma pack(push, 1)
struct InstanceHW
{
	Fvector4 m0, m1, m2;
	u16 na[4]; // terrain normal xyz + alpha
	u16 sh[4]; // sun, hemi, spare, spare
};
#pragma pack(pop)
static_assert(sizeof(InstanceHW) == CDetailManager::hw_InstanceStride, "InstanceHW must match hw_InstanceStride");
#endif

void CDetailManager::hw_Load_Shaders()
{
	// Create shader to access constant storage
	ref_shader S;
	S.create("details\\set");
	R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
	R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
	hwc_consts = T0.get("consts");
	hwc_wave = T0.get("wave");
	hwc_wind = T0.get("dir2D");
	hwc_array = T0.get("array");
	hwc_s_consts = T1.get("consts");
	hwc_s_xform = T1.get("xform");
	hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render(light* L)
{
	PROF_EVENT("CDetailManager::hw_Render");
	// Render-prepare
	//	Update timer
	//	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
	float fDelta = Device.fTimeGlobal - m_global_time_old;
	if ((fDelta < 0) || (fDelta > 1)) fDelta = 0.03;
	m_global_time_old = Device.fTimeGlobal;

	m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
	m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
	m_time_pos += fDelta * swing_current.speed;

	//float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
	//float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
	float tm_rot1 = m_time_rot_1;
	float tm_rot2 = m_time_rot_2;

	Fvector4 dir1, dir2;
	dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
	dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

	// Setup geometry and DMA
	RCache.set_CullMode(CULL_NONE);
	RCache.set_xform_world(Fidentity);
	RCache.set_Geometry(hw_Geom);

#ifdef USE_DX11
	// fill once per frame, the later phases replay the same buffer through their own ranges
	if (hw_instancing && hw_frame_filled != Device.dwFrame)
	{
		Device.Statistic->RenderDUMP_DT_Count = 0; // accumulates across this frame's phases
		hw_Fill_Instances();
	}
#endif

	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;

	// Wave0
	consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
	//RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);				// consts
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir1);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0, L);

	// Wave1
	//wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir2);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0, L);

	// Still
	consts.set(scale, scale, scale, 1.f);
	//RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
	//RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
	//hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 0, 1, L);

	if (prev_frame != Device.dwFrame) 
	{
		prev_frame = Device.dwFrame;
		
		// Prev Frame swing time
		prev_time = m_time_pos;

		// Prev frame dir
		prev_dir1.set(dir1);
		prev_dir2.set(dir2);
	}

	RCache.set_CullMode(CULL_CCW);
}

#ifdef USE_DX11
void CDetailManager::hw_Fill_Instances()
{
	// pack every visible instance into the buffer with one map and record the per object ranges
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHK_DX(HW.pContext->Map(hw_instanceVB, 0, D3D_MAP_WRITE_DISCARD, 0, &mapped));
	InstanceHW* pInst = (InstanceHW*)mapped.pData;

	const u32 nObj = (u32)objects.size();
	u32 instTotal = 0;
	BOOL bOverflow = FALSE;
	for (u32 vid = 0; vid < 3; vid++)
	{
		vis_list& list = m_visibles[vid];
		for (u32 O = 0; O < nObj; O++)
		{
			hw_inst_base[vid * nObj + O] = instTotal;

			xr_vector<SlotItemVec*>& vis = list[O];
			xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
			xr_vector<SlotItemVec*>::iterator _vE = vis.end();
			for (; _vI != _vE; _vI++)
			{
				SlotItemVec* items = *_vI;
				SlotItemVecIt _iI = items->begin();
				SlotItemVecIt _iE = items->end();
				for (; _iI != _iE; _iI++)
				{
					SlotItem& Instance = **_iI;

					// alpha smoothing advances once per frame now
					Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

					if (Instance.alpha <= 0)
						break;

					if (instTotal >= hw_instance_cap)
					{
						bOverflow = TRUE;
						break;
					}

					// 3x4 transform rows with scale premultiplied in UpdateVisibleM plus packed shading
					Fmatrix& M = Instance.mRotY_calculated;
					InstanceHW& R = pInst[instTotal];
					R.m0.set(M._11, M._21, M._31, M._41);
					R.m1.set(M._12, M._22, M._32, M._42);
					R.m2.set(M._13, M._23, M._33, M._43);
					R.na[0] = dm_f32tof16(Instance.normal.x);
					R.na[1] = dm_f32tof16(Instance.normal.y);
					R.na[2] = dm_f32tof16(Instance.normal.z);
					R.na[3] = dm_f32tof16(Instance.alpha);
					R.sh[0] = dm_f32tof16(Instance.c_sun);
					R.sh[1] = dm_f32tof16(Instance.c_hemi);
					R.sh[2] = 0;
					R.sh[3] = 0;
					instTotal++;
				}
				if (instTotal >= hw_instance_cap)
					break;
			}
			hw_inst_count[vid * nObj + O] = instTotal - hw_inst_base[vid * nObj + O];

			// UpdateVisibleM rebuilds the lists on the next frame
			if (!vis.empty())
				vis.clear_not_free();
		}
	}

	HW.pContext->Unmap(hw_instanceVB, 0);

	if (bOverflow && !hw_overflow_logged)
	{
		hw_overflow_logged = true;
		Msg("! [DETAILS] instance buffer overflow, cap(%d) - some grass dropped", hw_instance_cap);
	}

	hw_frame_filled = Device.dwFrame;
}
#endif

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind,
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id, light* L)
{
	if (RImplementation.phase == CRender::PHASE_SMAP && var_id == 0)
		return;

	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strArray("array");
	static shared_str strXForm("xform");

	// Vanilla grass/trees wind
	static shared_str strWavePrev("wave_prev");
	static shared_str strDir2DPrev("dir2D_prev");

	// Grass Benders
	static shared_str strPrevPos("benders_prevpos");
	static shared_str strPos("benders_pos");
	static shared_str strGrassSetup("benders_setup");

	static shared_str strExData("exdata");
	static shared_str strGrassAlign("grass_align");

	// Grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	Fvector4 player_pos = { 0, 0, 0, 0 };
	int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

	// Add Player?
	if (ps_ssfx_grass_interactive.x > 0)
		player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);

#ifdef USE_DX11
	if (hw_instancing)
	{
		if (!RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector))
			return;

		if (RImplementation.phase == CRender::PHASE_SMAP && L)
		{
			if (!L->GMLight.is_sector_visible(RImplementation.pOutdoorSector))
				return;
		}

		// scale fade moved into the vs, x 1 fades on camera distance vs y, x 2 fades on light xz vs zw
		static shared_str strFadeParams("dt_fade_params");
		static shared_str strInstBase("dt_instance_base");

		const u32 nObj = (u32)objects.size();
		const u32 rbase = var_id * nObj;

		u32 total = 0;
		for (u32 obj = 0; obj < nObj; obj++)
			total += hw_inst_count[rbase + obj];
		if (total == 0)
			return;

		Fvector4 fade_params;
		if (fade_distance <= -1)
			fade_params.set(2.f, 0.f, light_position.x, light_position.z);
		else
			fade_params.set(1.f, fade_distance, 0.f, 0.f);

		u32 maxPasses = 1;
		for (u32 O = 0; O < nObj; O++)
		{
			if (!hw_inst_count[rbase + O])
				continue;
			ShaderElement* E = objects[O]->shader->E[lod_id]._get();
			if (E)
				maxPasses = _max(maxPasses, (u32)E->passes.size());
		}

		for (u32 iPass = 0; iPass < maxPasses; ++iPass)
		{
			// group consecutive objects that share an element so the binds do not repeat
			ShaderElement* curE = 0;
			u32 vOffset = 0;
			u32 iOffset = 0;
			for (u32 O = 0; O < nObj; O++)
			{
				CDetail& Object = *objects[O];
				u32 count = hw_inst_count[rbase + O];
				ShaderElement* E = Object.shader->E[lod_id]._get();
				if (count && E && iPass < E->passes.size())
				{
					if (E != curE)
					{
						curE = E;
						RCache.set_Element(E, iPass);
						// set_Element can disturb the vs resource slots, re-bind the record buffer
						SRVSManager.SetVSResource(DETAIL_INSTANCE_SRV_SLOT, hw_instanceSRV);
						RImplementation.apply_lmaterial();

						RCache.set_c(strConsts, consts);
						RCache.set_c(strWave, wave);
						RCache.set_c(strDir2D, wind);
						RCache.set_c(strXForm, Device.mFullTransform);
						RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);
						RCache.set_c(strWavePrev, prev_wave);
						RCache.set_c(strDir2DPrev, prev_wind);
						RCache.set_c(strFadeParams, fade_params);

						if (ps_ssfx_grass_interactive.y > 0)
						{
							RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

							Fvector4* c_grass;
							{
								void* GrassData;
								RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
								c_grass = (Fvector4*)GrassData;
							}
							if (c_grass)
							{
								c_grass[0].set(player_pos);
								c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

								for (int Bend = 1; Bend < BendersQty; Bend++)
								{
									c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
									c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
								}
							}

							Fvector4* c_prev_grass;
							{
								void* prev_GrassData;
								RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
								c_prev_grass = (Fvector4*)prev_GrassData;
							}
							if (c_prev_grass)
							{
								for (int Bend = 0; Bend < BendersQty; Bend++)
								{
									c_prev_grass[Bend].set(GData.prev_pos[Bend]);
									c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
								}
							}
						}
					}

					// SV_InstanceID restarts at zero every draw so the range base travels as a constant
					Fvector4 inst_base;
					inst_base.set(float(hw_inst_base[rbase + O]), 0.f, 0.f, 0.f);
					RCache.set_c(strInstBase, inst_base);

					RCache.RenderInstanced(D3DPT_TRIANGLELIST, count, vOffset, 0, Object.number_vertices,
					                       iOffset, Object.number_indices / 3);
					Device.Statistic->RenderDUMP_DT_Count += count;
					RCache.stat.r.s_details.add(count * Object.number_vertices);
				}
				vOffset += Object.number_vertices;
				iOffset += Object.number_indices;
			}
		}
		return;
	}
#endif

	Device.Statistic->RenderDUMP_DT_Count = 0;

	// Matrices and offsets
	u32 vOffset = 0;
	u32 iOffset = 0;

	vis_list& list = m_visibles[var_id];

	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	Fvector c_sun, c_ambient, c_hemi;
	c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
	c_sun.mul(.5f);
	c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
	c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

	// pip the main-pass drain keeps the visible set when the SVP pass draws the scope grass second
	extern bool g_svp_defer_detail_clear;

	// Iterate
	for (u32 O = 0; O < objects.size(); O++)
	{
		CDetail& Object = *objects[O];
		xr_vector<SlotItemVec*>& vis = list[O];
		if (!vis.empty())
		{
			for (u32 iPass = 0; iPass < Object.shader->E[lod_id]->passes.size(); ++iPass)
			{
				// Setup matrices + colors (and flush it as necessary)
				//RCache.set_Element				(Object.shader->E[lod_id]);
				RCache.set_Element(Object.shader->E[lod_id], iPass);
				RImplementation.apply_lmaterial();

				//	This could be cached in the corresponding consatant buffer
				//	as it is done for DX9
				RCache.set_c(strConsts, consts);
				RCache.set_c(strWave, wave);
				RCache.set_c(strDir2D, wind);
				RCache.set_c(strXForm, Device.mFullTransform);
				RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);

				RCache.set_c(strWavePrev, prev_wave);
				RCache.set_c(strDir2DPrev, prev_wind);

				if (ps_ssfx_grass_interactive.y > 0)
				{
					RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

					Fvector4* c_grass;
					{
						void* GrassData;
						RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
						c_grass = (Fvector4*)GrassData;
					}
					VERIFY(c_grass);

					if (c_grass)
					{
						c_grass[0].set(player_pos);
						c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

						for (int Bend = 1; Bend < BendersQty; Bend++)
						{
							c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
							c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
						}
					}

					Fvector4* c_prev_grass;
					{
						void* prev_GrassData;
						RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
						c_prev_grass = (Fvector4*)prev_GrassData;
					}
					VERIFY(c_prev_grass);

					if (c_prev_grass)
					{
						for (int Bend = 0; Bend < BendersQty; Bend++)
						{
							c_prev_grass[Bend].set(GData.prev_pos[Bend]);
							c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
						}
					}
				}

				Fvector4* c_ExData = 0;
				{
					void* pExtraData;
					RCache.get_ConstantDirect(strExData, hw_BatchSize * sizeof(Fvector4), &pExtraData, 0, 0);
					c_ExData = (Fvector4*)pExtraData;
				}
				VERIFY(c_ExData);

				//ref_constant constArray = RCache.get_c(strArray);
				//VERIFY(constArray);

				//u32			c_base				= x_array->vs.index;
				//Fvector4*	c_storage			= RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
				Fvector4* c_storage = 0;
				//	Map constants to memory directly
				{
					void* pVData;
					RCache.get_ConstantDirect(strArray,
					                          hw_BatchSize * sizeof(Fvector4) * 4,
					                          &pVData, 0, 0);
					c_storage = (Fvector4*)pVData;
				}
				VERIFY(c_storage);

				u32 dwBatch = 0;

				xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
				xr_vector<SlotItemVec*>::iterator _vE = vis.end();
				for (; _vI != _vE; _vI++)
				{
					SlotItemVec* items = *_vI;
					SlotItemVecIt _iI = items->begin();
					SlotItemVecIt _iE = items->end();
					for (; _iI != _iE; _iI++)
					{
						SlotItem& Instance = **_iI;

						if (!RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector))
							continue;

						if (RImplementation.phase == CRender::PHASE_SMAP && L)
						{
							if (!L->GMLight.is_sector_visible(RImplementation.pOutdoorSector))
								continue;

							if (L->position.distance_to_sqr(Instance.position) >= _sqr(L->range))
								continue;
						}

						u32 base = dwBatch * 4;

						Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

						float scale = 1.f;

						// Sort of fade using the scale
						// fade_distance == -1 use light_position to define "fade", anything else uses fade_distance
						if (fade_distance <= -1)
							scale *= 1.0f - Instance.position.distance_to_xz_sqr(light_position) * 0.005f;
						else if (Instance.distance > fade_distance)
							scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;

						if (scale <= 0 || Instance.alpha <= 0)
							break;

						// Build matrix ( 3x4 matrix, last row - color )
						Fmatrix& M = Instance.mRotY_calculated;
						c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
						c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
						c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
						//RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,	M._31*scale,	M._41	);
						//RCache.set_ca(&*constArray, base+1, M._12*scale,	M._22*scale,	M._32*scale,	M._42	);
						//RCache.set_ca(&*constArray, base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

						// Build color
						// R2 only needs hemisphere
						float h = Instance.c_hemi;
						float s = Instance.c_sun;
						c_storage[base + 3].set(s, s, s, h);

						if (c_ExData)
							c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);

						//RCache.set_ca(&*constArray, base+3, s,				s,				s,				h		);
						dwBatch ++;
						if (dwBatch == hw_BatchSize)
						{
							// flush
							Device.Statistic->RenderDUMP_DT_Count += dwBatch;
							u32 dwCNT_verts = dwBatch * Object.number_vertices;
							u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
							//RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
							//RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
							RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
							RCache.stat.r.s_details.add(dwCNT_verts);

							// restart
							dwBatch = 0;

							//	Remap constants to memory directly (just in case anything goes wrong)
							{
								void* pVData;
								RCache.get_ConstantDirect(strArray,
								                          hw_BatchSize * sizeof(Fvector4) * 4,
								                          &pVData, 0, 0);
								c_storage = (Fvector4*)pVData;
							}
							VERIFY(c_storage);
						}
					}
				}
				// flush if nessecary
				if (dwBatch)
				{
					Device.Statistic->RenderDUMP_DT_Count += dwBatch;
					u32 dwCNT_verts = dwBatch * Object.number_vertices;
					u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
					//RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
					//RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
					RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
					RCache.stat.r.s_details.add(dwCNT_verts);
				}
			}
			// Clean up
			// KD: we must not clear vis on r2 since we want details shadows
			// pip the deferred clear keeps the set alive for the SVP drain, its own pass clears after
			if (ps_ssfx_grass_shadows.x <= 0 && !g_svp_defer_detail_clear)
			{
				if (!psDeviceFlags2.test(rsGrassShadow) || RImplementation.PHASE_NORMAL == RImplementation.phase) // phase normal without shadows
					vis.clear_not_free();
			}
		}
		vOffset += hw_BatchSize * Object.number_vertices;
		iOffset += hw_BatchSize * Object.number_indices;
	}
}
