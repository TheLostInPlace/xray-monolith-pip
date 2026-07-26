#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

// pip object-space render (skinning) matrix of the lens bone, the SVP eyepiece uses it so a skinned
// scope lens follows its bone through ADS and sway instead of the kinematics root
bool CSkeletonX::SVP_LensBoneXform(Fmatrix& out)
{
	if (!Parent)
		return false;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	out = Parent->LL_GetBoneInstance(bone).mRenderTransform;
	return true;
}

// pip lens bone visibility, hidden markswitch lens meshes must not win the eyepiece pick
bool CSkeletonX::SVP_LensBoneVisible()
{
	if (!Parent)
		return true;
	BOOL visible;
	if (SVP_BoneSnapshotVisible(visible))
		return !!visible;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return true;
	return !!Parent->LL_GetBoneVisible(bone);
}

// pip forward the owning kinematics measured lens fit, lets the render side query detection
// from a lens leaf visual, false when unparented
bool CSkeletonX::SVP_GetLensDetection(SLensDetection& out)
{
	if (!Parent)
		return false;
	return Parent->GetLensDetection(out);
}

// pip capture one HUD root sample for both viewports and every late lens draw
void CDSGraphManager::svp_latch_hud_poses()
{
	m_svp_pose.clear();
	m_svp_pose_frame = Device.dwFrame;
	auto grab = [this](Fmatrix* p)
	{
		if (!p)
			return;
		for (const auto& pose : m_svp_pose)
			if (pose.source == p)
				return;
		m_svp_pose.emplace_back();
		m_svp_pose.back().source = p;
		m_svp_pose.back().value = *p;
	};
	for (auto& n : RGraph.mapHUD)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Sorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Wmark)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Emissive)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Distort)
		grab(n.pMatrix);
#if defined(USE_DX11)
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab(n.pMatrix);

	extern int scope_svp_enabled;
	extern int ps_r__svp_weapon_continuity;
	const bool exact_pose = scope_svp_enabled >= 2 && ps_r__svp_weapon_continuity
		&& Device.true_pip_on;
	if (exact_pose)
	{
		svp_hud_bone_snapshot_begin();
		auto capture = [](dxRender_Visual* v)
		{
			CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
			if (skeleton)
				skeleton->SVP_CaptureBoneSnapshot();
		};
		for (auto& n : RGraph.mapHUD)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Sorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Wmark)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Emissive)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Distort)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDSorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDObjective)
			capture(n.pVisual);
		for (auto& n : RGraph.mapReflexHUDSorted)
			capture(n.pVisual);
	}

	m_svp_bone.clear();
	m_svp_bone_frame = Device.dwFrame;
	auto grab_bone = [this](dxRender_Visual* v)
	{
		if (!v)
			return;
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
				return;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(v);
		Fmatrix b;
		if (sk && sk->SVP_LensBoneXform(b))
		{
			m_svp_bone.emplace_back();
			m_svp_bone.back().visual = v;
			m_svp_bone.back().value = b;
		}
	};
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab_bone(n.pVisual);
#endif
}

// the latched pose while this frame's latch is live, else the caller's pointer unchanged
Fmatrix* CDSGraphManager::svp_pose_of(Fmatrix* p)
{
	if (m_svp_pose_frame == Device.dwFrame)
		for (auto& pose : m_svp_pose)
			if (pose.source == p)
				return &pose.value;
	return p;
}

// the latched lens bone while this frame's latch is live, false falls back to the live read
bool CDSGraphManager::svp_lens_bone_of(dxRender_Visual* v, Fmatrix& out)
{
	CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
	if (skeleton && skeleton->SVP_BoneSnapshotXform(out))
		return true;
	if (m_svp_bone_frame == Device.dwFrame)
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
			{
				out = bone.value;
				return true;
			}
	return false;
}

static LPCSTR svp_hud_role_name(u8 role)
{
	switch (role)
	{
	case IDSGraphManager::hud_hands: return "hands";
	case IDSGraphManager::hud_primary_item: return "primary";
	case IDSGraphManager::hud_offhand_item: return "offhand";
	case IDSGraphManager::hud_optic: return "optic";
	case IDSGraphManager::hud_prop: return "prop";
	default: return "unknown";
	}
}

