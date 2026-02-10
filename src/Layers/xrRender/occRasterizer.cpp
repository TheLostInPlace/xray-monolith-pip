// occRasterizer.cpp: implementation of the occRasterizer class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "occRasterizer.h"

#if DEBUG
#include "dxRenderDeviceRender.h"
#include "xrRender_console.h"
#endif

occRasterizer Raster;

void __stdcall fillDW_8x(void* _p, u32 size, u32 value)
{
	LPDWORD ptr = LPDWORD(_p);
	LPDWORD end = ptr + size;
	for (; ptr != end; ptr += 2)
	{
		ptr[0] = value;
		ptr[1] = value;
	}
}

// SSE2 implementation of Max for 32-bit signed integers
IC __m128i _mm_max_epi32_sse2(__m128i a, __m128i b) {
    __m128i mask = _mm_cmpgt_epi32(a, b);        // Compare a > b
    __m128i a_part = _mm_and_si128(mask, a);     // Keep 'a' where true
    __m128i b_part = _mm_andnot_si128(mask, b);  // Keep 'b' where false
    return _mm_or_si128(a_part, b_part);         // Combine
}

IC void propagade_depth(LPVOID p_dest, LPVOID p_src, int dim)
{
    occD* dest = (occD*)p_dest;
    occD* src = (occD*)p_src;

    int src_stride = dim * 2;

    for (int y = 0; y < dim; y++)
    {
        int src_y0 = (y * 2) * src_stride;
        int src_y1 = src_y0 + src_stride;
        int dest_y = y * dim;

        int x = 0;
        // Process 4 destination pixels at once (requires 8 horizontal source pixels)
        for (; x <= dim - 4; x += 4)
        {
            int src_x = x * 2;

            // Load 2 rows of 8 source pixels each
            // Row 0: [f1, f2, f1, f2, f1, f2, f1, f2]
            // Row 1: [f3, f4, f3, f4, f3, f4, f3, f4]
            __m128i r0_part0 = _mm_loadu_si128((__m128i*)(src + src_y0 + src_x));     // Pixels 0-3
            __m128i r0_part1 = _mm_loadu_si128((__m128i*)(src + src_y0 + src_x + 4)); // Pixels 4-7

            __m128i r1_part0 = _mm_loadu_si128((__m128i*)(src + src_y1 + src_x));     // Pixels 0-3
            __m128i r1_part1 = _mm_loadu_si128((__m128i*)(src + src_y1 + src_x + 4)); // Pixels 4-7

            // 1. Maximize Vertically: Max(Row0, Row1)
            __m128i max_v0 = _mm_max_epi32_sse2(r0_part0, r1_part0);
            __m128i max_v1 = _mm_max_epi32_sse2(r0_part1, r1_part1);

            // 2. Maximize Horizontally: 
            // We need to compare neighbor pairs. 
            // We can do this by shuffling the max_v results and comparing against themselves.
            // Shuffle to get [f2, f1, f4, f3] style alignment
            __m128i shuf0 = _mm_shuffle_epi32(max_v0, _MM_SHUFFLE(2, 3, 0, 1));
            __m128i shuf1 = _mm_shuffle_epi32(max_v1, _MM_SHUFFLE(2, 3, 0, 1));

            __m128i max_final0 = _mm_max_epi32_sse2(max_v0, shuf0);
            __m128i max_final1 = _mm_max_epi32_sse2(max_v1, shuf1);

            // 3. Collect the results
            // Move R1 and R3 into the lower half of their respective registers
            // Current: [3, 2, 1, 0] -> Shuffle to get 2 into 1's slot
            // We want to align the values so we can "zip" them together.
            __m128i res0 = _mm_shuffle_epi32(max_final0, _MM_SHUFFLE(3, 2, 2, 0)); // [?, R1, R1, R0]
            __m128i res1 = _mm_shuffle_epi32(max_final1, _MM_SHUFFLE(3, 2, 2, 0)); // [?, R3, R3, R2]

            // Now we use the 64-bit unpack to take the bottom 64 bits of both
            // res0 lower 64: [R1, R0]
            // res1 lower 64: [R3, R2]
            __m128i result = _mm_unpacklo_epi64(res0, res1); // [R3, R2, R1, R0]

            _mm_storeu_si128((__m128i*)(dest + dest_y + x), result);
        }

        // Scalar fallback for odd dimensions
        for (; x < dim; x++)
        {
            occD* base0 = src + src_y0 + (x * 2);
            occD* base1 = src + src_y1 + (x * 2);
            occD f = base0[0];
            if (base0[1] > f) f = base0[1];
            if (base1[0] > f) f = base1[0];
            if (base1[1] > f) f = base1[1];
            dest[dest_y + x] = f;
        }
    }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

occRasterizer::occRasterizer()
#if DEBUG
:dbg_HOM_draw_initialized(false)
#endif
{
}

occRasterizer::~occRasterizer()
{
}

void occRasterizer::clear()
{
	u32 size = occ_dim * occ_dim;
	float f = 1.f;
	Memory.mem_fill32(bufFrame, 0, size * (sizeof(void*) / 4));
	Memory.mem_fill32(bufDepth, *LPDWORD(&f), size);
}

IC BOOL shared(occTri* T1, occTri* T2)
{
	if (T1 == T2) return TRUE;
	if (T1->adjacent[0] == T2) return TRUE;
	if (T1->adjacent[1] == T2) return TRUE;
	if (T1->adjacent[2] == T2) return TRUE;
	return FALSE;
}

void occRasterizer::propagade()
{
    using namespace DirectX;
    occTri** pFrame = get_frame();
    float* pDepth = get_depth();

    // Constant for conversion: df_2_s32 uses occQ_s32
    const XMVECTOR vScale = XMVectorReplicate(occQ_s32);
    const XMVECTOR vMin = XMVectorReplicate(-1.99f);
    const XMVECTOR vMax = XMVectorReplicate(1.99f);

    for (int y = 0; y < occ_dim_0; y++)
    {
        // Pointers to the integer output row
        int* destRow = &bufDepth_0[y][0];

        for (int x = 0; x < occ_dim_0; x++)
        {
            int ox = x + 2, oy = y + 2;
            int pos = oy * occ_dim + ox;

            // --- Y2-connect logic ---
            // We keep this part scalar as shared() involves pointer-walking
            // which doesn't vectorize easily, but we've streamlined the bounds.
            int pos_up = pos - occ_dim;
            int pos_down = pos + occ_dim;
            int pos_down2 = pos_down + occ_dim;

            // Safety guards (using __max/__min to avoid branching)
            if (pos_up < 0) pos_up = pos;
            if (pos_down >= occ_dim * occ_dim) pos_down = pos;
            if (pos_down2 >= occ_dim * occ_dim) pos_down2 = pos_down;

            occTri* Tu1 = pFrame[pos_up];
            if (Tu1)
            {
                occTri* Td1 = pFrame[pos_down];
                occTri* Td2 = pFrame[pos_down2];

                if (Td1 && shared(Tu1, Td1))
                {
                    float ZR = (pDepth[pos_up] + pDepth[pos_down]) * 0.5f;
                    if (ZR < pDepth[pos]) { pFrame[pos] = Tu1; pDepth[pos] = ZR; }
                }
                else if (Td2 && shared(Tu1, Td2))
                {
                    float ZR = (pDepth[pos_up] + pDepth[pos_down2]) * 0.5f;
                    if (ZR < pDepth[pos]) { pFrame[pos] = Tu1; pDepth[pos] = ZR; }
                }
            }

            // --- Vectorized Conversion (Process in blocks of 4) ---
            // If x is a multiple of 4 and we have room, process 4 pixels
            if ((x % 4 == 0) && (x <= occ_dim_0 - 4))
            {
                // Load 4 depths: [p0, p1, p2, p3]
                // Note: We need to handle the +2 offset for the SIMD load
                XMVECTOR vD = _mm_loadu_ps(&pDepth[pos]);

                // Clamp
                vD = XMVectorClamp(vD, vMin, vMax);

                // Convert to s32: floor(d * scale)
                vD = XMVectorMultiply(vD, vScale);

                // Truncate/Floor to Ints
                __m128i vInt = _mm_cvttps_epi32(vD);

                // Store to bufDepth_0
                _mm_storeu_si128((__m128i*) & bufDepth_0[y][x], vInt);

                x += 3; // Loop will add the 4th
                continue;
            }

            // Scalar fallback for remaining pixels
            float d = pDepth[pos];
            if (d < -1.99f) d = -1.99f; else if (d > 1.99f) d = 1.99f;
            bufDepth_0[y][x] = df_2_s32(d);
        }
    }

    // Propagate other levels (MIP-mapping)
    propagade_depth(bufDepth_1, bufDepth_0, occ_dim_1);
    propagade_depth(bufDepth_2, bufDepth_1, occ_dim_2);
    propagade_depth(bufDepth_3, bufDepth_2, occ_dim_3);
}

void occRasterizer::on_dbg_render()
{
#if DEBUG
	if( !ps_r2_ls_flags_ext.is(R_FLAGEXT_HOM_DEPTH_DRAW) )
	{
		dbg_HOM_draw_initialized = false;
		return;
	}

	for ( int i = 0; i< occ_dim_0; ++i)
	{
		for ( int j = 0; j< occ_dim_0; ++j)
		{
			if( bDebug )
			{
				Fvector quad,left_top,right_bottom,box_center,box_r;
				quad.set( (float)j-occ_dim_0/2.f, -((float)i-occ_dim_0/2.f), (float)bufDepth_0[i][j]/occQ_s32);
				Device.mProject;

				float z = -Device.mProject._43/(float)(Device.mProject._33-quad.z);
				left_top.set		( quad.x*z/Device.mProject._11/(occ_dim_0/2.f),		quad.y*z/Device.mProject._22/(occ_dim_0/2.f), z);
				right_bottom.set	( (quad.x+1)*z/Device.mProject._11/(occ_dim_0/2.f), (quad.y+1)*z/Device.mProject._22/(occ_dim_0/2.f), z);

				box_center.set		((right_bottom.x + left_top.x)/2, (right_bottom.y + left_top.y)/2, z);
				box_r = right_bottom;
				box_r.sub(box_center);

				Fmatrix inv;
				inv.invert(Device.mView);
				inv.transform( box_center );
				inv.transform_dir( box_r );

				pixel_box& tmp = dbg_pixel_boxes[ i*occ_dim_0+j];
				tmp.center	= box_center;
				tmp.radius	= box_r;
				tmp.z 		= quad.z;
				dbg_HOM_draw_initialized = true;
			}

			if( !dbg_HOM_draw_initialized )
				return;

			pixel_box& tmp = dbg_pixel_boxes[ i*occ_dim_0+j];
			Fmatrix Transform;
			Transform.identity();
			Transform.translate(tmp.center);

			// draw wire
			Device.SetNearer(TRUE);

			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_DrawOBB( Transform, tmp.radius, D3DCOLOR_XRGB(u32(255*pow(tmp.z,20.f)),u32(255*(1-pow(tmp.z,20.f))),0) );
			Device.SetNearer(FALSE);
		}
	}
#endif
}


IC BOOL test_Level(occD* depth, int dim, float _x0, float _y0, float _x1, float _y1, occD z)
{
    // 1. Calculate integer bounds
    int x0 = iFloor(_x0 * dim + .5f); clamp(x0, 0, dim - 1);
    int x1 = iFloor(_x1 * dim + .5f); clamp(x1, x0, dim - 1);
    int y0 = iFloor(_y0 * dim + .5f); clamp(y0, 0, dim - 1);
    int y1 = iFloor(_y1 * dim + .5f); clamp(y1, y0, dim - 1);

    // 2. Prepare SIMD Z value
    __m128i vz = _mm_set1_epi32(z);

    for (int y = y0; y <= y1; y++)
    {
        occD* base = depth + y * dim;
        int x = x0;

        // 3. Vectorized Loop (4 pixels at a time)
        for (; x <= x1 - 3; x += 4)
        {
            __m128i vDepth = _mm_loadu_si128((__m128i*)(base + x));

            // Compare: Is z < depth? (PCMPEQ/PCMPGT logic)
            // Note: _mm_cmplt_epi32 is SSE4.1. For older, use _mm_cmpgt_epi32(vDepth, vz)
            __m128i vMask = _mm_cmpgt_epi32(vDepth, vz);

            // If any bit is set, at least one pixel is visible
            if (_mm_movemask_ps(_mm_castsi128_ps(vMask)))
                return TRUE;
        }

        // 4. Scalar remainder
        for (; x <= x1; x++)
        {
            if (z < base[x]) return TRUE;
        }
    }
    return FALSE;
}


BOOL occRasterizer::test(float _x0, float _y0, float _x1, float _y1, float _z)
{
    // df_2_s32up adds a small epsilon (+1) to ensure conservative testing
    occD z = df_2_s32up(_z) + 1;

    // First: Test Level 2 (16x16). 
    // If it's occluded here, it's definitely occluded at Level 0.
    if (test_Level(get_depth_level(2), occ_dim_2, _x0, _y0, _x1, _y1, z))
    {
        // Second: If visible at Level 2, we must verify against Level 0 (64x64).
        return test_Level(get_depth_level(0), occ_dim_0, _x0, _y0, _x1, _y1, z);
    }

    return FALSE;
}
