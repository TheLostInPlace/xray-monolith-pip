// occ_engine.h: the occlusion engine seam CHOM renders occluders into and answers queries from
#pragma once

class occTri;

class IOccEngine
{
public:
	virtual ~IOccEngine()
	{
	}

	virtual void begin_frame(const Fmatrix& full, const Fvector& cop, float near_w) = 0;

	// world space occluder triangle, false means the tri contributed nothing and earns a skip stamp
	virtual bool emit(occTri& T, const Fvector* verts) = 0;
	virtual void end_frame() = 0;

	virtual BOOL test_box(const Fbox& world) = 0;
	virtual BOOL test_poly(const Fvector* v, u32 n) = 0;

	// legacy clips against the near plane inside emit, a masked rasterizer clips internally
	virtual bool wants_near_clip() = 0;
	virtual bool wants_skip_filter() = 0;
};

// the stock hierarchical raster, one instance wrapping the process wide Raster
IOccEngine& occ_engine_legacy();

#ifdef DEBUG
// tris the legacy near clip passed this frame, the HOM debug panel reads it
u32 occ_legacy_dbg_visible();
#endif
