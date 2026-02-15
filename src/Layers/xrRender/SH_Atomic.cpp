#include "stdafx.h"
#pragma hdrstop

#include "sh_atomic.h"
#include "ResourceManager.h"

#include "dxRenderDeviceRender.h"

// Atomic
//SVS::~SVS								()			{	_RELEASE(vs);		dxRenderDeviceRender::Instance().Resources->_DeleteVS			(this);	}
//SPS::~SPS								()			{	_RELEASE(ps);		dxRenderDeviceRender::Instance().Resources->_DeletePS			(this);	}
//SState::~SState							()			{	_RELEASE(state);	dxRenderDeviceRender::Instance().Resources->_DeleteState		(this);	}
//SDeclaration::~SDeclaration				()			{	_RELEASE(dcl);		dxRenderDeviceRender::Instance().Resources->_DeleteDecl		(this);	}

///////////////////////////////////////////////////////////////////////
//	SVS
SVS::SVS() :
	vs(0)
#if defined(USE_DX10) || defined(USE_DX11)
//	,signature(0)
#endif	//	USE_DX10
{
	;
}


SVS::~SVS()
{
	DEV->_DeleteVS(this);
#if defined(USE_DX10) || defined(USE_DX11)
	//_RELEASE(signature);
	//	Now it is release automatically
#endif	//	USE_DX10
	_RELEASE(vs);
}


///////////////////////////////////////////////////////////////////////
//	SPS
SPS::~SPS()
{
	DEV->_DeletePS(this);
    _RELEASE(ps);
}

#if defined(USE_DX10) || defined(USE_DX11)
///////////////////////////////////////////////////////////////////////
//	SGS
SGS::~SGS()
{
	DEV->_DeleteGS(this);
    _RELEASE(gs);
}

#	ifdef USE_DX11
SHS::~SHS()
{
	DEV->_DeleteHS(this);
    _RELEASE(sh);
}

SDS::~SDS()
{
	DEV->_DeleteDS(this);
    _RELEASE(sh);
}

SCS::~SCS()
{
	DEV->_DeleteCS(this);
    _RELEASE(sh);
}
#	endif

///////////////////////////////////////////////////////////////////////
//	SInputSignature
SInputSignature::SInputSignature(ID3DBlob* pBlob)
{
	VERIFY(pBlob);
	signature = pBlob;
	signature->AddRef();
};

SInputSignature::~SInputSignature()
{
	DEV->_DeleteInputSignature(this);
    _RELEASE(signature);
}
#endif	//	USE_DX10

///////////////////////////////////////////////////////////////////////
//	SState
SState::~SState()
{
	DEV->_DeleteState(this);
    _RELEASE(state);
}

///////////////////////////////////////////////////////////////////////
//	SDeclaration
SDeclaration::~SDeclaration()
{
	DEV->_DeleteDecl(this);
#if defined(USE_DX10) || defined(USE_DX11)
	xr_map<ID3DBlob*, ID3DInputLayout*>::iterator iLayout;
	iLayout = vs_to_layout.begin();
	for (; iLayout != vs_to_layout.end(); ++iLayout)
	{
		//	Release vertex layout
		_RELEASE(iLayout->second);
	}
#else	//	USE_DX10
	//	Release vertex layout
	_RELEASE(dcl);
#endif	//	USE_DX10
}
