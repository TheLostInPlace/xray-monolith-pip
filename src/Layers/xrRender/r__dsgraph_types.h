#pragma once

#include "../../xrCore/fixedmap.h"

//#ifndef USE_MEMORY_MONITOR
//#	define USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//#endif // USE_MEMORY_MONITOR

#ifdef USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//	extern doug_lea_allocator	g_render_lua_allocator;

	template <class T>
	class doug_lea_alloc {
	public:
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using pointer = T*;
		using const_pointer = const T*;
		using reference = T&;
		using const_reference = const T&;
		using value_type = T;

	public:
		template<class _Other>	
		struct rebind			{ using other = doug_lea_alloc<_Other>;	};
	public:
								pointer					address			(reference _Val) const					{	return (&_Val);	}
								const_pointer			address			(const_reference _Val) const			{	return (&_Val);	}
														doug_lea_alloc	()										{	}
														doug_lea_alloc	(const doug_lea_alloc<T>&)				{	}
		template<class _Other>							doug_lea_alloc	(const doug_lea_alloc<_Other>&)			{	}
		template<class _Other>	doug_lea_alloc<T>&		operator=		(const doug_lea_alloc<_Other>&)			{	return (*this);	}
								pointer					allocate		(size_type n, const void* p=0) const	{	return (T*)g_render_lua_allocator.malloc_impl(sizeof(T)*(u32)n);	}
								void					deallocate		(pointer p, size_type n) const			{	g_render_lua_allocator.free_impl	((void*&)p);				}
								void					deallocate		(void* p, size_type n) const			{	g_render_lua_allocator.free_impl	(p);				}
								char*					__charalloc		(size_type n)							{	return (char*)allocate(n); }
								void					construct		(pointer p, const T& _Val)				{	std::_Construct(p, _Val);	}
								void					destroy			(pointer p)								{	std::_Destroy(p);			}
								size_type				max_size		() const								{	size_type _Count = (size_type)(-1) / sizeof (T);	return (0 < _Count ? _Count : 1);	}
	};

	template<class _Ty,	class _Other>	inline	bool operator==(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (true);							}
	template<class _Ty, class _Other>	inline	bool operator!=(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (false);							}

	struct doug_lea_allocator_wrapper {
		template <typename T>
		struct helper {
			using result = doug_lea_alloc<T>;
		};

		static	void	*alloc		(const u32 &n)	{	return g_render_lua_allocator.malloc_impl((u32)n);	}
		template <typename T>
		static	void	dealloc		(T *&p)			{	g_render_lua_allocator.free_impl((void*&)p);	}
	};

#	define render_alloc				doug_lea_alloc
	using render_allocator = doug_lea_allocator_wrapper;

#else // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
#	define render_alloc				xalloc
using render_allocator = xr_allocator;
#endif // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER

class dxRender_Visual;

// #define	USE_RESOURCE_DEBUGGER

namespace R_dsgraph
{
	// Elementary types
	struct _NormalItem
	{
		float ssa;
		dxRender_Visual* pVisual;
	};

	struct _MatrixItem
	{
		float ssa;
		IRenderable* pObject;
		dxRender_Visual* pVisual;
		Fmatrix Matrix;				// matrix (copy)
		Fmatrix PrevMatrix;
	};

	struct _MatrixItemS : public _MatrixItem
	{
		ShaderElement* se;
	};

	struct _LodItem
	{
		float ssa;
		dxRender_Visual* pVisual;
	};

#ifdef USE_RESOURCE_DEBUGGER
		using vs_type = ref_vs;
		using ps_type = ref_ps;
#	if defined(USE_DX10) || defined(USE_DX11)
		using gs_type = ref_gs;
#		ifdef USE_DX11
		using hs_type = ref_hs;
		using ds_type = ref_ds;
#		endif
#	endif	//	USE_DX10
#else
#if defined(USE_DX10) || defined(USE_DX11)	//	DX10 needs shader signature to propperly bind deometry to shader
		using vs_type = SVS*;
		using gs_type = ID3DGeometryShader*;
#ifdef USE_DX11
			using hs_type = ID3D11HullShader*;
			using ds_type = ID3D11DomainShader*;
#endif
#else	//	USE_DX10
	using vs_type = ID3DVertexShader*;
#endif	//	USE_DX10
	using ps_type = ID3DPixelShader*;
#endif

