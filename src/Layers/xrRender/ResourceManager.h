// TextureManager.h: interface for the CTextureManager class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ResourceManagerH
#define ResourceManagerH
#pragma once

#include	"shader.h"
#include	"tss_def.h"
#include	"TextureDescrManager.h"
// refs
struct lua_State;

class dx10ConstantBuffer;

// defs
class ECORE_API CResourceManager
{
private:
    struct str_hash
    {
        using is_transparent = void;
        size_t operator()(const char* s) const
        {
            return xr_hash<std::string_view>{}(s);
        }
        size_t operator()(const shared_str& s) const
        {
            return xr_hash<std::string_view>{}(s.c_str());
        }
    };

    struct str_equal
    {
        using is_transparent = void;
        bool operator()(const char* lhs, const char* rhs) const
        {
            return xr_strcmp(lhs, rhs) == 0;
        }
    };

	struct texture_detail
	{
		const char* T;
		R_constant_setup* cs;
	};

public:
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, IBlender*, map_Blender, map_BlenderIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, CTexture*, map_Texture, map_TextureIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, CMatrix*, map_Matrix, map_MatrixIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, CConstant*, map_Constant, map_ConstantIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, CRT*, map_RT, map_RTIt, str_hash, str_equal);
    //	DX10 cut DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*,CRTC*,			map_RTC,		map_RTCIt,			str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SVS*, map_VS, map_VSIt, str_hash, str_equal);
#if defined(USE_DX10) || defined(USE_DX11)
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SGS*, map_GS, map_GSIt, str_hash, str_equal);
#endif	//	USE_DX10
#ifdef USE_DX11
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SHS*, map_HS, map_HSIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SDS*, map_DS, map_DSIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SCS*, map_CS, map_CSIt, str_hash, str_equal);
#endif

    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, SPS*, map_PS, map_PSIt, str_hash, str_equal);
    DEFINE_UNORDERED_FLAT_MAP_PRED_EQUAL(const char*, texture_detail, map_TD, map_TDIt, str_hash, str_equal);
private:
	// data
	map_Blender m_blenders;
	map_Texture m_textures;
	map_Matrix m_matrices;
	map_Constant m_constants;
	map_RT m_rtargets;
	//	DX10 cut map_RTC												m_rtargets_c;
	map_VS m_vs;
	map_PS m_ps;
#if defined(USE_DX10) || defined(USE_DX11)
	map_GS												m_gs;
