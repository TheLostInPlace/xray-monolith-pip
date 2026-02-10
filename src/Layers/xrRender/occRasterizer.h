// occRasterizer.h: interface for the occRasterizer class.
//////////////////////////////////////////////////////////////////////
#pragma once

const int occ_dim_0 = 64;
const int occ_dim_1 = occ_dim_0 / 2;
const int occ_dim_2 = occ_dim_1 / 2;
const int occ_dim_3 = occ_dim_2 / 2;
const int occ_dim = occ_dim_0 + 4; // 2 pixel border around frame

#include <DirectXMath.h>

class alignas(16) occTri
{
public:
    // Pointers are 8-bytes each on x64
    occTri* adjacent[3];

    // Using DirectX types for the screen-space vertices
    // These are the projected X, Y, Z coordinates
    DirectX::XMFLOAT3   raster[3];

    Fplane              plane;
    float               area;
    u32                 flags;
    u32                 skip;
    Fvector             center;

    // Helper to get a SIMD vector from the raster data
    inline DirectX::XMVECTOR get_raster_v(int idx) const
    {
        return DirectX::XMLoadFloat3(&raster[idx]);
    }
};

const float occQ_s32 = float(0x40000000); // [-2..2]
const float occQ_s16 = float(16384 - 1); // [-2..2]
typedef s32 occD;

class occRasterizer
{
private:
    alignas(64) occTri* bufFrame[occ_dim][occ_dim];
    alignas(64) float   bufDepth[occ_dim][occ_dim];

	occD bufDepth_0 [occ_dim_0][occ_dim_0];
	occD bufDepth_1 [occ_dim_1][occ_dim_1];
	occD bufDepth_2 [occ_dim_2][occ_dim_2];
	occD bufDepth_3 [occ_dim_3][occ_dim_3];

    occTri* currentTri = 0;
    u32 dwPixels = 0;

    alignas(16) DirectX::XMVECTOR currentV[3];
public:
	IC int df_2_s32(float d) { return iFloor(d * occQ_s32); }
	IC s16 df_2_s16(float d) { return s16(iFloor(d * occQ_s16)); }
	IC int df_2_s32up(float d) { return iCeil(d * occQ_s32); }
	IC s16 df_2_s16up(float d) { return s16(iCeil(d * occQ_s16)); }
	IC float ds32_2_f(s32 d) { return float(d) / occQ_s32; }
	IC float ds16_2_f(s16 d) { return float(d) / occQ_s16; }

	void clear();
	void propagade();
	u32 rasterize(occTri* T);
	BOOL test(float x0, float y0, float x1, float y1, float z);

	occTri** get_frame() { return &(bufFrame[0][0]); }
	float* get_depth() { return &(bufDepth[0][0]); }

	occD* get_depth_level(int level)
	{
		switch (level)
		{
		case 0: return &(bufDepth_0[0][0]);
		case 1: return &(bufDepth_1[0][0]);
		case 2: return &(bufDepth_2[0][0]);
		case 3: return &(bufDepth_3[0][0]);
		default: return NULL;
		}
	}

	void on_dbg_render();

    void i_order(const DirectX::XMFLOAT3& V0, const DirectX::XMFLOAT3& V1, const DirectX::XMFLOAT3& V2);
    void Vclamp(int& v, int a, int b);
    BOOL shared(occTri* T1, occTri* T2);
    void i_scan(int curY, float leftX, float lhx, float rightX, float rhx, float startZ, float endZ);
    void i_section(int Sect, BOOL bMiddle);
    void i_section_b0();
    void i_section_b1();
    void i_section_t0();
    void i_section_t1();

#if DEBUG
	struct pixel_box
	{
		Fvector center;
		Fvector radius;
		float	z;
	}  dbg_pixel_boxes [occ_dim_0*occ_dim_0];
	bool dbg_HOM_draw_initialized;
	
#endif

	occRasterizer();
	~occRasterizer();
};

extern occRasterizer Raster;