	// NORMAL
	using mapNormalDirect = xr_vector<_NormalItem,render_allocator::helper<_NormalItem>::result>;
	using mapNormalItems = mapNormalDirect;
	using mapNormalTextures = FixedMAP<STextureList*, mapNormalItems, render_allocator>;
	using mapNormalStates = FixedMAP<ID3DState*, mapNormalTextures, render_allocator>;
	using mapNormalCS = FixedMAP<R_constant_table*, mapNormalStates, render_allocator>;
#ifdef USE_DX11
	struct	mapNormalAdvStages
	{
		hs_type		hs;
		ds_type		ds;
		mapNormalCS	mapCS;
	};
	using mapNormalPS = FixedMAP<ps_type, mapNormalAdvStages, render_allocator>;
#else
	using mapNormalPS = FixedMAP<ps_type, mapNormalCS, render_allocator>;
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	using mapNormalGS = FixedMAP<gs_type, mapNormalPS, render_allocator>;
	using mapNormalVS = FixedMAP<vs_type, mapNormalGS, render_allocator>;
#else	//	USE_DX10
	using mapNormalVS = FixedMAP<vs_type, mapNormalPS, render_allocator>;
#endif	//	USE_DX10
	using mapNormal_T = mapNormalVS;
	using mapNormalPasses_T = mapNormal_T[SHADER_PASSES_MAX];



	// MATRIX
	using mapMatrixDirect = xr_vector<_MatrixItem, render_allocator::helper<_MatrixItem>::result>;
	using mapMatrixItems = mapMatrixDirect;
	using mapMatrixTextures = FixedMAP<STextureList*, mapMatrixItems, render_allocator>;
	using mapMatrixStates = FixedMAP<ID3DState*, mapMatrixTextures, render_allocator>;
	using mapMatrixCS = FixedMAP<R_constant_table*, mapMatrixStates, render_allocator>;
#ifdef USE_DX11
	struct	mapMatrixAdvStages
	{
		hs_type		hs;
		ds_type		ds;
		mapMatrixCS	mapCS;
	};
	using mapMatrixPS = FixedMAP<ps_type, mapMatrixAdvStages, render_allocator>;
#else
	using mapMatrixPS = FixedMAP<ps_type, mapMatrixCS, render_allocator>;
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	using mapMatrixGS = FixedMAP<gs_type, mapMatrixPS, render_allocator>;
	using mapMatrixVS = FixedMAP<vs_type, mapMatrixGS, render_allocator>;
#else	//	USE_DX10
	using mapMatrixVS = FixedMAP<vs_type, mapMatrixPS, render_allocator>;
#endif	//	USE_DX10
	using mapMatrix_T = mapMatrixVS;
	using mapMatrixPasses_T = mapMatrix_T[SHADER_PASSES_MAX];

	// Top level
#if defined(USE_DX11)
	using mapScopeHUD_T = FixedMAP<float, _MatrixItemS, render_allocator>; // Redotix99: for 3D Shader Based Scopes
	using mapScopeHUD_T_Node = mapScopeHUD_T::TNode;
#endif

	using HUDMask_T = FixedMAP<float, _MatrixItemS, render_allocator>;
	using HUDMask_Node = HUDMask_T::TNode;

	using mapSorted_T = FixedMAP<float,_MatrixItemS,render_allocator>;
	using mapSorted_Node = mapSorted_T::TNode;

	using mapWater_T = FixedMAP<float, _MatrixItemS, render_allocator>;
	using mapWater_Node = mapWater_T::TNode;

	using mapHUD_T = FixedMAP<float, _MatrixItemS, render_allocator>;
	using mapHUD_Node = mapHUD_T::TNode;

	using mapLOD_T = FixedMAP<float,_LodItem,render_allocator>;
	using mapLOD_Node = mapLOD_T::TNode;
};