static bool svp_axial_bounds(const Fbox& box, const Fmatrix& world,
	const Fvector& origin, const Fvector& axis, float& lower, float& upper)
{
	lower = 1e9f;
	upper = -1e9f;
	for (u32 corner = 0; corner < 8; ++corner)
	{
		Fvector local;
		local.set((corner & 1) ? box.max.x : box.min.x,
			(corner & 2) ? box.max.y : box.min.y,
			(corner & 4) ? box.max.z : box.min.z);
		Fvector point;
		world.transform_tiny(point, local);
		Fvector offset;
		offset.sub(point, origin);
		const float axial = offset.dotproduct(axis);
		if (!_valid(axial))
			return false;
		lower = _min(lower, axial);
		upper = _max(upper, axial);
	}
	return lower <= upper;
}

// pip drain only the weapon list into the scope image, the caller owns the view/projection
int g_svp_hud_skip_scope = 0; // hud_full mode: 1 drops the scope body, 2 also drops pieces fully behind the front lens
float g_svp_legacy_front_m = 0.f;
void CDSGraphManager::r_dsgraph_render_hud_svp()
{
	PROF_EVENT("r_dsgraph_render_hud_svp");
	// mode 1 keeps the heuristic body skip
	// the mode 2 objective admission plane replaces it
	extern int scope_svp_enabled;
	const bool legacy_mode = scope_svp_enabled == 1;
	{
		extern int ps_r__svp_hud_full;
		g_svp_hud_skip_scope = legacy_mode ? ps_r__svp_hud_full : 0;
	}
	if (!legacy_mode)
		g_svp_legacy_front_m = 0.f;
	// same camera and projection as the world, real depth so the scene occludes the weapon
	// sliver and vice versa (rmNear depth-fronting belonged to the retired separate camera)
	RImplementation.rmNormal();
	auto& graph = RGraph.mapHUD;
	if (!graph.empty())
	{
		std::sort(graph.begin(), graph.end());
		// the scope body hugs the eyepiece-objective axis, everything else on the weapon (barrel,
		// sights, hands) sits off-axis. the size cap keeps a merged whole-gun mesh from matching
		const auto& vp = Device.m_SecondViewport;
		const Fvector A = vp.eyepiece.m_W.c;
		Fvector ax; ax.sub(vp.objective.m_W.c, A);
		const float len2 = ax.square_magnitude();
		const float tube = _sqrt(len2);
		Fvector axis = ax;
		if (tube > EPS)
			axis.div(tube);
		const float rcyl = std::max(vp.eyepiece.radius, vp.objective.radius) * 1.75f + 0.01f;
		extern int ps_r__svp_optic_body_suppress;
		const bool objective_filter = !legacy_mode && ps_r__svp_optic_body_suppress
			&& scope_svp_enabled >= 2 && Device.true_pip_on && vp.IsSVPActive()
			&& vp.svp_camera_domain == CSecondVPParams::camera_objective
			&& vp.SnapshotExact(vp.svp_camera_frame, vp.svp_camera_session, Device.dwFrame)
			&& vp.objective.radius > EPS;
		const bool measure_ok = (legacy_mode || objective_filter)
			&& len2 > EPS && vp.eyepiece.radius > EPS;
		const bool skip_ok = legacy_mode ? (g_svp_hud_skip_scope && measure_ok) : objective_filter;
		float front = 0.f;
		float clipon_front = 0.f;
		u32 admit_candidates = 0;
		u32 admit_suppressed = 0;
		u32 admit_clipon_unresolved = 0;
		// the clip-on window widens to the authored front plane when the per-scope data reaches
		// past the fixed bound, widening only so no rig loses a classification it had
		float clip_hi = 2.6f;
		{
			extern int ps_r__svp_drain_anchor;
			const float auth_z = vp.svp_opt_offset.z; // bus resolved, authored_optics folded in
			if (ps_r__svp_drain_anchor && auth_z > EPS && tube > EPS)
			{
				const float t_front = auth_z * vp.eyepiece.radius / tube;
				if (t_front > clip_hi)
					clip_hi = t_front;
			}
		}
		extern int ps_r__svp_cop_diag;
		static u32 s_hud_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_hud_diag_ms > 3000);
		if (diag)
		{
			s_hud_diag_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-HUD] tube=%.1fcm rcyl=%.1fcm cap=%.1fcm items=%u skip_ok=%d front=%.1fcm objectiveFilter=%d",
				tube * 100.f, rcyl * 100.f, tube * 1.75f * 100.f, (u32)graph.size(), (int)skip_ok,
				g_svp_legacy_front_m * 100.f, objective_filter ? 1 : 0);
		}
		extern int ps_r__svp_stats; extern u32 svp_stats_hud_cull_reject; extern u32 svp_ledger_hud_cull_reject;
		for (auto& item : graph)
		{
			if (svp_cull_reject(item.pVisual, svp_pose_of(item.pMatrix))) { if (ps_r__svp_stats) ++svp_stats_hud_cull_reject; svp_ledger_hud_cull_reject = 1; continue; } // pip skip off-cone SVP geometry
			dxRender_Visual* V = item.pVisual;
			VERIFY(V && V->shader._get());
			bool drop = false;
			bool clip_obj = false;
			float t = 0.f, rad = -1.f;
			float axial_lo = 0.f, axial_hi = 0.f;
			LPCSTR admission = "outside";
			if (measure_ok && item.pMatrix)
			{
				// skinned parts (addon scopes) sit at their bone, the rest-pose sphere lies elsewhere
				Fmatrix W = *svp_pose_of(item.pMatrix);
				CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(V);
				if (sk)
				{
					Fmatrix boneR;
					if (svp_lens_bone_of(V, boneR) || sk->SVP_LensBoneXform(boneR))
						W.mulB_43(boneR);
				}
				Fvector c; W.transform_tiny(c, V->vis.sphere.P);
				Fvector ac; ac.sub(c, A);
				t = ac.dotproduct(ax) / len2;
				// radial distance to the axis LINE, the ocular stack (eyecup, rear housing) sits
				// on-axis behind the eyepiece and must match too or it shows as rings in the image
				Fvector p; p.set(A); p.mad(ax, t);
				rad = p.distance_to(c);
				// mounts wrap the tube with the center pulled off-axis, the size cap keeps a
				// barrel sphere from impersonating a clamp
				const float R = V->vis.sphere.R;
				const bool wrap = (rad < R * 0.6f) && (R < tube * 0.3f);
				if (objective_filter)
				{
					const bool role_ok = item.hud_role == IDSGraphManager::hud_primary_item
						|| item.hud_role == IDSGraphManager::hud_optic;
					const bool size_ok = _valid(R) && R > EPS && R < tube * 1.75f;
					const bool coaxial = rad < rcyl || wrap;
					const bool local = t > -0.6f && t < 1.4f;
					const bool bounds_ok = svp_axial_bounds(V->vis.box, W, A, axis, axial_lo, axial_hi);
					const bool contains_objective = bounds_ok
						&& axial_lo <= tube + 0.001f && axial_hi >= tube - 0.001f;
					const bool candidate = role_ok && size_ok && coaxial;
					if (candidate)
						++admit_candidates;
					drop = candidate && local && contains_objective;
					if (drop)
					{
						++admit_suppressed;
						admission = "objective-body";
					}
					else if (candidate && !contains_objective
						&& t >= 1.4f && t < clip_hi && rad < rcyl * 0.8f)
					{
						++admit_clipon_unresolved;
						admission = "clipon-retained";
					}
					else if (candidate && !contains_objective)
						admission = "plane-miss";
					else if (candidate)
						admission = "range-miss";
					else if (!role_ok)
						admission = "role";
					else if (!size_ok)
						admission = "size";
					else
						admission = "axis";
				}
				else
				{
					// clip-on optics sit coaxial PAST the objective, the tight radial keeps the
					// under-slung barrel out of this branch
					const bool clipon = (t >= 1.4f && t < clip_hi) && (rad < rcyl * 0.8f);
					const bool explicit_optic = item.hud_role == IDSGraphManager::hud_optic;
					drop = explicit_optic || ((R < tube * 1.75f)
						&& (clipon || ((t > -0.6f && t < 1.4f) && (rad < rcyl || wrap))));
					// only coaxial optic bodies define the front plane, a wrap mount's sphere is
					// length-dominated and says nothing about the front lens
					if (drop && (rad < rcyl || clipon))
						front = _max(front, t * tube + R);
					// a real clip-on front is a flat round disc, its mesh box has two long axes and a thin
					// one, a front-sight post is a spike and must not push the plane past the objective
					Fvector bs; V->vis.box.getsize(bs);
					const float bmax = std::max(bs.x, std::max(bs.y, bs.z));
					const float bmin = std::min(bs.x, std::min(bs.y, bs.z));
					const float bmid = bs.x + bs.y + bs.z - bmax - bmin;
					const bool disc_like = (bmax > EPS) && (bmid > 0.5f * bmax) && (bmin < 0.5f * bmax);
					if (clipon && disc_like)
						clipon_front = _max(clipon_front, t * tube + R);
					// mode 2 drops pieces wholly behind the front plane, spanning pieces render whole
					const float plane = (g_svp_legacy_front_m > EPS) ? g_svp_legacy_front_m : tube;
					if (!drop && g_svp_hud_skip_scope >= 2 && (t * tube + R) < plane)
						drop = true;
					extern int ps_r__svp_drain_clip;
					if (legacy_mode && ps_r__svp_drain_clip
						&& vp.svp_opt_offset.z > EPS && vp.eyepiece.radius > EPS
						&& (t * tube + R) < vp.svp_opt_offset.z * vp.eyepiece.radius)
						clip_obj = true;
				}
			}
			if (diag)
			{
				auto tx = V->GetTexture();
				if (objective_filter)
					PipMsg("[SVP-ADMIT] %s role=%s t=%.2f rad=%.1fcm R=%.1fcm axial=[%.1f,%.1f]cm objective=%.1fcm reason=%s %s",
						drop ? "SUPPRESS" : "retain", svp_hud_role_name(item.hud_role), t,
						rad * 100.f, V->vis.sphere.R * 100.f, axial_lo * 100.f, axial_hi * 100.f,
						tube * 100.f, admission, tx ? tx->cName.c_str() : "?");
				else
					PipMsg("[SVP-HUD] %s t=%.2f rad=%.1fcm R=%.1fcm %s", (drop || clip_obj) ? "SKIP" : "keep",
						t, rad * 100.f, V->vis.sphere.R * 100.f, tx ? tx->cName.c_str() : "?");
			}
			if ((drop && skip_ok) || clip_obj) continue;
			RCache.set_Element(item.pSE);
			RCache.set_xform_world(*svp_pose_of(item.pMatrix));
			RImplementation.apply_object(item.pObject);
			RImplementation.apply_lmaterial();
			V->Render(svp_drain_lod(item.ssa, V->vis.sphere.R));
#if RENDER == R_R4
			extern void svp_objective_hud_note_draw(u8);
			svp_objective_hud_note_draw(item.hud_role);
#endif
		}
		if (diag && objective_filter)
			PipMsg("[SVP-ADMIT] summary candidates=%u suppressed=%u clipon-unresolved=%u front=%.1fcm frame=%u session=%u",
				admit_candidates, admit_suppressed, admit_clipon_unresolved,
				vp.svp_front_use_m * 100.f, vp.svp_camera_frame, vp.svp_camera_session);
		// consumed by next frame's near plane, the authored (or detected) objective plane wins,
		// a confirmed clip-on front sitting beyond it keeps the plane ahead of the ring
		const float auth_z = vp.svp_opt_offset.z; // bus resolved, authored_optics folded in
		if (legacy_mode)
		{
			g_svp_legacy_front_m = _max(front, clipon_front);
			if (auth_z > EPS && vp.eyepiece.radius > EPS)
			{
				const float authored = auth_z * vp.eyepiece.radius;
				g_svp_legacy_front_m = _max(authored, clipon_front);
			}
		}
		graph.clear();
	}
	RCache.set_xform_world(Fidentity);
	RImplementation.rmNormal();
}
