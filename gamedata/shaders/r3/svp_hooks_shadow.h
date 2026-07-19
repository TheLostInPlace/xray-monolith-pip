// PIP_HOOK svp_hooks_shadow 20260715
// true pip logic for the 3DSS shadow chunk, rides mod.db0, the compat patch includes it

#ifndef SVP_HOOKS_SHADOW_H
#define SVP_HOOKS_SHADOW_H

#include "svp_hooks_common.h"

// the swing side slides the pupil center so the shadow enters from the side of the motion
// and a symmetric ring cannot form at center
float2 svp_shadow_swing(float2 off)
{
	if (shader_scope_params.w < -1.5)
		off += svp_exposure.zw;
	return off;
}

// the parallax crescent rides the swing envelope, calm aim shows none and a hard weapon
// swing sweeps in a dark crescent that fades back out quickly
float4 svp_shadow_soften(float4 shadow)
{
	if (shader_scope_params.w < -1.5)
	{
		shadow.rgb = 0;
		shadow.a *= saturate((svp_glass4.w - 0.3) / 0.35);
	}
	return shadow;
}

#endif