#endif	//	USE_DX10
	map_TD m_td;

    struct SState_equal
    {
        using is_transparent = void;

        // Pointer vs Pointer
        bool operator()(const SState* lhs, const SState* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<SState*>(lhs)->state_code.equal(const_cast<SState*>(rhs)->state_code);
        }

        // Pointer vs Raw Code (Transparent)
        bool operator()(const SState* lhs, const SimulatorStates& rhs) const
        {
            if (!lhs) return false;
            return const_cast<SState*>(lhs)->state_code.equal(const_cast<SimulatorStates&>(rhs));
        }

        // Raw Code vs Pointer
        bool operator()(const SimulatorStates& lhs, const SState* rhs) const
        {
            return (*this)(rhs, lhs);
        }

    };
    struct SState_hash
    {
        using is_transparent = void;

        // Hash for the stored pointer
        size_t operator()(const SState* s) const
        {
            return s ? hash_internal(s->state_code) : 0;
        }

        // Hash for the lookup object (Transparent)
        size_t operator()(const SimulatorStates& code) const
        {
            return hash_internal(code);
        }

    private:
       static size_t hash_internal(const SimulatorStates& s)
       {
            if (s.States.empty()) return 0;
            std::string_view view(
                reinterpret_cast<const char*>(s.States.data()),
                s.States.size() * sizeof(SimulatorStates::State)
            );
            return xr_hash<std::string_view>{}(view);
        }
    };
	xr_unordered_flat_set<SState*, SState_hash, SState_equal> v_states;

    struct SDeclaration_equal
    {
        using is_transparent = void;

        bool operator()(const SDeclaration* lhs, const SDeclaration* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return dcl_equal(lhs->dcl_code.data(), rhs->dcl_code.data());
        }

        bool operator()(const SDeclaration* lhs, const D3DVERTEXELEMENT9* rhs) const
        {
            if (!lhs || !rhs) return false;
            return dcl_equal(lhs->dcl_code.data(), rhs);
        }

        bool operator()(const D3DVERTEXELEMENT9* lhs, const SDeclaration* rhs) const
        {
            return (*this)(rhs, lhs);
        }

    private:
        static bool dcl_equal(const D3DVERTEXELEMENT9* a, const D3DVERTEXELEMENT9* b)
        {
            // check sizes
            u32 a_size = D3DXGetDeclLength(a);
            u32 b_size = D3DXGetDeclLength(b);
            if (a_size != b_size) return false;
            return 0 == memcmp(a, b, a_size * sizeof(D3DVERTEXELEMENT9));
        }
    };
    struct SDeclaration_hash
    {
        using is_transparent = void;

        size_t operator()(const SDeclaration* dcl) const
        {
            return dcl ? hash_internal(dcl->dcl_code.data()) : 0;
        }

        size_t operator()(const D3DVERTEXELEMENT9* code) const
        {
            return code ? hash_internal(code) : 0;
        }

    private:
        static size_t hash_internal(const D3DVERTEXELEMENT9* code)
        {
            u32 size = D3DXGetDeclLength(code);
            std::string_view view(
                reinterpret_cast<const char*>(code),
                (size + 1) * sizeof(D3DVERTEXELEMENT9)
            );
            return xr_hash<std::string_view>{}(view);
        }
    };
	xr_unordered_flat_set<SDeclaration*, SDeclaration_hash, SDeclaration_equal> v_declarations;

    struct SGeometry_key
    {
        SDeclaration* dcl;
#if defined(USE_DX10) || defined(USE_DX11)
        ID3DVertexBuffer* vb;
        ID3DIndexBuffer* ib;
#else
        IDirect3DVertexBuffer9* vb;
        IDirect3DIndexBuffer9* ib;
#endif
        u32 vb_stride;

        // Helper for easier equality checks
        bool operator==(const SGeometry_key& other) const
        {
            return dcl == other.dcl && vb == other.vb && ib == other.ib && vb_stride == other.vb_stride;
        }
    };
	struct SGeometry_equal
	{
		using is_transparent = void;

		bool operator()(const SGeometry* lhs, const SGeometry* rhs) const
		{
			if (lhs == rhs) return true;
			if (!lhs || !rhs) return false;
			return (lhs->dcl == rhs->dcl) && 
				   (lhs->vb == rhs->vb) && 
				   (lhs->ib == rhs->ib) && 
				   (lhs->vb_stride == rhs->vb_stride);
		}
        bool operator()(const SGeometry* lhs, const SGeometry_key& rhs) const
        {
            if (!lhs) return false;
            return lhs->dcl == rhs.dcl && lhs->vb == rhs.vb && lhs->ib == rhs.ib && lhs->vb_stride == rhs.vb_stride;
        }
        bool operator()(const SGeometry_key& lhs, const SGeometry* rhs) const
        {
            return (*this)(rhs, lhs);
        }
	};
	struct SGeometry_hash
	{
		using is_transparent = void;

        size_t operator()(const SGeometry* g) const
        {
            return g ? hash_internal({ g->dcl._get(), g->vb, g->ib, g->vb_stride}) : 0;
        }
        size_t operator()(const SGeometry_key& key) const
        {
            return hash_internal(key);
        }
    private:
        static size_t hash_internal(const SGeometry_key& k)
        {
            size_t seed = 0;
            // Combine hashes of all 4 members
            static auto hash_combine = [](auto val, size_t& seed)
            {
                seed ^= xr_hash<decltype(val)>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
            hash_combine(k.dcl, seed);
            hash_combine(k.vb, seed);
            hash_combine(k.ib, seed);
            hash_combine(k.vb_stride, seed);
            return seed;
        }
	};
	xr_unordered_flat_set<SGeometry*, SGeometry_hash, SGeometry_equal> v_geoms;

    struct R_constant_table_equal
    {
        using is_transparent = void;

        // Table* vs Table*
        bool operator()(const R_constant_table* lhs, const R_constant_table* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<R_constant_table*>(lhs)->equal(*const_cast<R_constant_table*>(rhs));
        }

        // Table* vs Table& (Transparent)
        bool operator()(const R_constant_table* lhs, const R_constant_table& rhs) const
        {
            if (!lhs) return false;
            return const_cast<R_constant_table*>(lhs)->equal(const_cast<R_constant_table&>(rhs));
        }

        // Table& vs Table* (Symmetry)
        bool operator()(const R_constant_table& lhs, const R_constant_table* rhs) const
        {
            return (*this)(rhs, lhs);
        }
    };
    struct R_constant_table_hash
    {
        using is_transparent = void;

        size_t operator()(const R_constant_table* t) const
        {
            return t ? hash_internal(*t) : 0;
        }

        size_t operator()(const R_constant_table& t) const
        {
            return hash_internal(t);
        }

    private:
        static size_t hash_internal(const R_constant_table& t)
        {
            size_t seed = 0;
            for (const auto& it : t.table)
            {
                if (!it) continue;

                // name
                seed ^= xr_hash<std::string_view>{}(it->name.c_str()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

                // metadata
                seed ^= xr_hash<u16>{}(it->type) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= xr_hash<u32>{}(it->destination) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

                static auto hash_load = [&](const R_constant_load& L, size_t& seed) {
                    seed ^= xr_hash<u16>{}(L.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    seed ^= xr_hash<u16>{}(L.cls) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
                hash_load(it->ps, seed);
                hash_load(it->vs, seed);
                hash_load(it->samp, seed);

                seed ^= xr_hash<R_constant_setup*>{}(it->handler) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
	xr_unordered_flat_set<R_constant_table*, R_constant_table_hash, R_constant_table_equal> v_constant_tables;

#if defined(USE_DX10) || defined(USE_DX11)
    struct SConstantBuffer_equal
    {
        using is_transparent = void;

        // Pointer vs Pointer
        bool operator()(const dx10ConstantBuffer* lhs, const dx10ConstantBuffer* rhs) const
        {
            return const_cast<dx10ConstantBuffer*>(lhs)->Similar(*const_cast<dx10ConstantBuffer*>(rhs));
        }

        // Pointer vs Reflection Interface
        bool operator()(const dx10ConstantBuffer* lhs, ID3DShaderReflectionConstantBuffer* rhs) const
        {
            D3D_SHADER_BUFFER_DESC desc;
            rhs->GetDesc(&desc);

            if (lhs->m_uiBufferSize != desc.Size) return false;
            if (lhs->m_eBufferType != desc.Type) return false;
            if (0 != xr_strcmp(lhs->m_strBufferName.c_str(), desc.Name)) return false;

            // Final check: CRC
            return lhs->m_uiMembersCRC == dx10ConstantBuffer::GetReflectionCRC(rhs);
        }

        // Reflection Interface vs Pointer (Symmetry)
        bool operator()(ID3DShaderReflectionConstantBuffer* lhs, const dx10ConstantBuffer* rhs) const
        {
            return (*this)(rhs, lhs); // Forward to the implementation above
        }
    };
    struct SConstantBuffer_hash
    {
        using is_transparent = void;

        // Hash for the stored buffer pointer
        size_t operator()(const dx10ConstantBuffer* b) const
        {
            if (!b) return 0;
            return hash_internal(b->m_strBufferName.c_str(), b->m_eBufferType, b->m_uiBufferSize, b->m_uiMembersCRC);
        }

        // Hash for the reflection interface (Transparent)
        size_t operator()(ID3DShaderReflectionConstantBuffer* pTable) const
        {
            D3D_SHADER_BUFFER_DESC desc;
            pTable->GetDesc(&desc);
            u32 uiMembersCRC = dx10ConstantBuffer::GetReflectionCRC(pTable);
            return hash_internal(desc.Name, desc.Type, desc.Size, uiMembersCRC);
        }

    private:
        static size_t hash_internal(std::string_view name, u32 type, u32 size, u32 crc)
        {
            size_t seed = 0;
            static auto hash_combine = [](auto val, size_t& seed)
            {
                seed ^= xr_hash<decltype(val)>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
            hash_combine(name, seed);
            hash_combine(type, seed);
            hash_combine(size, seed);
            hash_combine(crc, seed);
            return seed;
        }
    };
	xr_unordered_flat_set<dx10ConstantBuffer*, SConstantBuffer_hash, SConstantBuffer_equal> v_constant_buffer;

    struct SInputSignature_equal
    {
        using is_transparent = void;

        // Pointer vs Pointer
        bool operator()(const SInputSignature* lhs, const SInputSignature* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return compare(lhs->signature, rhs->signature);
        }

        // Pointer vs Blob (Transparent)
        bool operator()(const SInputSignature* lhs, ID3DBlob* rhs) const
        {
            if (!lhs || !rhs) return false;
            return compare(lhs->signature, rhs);
        }

        // Blob vs Pointer (Symmetry)
        bool operator()(ID3DBlob* lhs, const SInputSignature* rhs) const
        {
            return (*this)(rhs, lhs);
        }

    private:
        static bool compare(ID3DBlob* a, ID3DBlob* b) {
            if (a->GetBufferSize() != b->GetBufferSize()) return false;
            return 0 == memcmp(a->GetBufferPointer(), b->GetBufferPointer(), a->GetBufferSize());
        }
    };
    struct SInputSignature_hash
    {
        using is_transparent = void;

        // Hash for the stored resource
        size_t operator()(const SInputSignature* s) const
        {
            if (!s || !s->signature) return 0;
            return hash_internal(s->signature->GetBufferPointer(), s->signature->GetBufferSize());
        }

        // Hash for the raw blob (Transparent Lookup)
        size_t operator()(ID3DBlob* pBlob) const
        {
            if (!pBlob) return 0;
            return hash_internal(pBlob->GetBufferPointer(), pBlob->GetBufferSize());
        }

    private:
        static size_t hash_internal(const void* pData, size_t size)
        {
            std::string_view view(static_cast<const char*>(pData), size);
            return xr_hash<std::string_view>{}(view);
        }
    };
	xr_unordered_flat_set<SInputSignature*, SInputSignature_hash, SInputSignature_equal> v_input_signature;
#endif	//	USE_DX10

	// lists
    struct STextureList_equal
    {
        using is_transparent = void;

        bool operator()(const STextureList* lhs, const STextureList* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<STextureList*>(lhs)->equal(*const_cast<STextureList*>(rhs));
        }

        bool operator()(const STextureList* lhs, const STextureList& rhs) const
        {
            if (!lhs) return false;
            return const_cast<STextureList*>(lhs)->equal(const_cast<STextureList&>(rhs));
        }

        bool operator()(const STextureList& lhs, const STextureList* rhs) const
        {
            return (*this)(rhs, lhs);
        }
    };
    struct STextureList_hash
    {
        using is_transparent = void;

        size_t operator()(const STextureList* l) const {
            return l ? hash_internal(*l) : 0;
        }

        size_t operator()(const STextureList& l) const {
            return hash_internal(l);
        }

    private:
        static size_t hash_internal(const STextureList& l)
        {
            size_t seed = 0;
            for (const auto& it : l)
            {
                seed ^= xr_hash<u32>{}(it.first) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= xr_hash<CTexture*>{}(it.second._get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
	xr_unordered_flat_set<STextureList*, STextureList_hash, STextureList_equal> lst_textures;

    struct SMatrixList_equal
    {
        using is_transparent = void;

        bool operator()(const SMatrixList* lhs, const SMatrixList* rhs) const {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<SMatrixList*>(lhs)->equal(*const_cast<SMatrixList*>(rhs));
        }

        bool operator()(const SMatrixList* lhs, const SMatrixList& rhs) const {
            if (!lhs) return false;
            return const_cast<SMatrixList*>(lhs)->equal(const_cast<SMatrixList&>(rhs));
        }

        bool operator()(const SMatrixList& lhs, const SMatrixList* rhs) const {
            return (*this)(rhs, lhs);
        }
    };
    struct SMatrixList_hash
    {
        using is_transparent = void;

        size_t operator()(const SMatrixList* l) const
        {
            return l ? hash_internal(*l) : 0;
        }

        size_t operator()(const SMatrixList& l) const
        {
            return hash_internal(l);
        }

    private:
        static size_t hash_internal(const SMatrixList& l)
        {
            size_t seed = 0;
            for (const auto& it : l)
            {
                seed ^= xr_hash<CMatrix*>{}(it._get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
	xr_unordered_flat_set<SMatrixList*, SMatrixList_hash, SMatrixList_equal> lst_matrices;

    struct SConstantList_equal
    {
        using is_transparent = void;

        bool operator()(const SConstantList* lhs, const SConstantList* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<SConstantList*>(lhs)->equal(*const_cast<SConstantList*>(rhs));
        }

        bool operator()(const SConstantList* lhs, const SConstantList& rhs) const
        {
            if (!lhs) return false;
            return const_cast<SConstantList*>(lhs)->equal(const_cast<SConstantList&>(rhs));
        }

        bool operator()(const SConstantList& lhs, const SConstantList* rhs) const
        {
            return (*this)(rhs, lhs);
        }
    };
    struct SConstantList_hash
    {
        using is_transparent = void;

        size_t operator()(const SConstantList* l) const
        {
            return l ? hash_internal(*l) : 0;
        }

        size_t operator()(const SConstantList& l) const
        {
            return hash_internal(l);
        }

    private:
        static size_t hash_internal(const SConstantList& l)
        {
            size_t seed = 0;
            for (const auto& it : l) {
                seed ^= xr_hash<CConstant*>{}(it._get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
	xr_unordered_flat_set<SConstantList*, SConstantList_hash, SConstantList_equal> lst_constants;

	// main shader-array
    struct SPass_equal {
        using is_transparent = void;

        bool operator()(const SPass* lhs, const SPass* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<SPass*>(lhs)->equal(*const_cast<SPass*>(rhs));
        }

        bool operator()(const SPass* lhs, const SPass& rhs) const
        {
            if (!lhs) return false;
            return const_cast<SPass*>(lhs)->equal(const_cast<SPass&>(rhs));
        }

        bool operator()(const SPass& lhs, const SPass* rhs) const
        {
            return (*this)(rhs, lhs);
        }
    };
    struct SPass_hash
    {
        using is_transparent = void;

        size_t operator()(const SPass* p) const
        {
            return p ? hash_internal(*p) : 0;
        }

        size_t operator()(const SPass& p) const
        {
            return hash_internal(p);
        }

    private:
        static size_t hash_internal(const SPass& p)
        {
            size_t seed = 0;
            static auto hash_combine = [](auto val, size_t& seed)
            {
                seed ^= xr_hash<decltype(val)>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            // Hash the constituent resource pointers
            hash_combine(p.state._get(), seed);
            hash_combine(p.ps._get(), seed);
            hash_combine(p.vs._get(), seed);
#if defined(USE_DX10) || defined(USE_DX11)
            hash_combine(p.gs._get(), seed);
#endif
#ifdef USE_DX11
            hash_combine(p.hs._get(), seed);
            hash_combine(p.ds._get(), seed);
            hash_combine(p.cs._get(), seed);
#endif
            hash_combine(p.constants._get(), seed);
            hash_combine(p.T._get(), seed);
            hash_combine(p.C._get(), seed);
#ifdef _EDITOR
            hash_combine(p.M._get(), seed);
#endif
            return seed;
        }
    };
	xr_unordered_flat_set<SPass*, SPass_hash, SPass_equal> v_passes;

    struct ShaderElement_equal
    {
        using is_transparent = void;

        bool operator()(const ShaderElement* lhs, const ShaderElement* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<ShaderElement*>(lhs)->equal(*const_cast<ShaderElement*>(rhs));
        }

        bool operator()(const ShaderElement* lhs, const ShaderElement& rhs) const
        {
            if (!lhs) return false;
            return const_cast<ShaderElement*>(lhs)->equal(const_cast<ShaderElement&>(rhs));
        }

        bool operator()(const ShaderElement& lhs, const ShaderElement* rhs) const {
            return (*this)(rhs, lhs);
        }
    };
    struct ShaderElement_hash
    {
        using is_transparent = void;

        size_t operator()(const ShaderElement* s) const
        {
            return s ? hash_internal(*s) : 0;
        }

        size_t operator()(const ShaderElement& s) const
        {
            return hash_internal(s);
        }

    private:
        static size_t hash_internal(const ShaderElement& s)
        {
            size_t seed = 0;
            for (const auto& pass : s.passes)
            {
                seed ^= xr_hash<SPass*>{}(pass._get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
    xr_unordered_flat_set<ShaderElement*, ShaderElement_hash, ShaderElement_equal> v_elements;

    struct Shader_equal
    {
        using is_transparent = void;

        bool operator()(const Shader* lhs, const Shader* rhs) const
        {
            if (lhs == rhs) return true;
            if (!lhs || !rhs) return false;
            return const_cast<Shader*>(lhs)->equal(*const_cast<Shader*>(rhs));
        }

        bool operator()(const Shader* lhs, const Shader& rhs) const
        {
            if (!lhs) return false;
            return const_cast<Shader*>(lhs)->equal(const_cast<Shader&>(rhs));
        }

        bool operator()(const Shader& lhs, const Shader* rhs) const
        {
            return (*this)(rhs, lhs);
        }
    };
    struct Shader_hash
    {
        using is_transparent = void;

        size_t operator()(const Shader* s) const
        {
            return s ? hash_internal(*s) : 0;
        }

        size_t operator()(const Shader& s) const
        {
            return hash_internal(s);
        }

    private:
        static size_t hash_internal(const Shader& s)
        {
            size_t seed = 0;
            for (u32 i = 0; i < 5; ++i)
            {
                seed ^= xr_hash<ShaderElement*>{}(s.E[i]._get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
    xr_unordered_flat_set<Shader*, Shader_hash, Shader_equal> v_shaders;
	
	xr_vector<ref_texture> m_necessary;
	// misc
    // separate locks for resource categories to reduce contention
    xrSRWLock shaderGuard;   // shaders, elements, passes, shader maps
    xrSRWLock textureGuard;  // textures and texture lists
    xrSRWLock matrixGuard;   // matrices and matrix lists
    xrSRWLock constantGuard; // constants, constant lists, constant tables
    xrSRWLock rtargetGuard;  // render targets
    xrSRWLock declGuard;     // declarations
    xrSRWLock stateGuard;    // simulator states
    xrSRWLock geomGuard;     // geometries
    xrSRWLock bufferGuard;   // constant buffers, input signatures (DX10/11)

public:
	CTextureDescrMngr m_textures_description;
	xr_vector<std::pair<shared_str, R_constant_setup*>> v_constant_setup;
	lua_State* LSVM;
	BOOL bDeferredLoad;
private:
	void LS_Load();
	void LS_Unload();
public:
	// Miscelaneous
	void _ParseList(sh_list& dest, LPCSTR names);
	IBlender* _GetBlender(LPCSTR Name);
	IBlender* _FindBlender(LPCSTR Name);
	void _GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps);
	void _DumpMemoryUsage();
	//.	BOOL							_GetDetailTexture	(LPCSTR Name, LPCSTR& T, R_constant_setup* &M);

	map_Blender& _GetBlenders() { return m_blenders; }

	// Debug
	void DBG_VerifyGeoms();
	void DBG_VerifyTextures();

	// Editor cooperation
	void ED_UpdateBlender(LPCSTR Name, IBlender* data);
#ifdef _EDITOR
	void							ED_UpdateTextures	(AStringVec* names);
#endif

	// Low level resource creation
	CTexture* _CreateTexture(LPCSTR Name);
	void _DeleteTexture(const CTexture* T);

	CMatrix* _CreateMatrix(LPCSTR Name);
	void _DeleteMatrix(const CMatrix* M);

	CConstant* _CreateConstant(LPCSTR Name);
	void _DeleteConstant(const CConstant* C);

	R_constant_table* _CreateConstantTable(R_constant_table& C);
	void _DeleteConstantTable(const R_constant_table* C);

#if defined(USE_DX10) || defined(USE_DX11)
	dx10ConstantBuffer*				_CreateConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable);
	void							_DeleteConstantBuffer(const dx10ConstantBuffer* pBuffer);

	SInputSignature*				_CreateInputSignature(ID3DBlob* pBlob);
	void							_DeleteInputSignature(const SInputSignature* pSignature);
#endif	//	USE_DX10

#ifdef USE_DX11
	CRT*							_CreateRT			(LPCSTR Name, u32 w, u32 h,	D3DFORMAT f, u32 SampleCount = 1, bool useUAV=false );
#else
	CRT* _CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1);
#endif
	void _DeleteRT(const CRT* RT);

	//	DX10 cut CRTC*							_CreateRTC			(LPCSTR Name, u32 size,	D3DFORMAT f);
	//	DX10 cut void							_DeleteRTC			(const CRTC*	RT	);
#if defined(USE_DX10) || defined(USE_DX11)
	SGS*							_CreateGS			(LPCSTR Name);
	void							_DeleteGS			(const SGS*	GS	);
#endif	//	USE_DX10

#ifdef USE_DX11
	SHS*							_CreateHS			(LPCSTR Name);
	void							_DeleteHS			(const SHS*	HS	);

	SDS*							_CreateDS			(LPCSTR Name);
	void							_DeleteDS			(const SDS*	DS	);

    SCS*							_CreateCS			(LPCSTR Name);
	void							_DeleteCS			(const SCS*	CS	);
#endif	//	USE_DX10

	SPS* _CreatePS(LPCSTR Name);
	void _DeletePS(const SPS* PS);

	SVS* _CreateVS(LPCSTR Name);
	void _DeleteVS(const SVS* VS);

	SPass* _CreatePass(const SPass& proto);
	void _DeletePass(const SPass* P);

	// Shader compiling / optimizing
	SState* _CreateState(SimulatorStates& Code);
	void _DeleteState(const SState* SB);

	SDeclaration* _CreateDecl(D3DVERTEXELEMENT9* dcl);
	void _DeleteDecl(const SDeclaration* dcl);

	STextureList* _CreateTextureList(STextureList& L);
	void _DeleteTextureList(const STextureList* L);

	SMatrixList* _CreateMatrixList(SMatrixList& L);
	void _DeleteMatrixList(const SMatrixList* L);

	Shader* _CreateShader(Shader* InShader);

	SConstantList* _CreateConstantList(SConstantList& L);
	void _DeleteConstantList(const SConstantList* L);

	ShaderElement* _CreateElement(ShaderElement& L);
	void _DeleteElement(const ShaderElement* L);

	Shader* _cpp_Create(LPCSTR s_shader, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
	Shader* _cpp_Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0,
	                    LPCSTR s_matrices = 0);
	Shader* _lua_Create(LPCSTR s_shader, LPCSTR s_textures);
	BOOL _lua_HasShader(LPCSTR s_shader);

	CResourceManager() : bDeferredLoad(TRUE)
	{
	}

	~CResourceManager();

	void OnDeviceCreate(IReader* F);
	void OnDeviceCreate(LPCSTR name);
	void OnDeviceDestroy(BOOL bKeepTextures);

	void reset_begin();
	void reset_end();

	// Creation/Destroying
	Shader* Create(LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
	Shader* Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0,
	               LPCSTR s_matrices = 0);
	void Delete(const Shader* S);

	void RegisterConstantSetup(LPCSTR name, R_constant_setup* s)
	{
		v_constant_setup.push_back(mk_pair(shared_str(name), s));
	}

	SGeometry* CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
	SGeometry* CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
	void DeleteGeom(const SGeometry* VS);
	void DeferredLoad(BOOL E) { bDeferredLoad = E; }
	void DeferredUpload();
	void DeferredUnload();
	void Evict();
	void StoreNecessaryTextures();
	void DestroyNecessaryTextures();
	void Dump(bool bBrief);

private:
#ifdef USE_DX11
	map_DS	m_ds;
	map_HS	m_hs;
	map_CS	m_cs;

	template<typename T>
	T& GetShaderMap();

	template<typename T>
	T* CreateShader(const char* name);

	template<typename T>
	void DestroyShader(const T* sh);

#endif	//	USE_DX10
};

#endif //ResourceManagerH
