#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

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
	if (!UseHW()) return;
	PROF_EVENT("CDetailManager::hw_Render");

	RCache.set_CullMode		(CULL_NONE);
	RCache.set_xform_world	(Fidentity);

	// Setup geometry and DMA
	RCache.set_Geometry(hw_Geom);

	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;

	// Wave0
	{
		PROF_EVENT("Wave0");
		consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
		wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
		prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
		hw_Render_dump(consts, wave.div(PI_MUL_2), wave_dir1, prev_wave.div(PI_MUL_2), wave_dir1_old, 1, 0, L);
	}

	// Wave1
	{
		PROF_EVENT("Wave1");
		wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
		prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
		hw_Render_dump(consts, wave.div(PI_MUL_2), wave_dir2, prev_wave.div(PI_MUL_2), wave_dir2_old, 2, 0, L);
	}
	
	// Still
	{
		PROF_EVENT("Still");
		consts.set(scale, scale, scale, 1.f);
		hw_Render_dump(consts, wave.div(PI_MUL_2), wave_dir2, prev_wave.div(PI_MUL_2), wave_dir2_old, 0, 1, L);
	}

	RCache.set_CullMode(CULL_CCW);
}

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind, 
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id, light* L)
{
	if (RImplementation.phase == CRender::PHASE_SMAP && var_id == 0)
		return;

	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strArray("array");

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


	// Matrices and offsets
	u32 vOffset	= 0;
	u32 iOffset	= 0;

	// Iterate
	for (CDetail& Object : objects)
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

			u32 dwBatch = 0;
			for (auto& S : Object.m_items[var_id][render_key])
			{
				CDetail::SlotItem& Instance = *S.get();

				if (RImplementation.pOutdoorSector && PortalTraverser.i_marker != RImplementation.pOutdoorSector->r_marker)
					continue;

				if (RImplementation.phase == CRender::PHASE_SMAP && L)
				{
					if (L->position.distance_to_sqr(Instance.mRotY.c) >= _sqr(L->range))
						continue;
				}

				static Fmatrix* c_storage = NULL;
				if (dwBatch == 0)
					RCache.get_ConstantDirect(strArray, hw_BatchSize*sizeof(Fmatrix), (void**)&c_storage, 0, 0);


				if (!c_storage) continue;

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
				c_storage[dwBatch] = {M._11 * scale, M._21 * scale, M._31 * scale, M._41,
									  M._12 * scale, M._22 * scale, M._32 * scale, M._42,
									  M._13 * scale, M._23 * scale, M._33 * scale, M._43,
									  1.f, 1.f, 1.f, Instance.c_hemi};

				if (c_ExData)
					c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);

				dwBatch++;

				if (dwBatch >= hw_BatchSize)
				{
					// flush
					u32 dwCNT_verts = dwBatch * Object.number_vertices;
					u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
					RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);

					// restart
					dwBatch = 0;
				}
			}
			// flush if nessecary
			if (dwBatch > 0 && dwBatch < hw_BatchSize)
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
		if (ps_ssfx_grass_shadows.x <= 0)
		{
			if (!psDeviceFlags2.test(rsGrassShadow) || ((ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_SMAP ==
				RImplementation.phase)) // phase smap with shadows
				|| (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_NORMAL == RImplementation.phase)
					&& (!RImplementation.is_sun())) // phase normal with shadows without sun
				|| (!ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_NORMAL == RImplementation.phase))
				)) // phase normal without shadows
				// replace with working code
			{
				//vis.clear_not_free();
			}
		}
		vOffset += hw_BatchSize * Object.number_vertices;
		iOffset += hw_BatchSize * Object.number_indices;
	}
}
