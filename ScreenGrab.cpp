// Does not capture 1D textures or 3D textures (volume maps)
// Does not capture mipmap chains, only the top-most texture level is saved
// For 2D array textures and cubemaps, it captures only the first image in the array

#include <dxgiformat.h>
#include <assert.h>
#include <stdio.h>

// VS 2010's stdint.h conflicts with intsafe.h
#if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY != WINAPI_FAMILY_PHONE_APP) || (_WIN32_WINNT > _WIN32_WINNT_WIN8)
#pragma warning(push)
#pragma warning(disable : 4005)
#include <wincodec.h>
#include <intsafe.h>
#pragma warning(pop)
#endif

#include <wrl\client.h>
#include <algorithm>

#include "ScreenGrab.h"

using Microsoft::WRL::ComPtr;

// Macros
#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
	((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) |       \
	((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24 ))
#endif /* defined(MAKEFOURCC) */

// DDS file structure definitions
// See DDS.h in the 'Texconv' sample and the 'DirectXTex' library
#pragma pack(push,1)
#define DDS_MAGIC 0x20534444 // "DDS "

#define DDS_FOURCC      0x00000004  // DDPF_FOURCC
#define DDS_RGB         0x00000040  // DDPF_RGB
#define DDS_RGBA        0x00000041  // DDPF_RGB | DDPF_ALPHAPIXELS
#define DDS_LUMINANCE   0x00020000  // DDPF_LUMINANCE
#define DDS_LUMINANCEA  0x00020001  // DDPF_LUMINANCE | DDPF_ALPHAPIXELS
#define DDS_ALPHA       0x00000002  // DDPF_ALPHA
#define DDS_BUMPDUDV    0x00080000  // DDPF_BUMPDUDV

#define DDS_HEADER_FLAGS_TEXTURE        0x00001007  // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT 
#define DDS_HEADER_FLAGS_MIPMAP         0x00020000  // DDSD_MIPMAPCOUNT
#define DDS_HEADER_FLAGS_PITCH          0x00000008  // DDSD_PITCH
#define DDS_HEADER_FLAGS_LINEARSIZE     0x00080000  // DDSD_LINEARSIZE

#define DDS_HEIGHT 0x00000002 // DDSD_HEIGHT
#define DDS_WIDTH  0x00000004 // DDSD_WIDTH

#define DDS_SURFACE_FLAGS_TEXTURE 0x00001000 // DDSCAPS_TEXTURE

struct DDS_PIXELFORMAT
{
	uint32_t    size;
	uint32_t    flags;
	uint32_t    fourCC;
	uint32_t    RGBBitCount;
	uint32_t    RBitMask;
	uint32_t    GBitMask;
	uint32_t    BBitMask;
	uint32_t    ABitMask;
};

typedef struct
{
	uint32_t        size;
	uint32_t        flags;
	uint32_t        height;
	uint32_t        width;
	uint32_t        pitchOrLinearSize;
	uint32_t        depth; // only if DDS_HEADER_FLAGS_VOLUME is set in flags
	uint32_t        mipMapCount;
	uint32_t        reserved1[11];
	DDS_PIXELFORMAT ddspf;
	uint32_t        caps;
	uint32_t        caps2;
	uint32_t        caps3;
	uint32_t        caps4;
	uint32_t        reserved2;
} DDS_HEADER;

typedef struct
{
	DXGI_FORMAT     dxgiFormat;
	uint32_t        resourceDimension;
	uint32_t        miscFlag; // see D3D11_RESOURCE_MISC_FLAG
	uint32_t        arraySize;
	uint32_t        reserved;
} DDS_HEADER_DXT10;

#pragma pack(pop)

static const DDS_PIXELFORMAT DDSPF_DXT1 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('D','X','T','1'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_DXT3 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('D','X','T','3'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_DXT5 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('D','X','T','5'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_BC4_UNORM =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('B','C','4','U'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_BC4_SNORM =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('B','C','4','S'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_BC5_UNORM =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('B','C','5','U'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_BC5_SNORM =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('B','C','5','S'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_R8G8_B8G8 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('R','G','B','G'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_G8R8_G8B8 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('G','R','G','B'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_YUY2 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('Y','U','Y','2'), 0, 0, 0, 0, 0 };

static const DDS_PIXELFORMAT DDSPF_A8R8G8B8 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGBA, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000 };

static const DDS_PIXELFORMAT DDSPF_X8R8G8B8 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGB,  0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000 };

static const DDS_PIXELFORMAT DDSPF_A8B8G8R8 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGBA, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 };

static const DDS_PIXELFORMAT DDSPF_G16R16 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGB,  0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000 };

static const DDS_PIXELFORMAT DDSPF_R5G6B5 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGB, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000 };

static const DDS_PIXELFORMAT DDSPF_A1R5G5B5 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGBA, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000 };

static const DDS_PIXELFORMAT DDSPF_A4R4G4B4 =
{ sizeof(DDS_PIXELFORMAT), DDS_RGBA, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000 };

static const DDS_PIXELFORMAT DDSPF_L8 =
{ sizeof(DDS_PIXELFORMAT), DDS_LUMINANCE, 0,  8, 0xff, 0x00, 0x00, 0x00 };

static const DDS_PIXELFORMAT DDSPF_L16 =
{ sizeof(DDS_PIXELFORMAT), DDS_LUMINANCE, 0, 16, 0xffff, 0x0000, 0x0000, 0x0000 };

static const DDS_PIXELFORMAT DDSPF_A8L8 =
{ sizeof(DDS_PIXELFORMAT), DDS_LUMINANCEA, 0, 16, 0x00ff, 0x0000, 0x0000, 0xff00 };

static const DDS_PIXELFORMAT DDSPF_A8 =
{ sizeof(DDS_PIXELFORMAT), DDS_ALPHA, 0, 8, 0x00, 0x00, 0x00, 0xff };

static const DDS_PIXELFORMAT DDSPF_V8U8 = 
{ sizeof(DDS_PIXELFORMAT), DDS_BUMPDUDV, 0, 16, 0x00ff, 0xff00, 0x0000, 0x0000 };

static const DDS_PIXELFORMAT DDSPF_Q8W8V8U8 = 
{ sizeof(DDS_PIXELFORMAT), DDS_BUMPDUDV, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 };

static const DDS_PIXELFORMAT DDSPF_V16U16 = 
{ sizeof(DDS_PIXELFORMAT), DDS_BUMPDUDV, 0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000 };

// DXGI_FORMAT_R10G10B10A2_UNORM should be written using DX10 extension to avoid D3DX 10:10:10:2 reversal issue

// This indicates the DDS_HEADER_DXT10 extension is present (the format is in dxgiFormat)
static const DDS_PIXELFORMAT DDSPF_DX10 =
{ sizeof(DDS_PIXELFORMAT), DDS_FOURCC, MAKEFOURCC('D','X','1','0'), 0, 0, 0, 0, 0 };

// Return the BPP for a particular format
static size_t BitsPerPixel( _In_ DXGI_FORMAT fmt )
{
	switch( fmt )
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;

	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;

	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;

	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
		return 32;

	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		return 24;

	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_A8P8:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;

	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;

	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
		return 8;

	case DXGI_FORMAT_R1_UNORM:
		return 1;

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;

	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return 8;

	default:
		return 0;
	}
}
// Get surface information for a particular format
static void GetSurfaceInfo( _In_ size_t width,
						   _In_ size_t height,
						   _In_ DXGI_FORMAT fmt,
						   _Out_opt_ size_t* outNumBytes,
						   _Out_opt_ size_t* outRowBytes,
						   _Out_opt_ size_t* outNumRows )
{
	size_t numBytes = 0;
	size_t rowBytes = 0;
	size_t numRows = 0;

	bool bc = false;
	bool packed = false;
	bool planar = false;
	size_t bpe = 0;
	switch (fmt)
	{
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		bc=true;
		bpe = 8;
		break;

	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		bc = true;
		bpe = 16;
		break;

	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_YUY2:
		packed = true;
		bpe = 4;
		break;

	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		packed = true;
		bpe = 8;
		break;

	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
		planar = true;
		bpe = 2;
		break;

	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		planar = true;
		bpe = 4;
		break;
	}

	if (bc)
	{
		size_t numBlocksWide = 0;
		if (width > 0)
		{
			numBlocksWide = std::max<size_t>( 1, (width + 3) / 4 );
		}
		size_t numBlocksHigh = 0;
		if (height > 0)
		{
			numBlocksHigh = std::max<size_t>( 1, (height + 3) / 4 );
		}
		rowBytes = numBlocksWide * bpe;
		numRows = numBlocksHigh;
		numBytes = rowBytes * numBlocksHigh;
	}
	else if (packed)
	{
		rowBytes = ( ( width + 1 ) >> 1 ) * bpe;
		numRows = height;
		numBytes = rowBytes * height;
	}
	else if ( fmt == DXGI_FORMAT_NV11 )
	{
		rowBytes = ( ( width + 3 ) >> 2 ) * 4;
		numRows = height * 2; // Direct3D makes this simplifying assumption, although it is larger than the 4:1:1 data
		numBytes = rowBytes * numRows;
	}
	else if (planar)
	{
		rowBytes = ( ( width + 1 ) >> 1 ) * bpe;
		numBytes = ( rowBytes * height ) + ( ( rowBytes * height + 1 ) >> 1 );
		numRows = height + ( ( height + 1 ) >> 1 );
	}
	else
	{
		size_t bpp = BitsPerPixel( fmt );
		rowBytes = ( width * bpp + 7 ) / 8; // round up to nearest byte
		numRows = height;
		numBytes = rowBytes * height;
	}

	if (outNumBytes)
	{
		*outNumBytes = numBytes;
	}
	if (outRowBytes)
	{
		*outRowBytes = rowBytes;
	}
	if (outNumRows)
	{
		*outNumRows = numRows;
	}
}


//--------------------------------------------------------------------------------------
static DXGI_FORMAT EnsureNotTypeless( DXGI_FORMAT fmt )
{
	// Assumes UNORM or FLOAT; doesn't use UINT or SINT
	switch( fmt )
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case DXGI_FORMAT_R32G32B32_TYPELESS:    return DXGI_FORMAT_R32G32B32_FLOAT;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_UNORM;
	case DXGI_FORMAT_R32G32_TYPELESS:       return DXGI_FORMAT_R32G32_FLOAT;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_UNORM;
	case DXGI_FORMAT_R32_TYPELESS:          return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_R8G8_TYPELESS:         return DXGI_FORMAT_R8G8_UNORM;
	case DXGI_FORMAT_R16_TYPELESS:          return DXGI_FORMAT_R16_UNORM;
	case DXGI_FORMAT_R8_TYPELESS:           return DXGI_FORMAT_R8_UNORM;
	case DXGI_FORMAT_BC1_TYPELESS:          return DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS:          return DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS:          return DXGI_FORMAT_BC3_UNORM;
	case DXGI_FORMAT_BC4_TYPELESS:          return DXGI_FORMAT_BC4_UNORM;
	case DXGI_FORMAT_BC5_TYPELESS:          return DXGI_FORMAT_BC5_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:     return DXGI_FORMAT_B8G8R8X8_UNORM;
	case DXGI_FORMAT_BC7_TYPELESS:          return DXGI_FORMAT_BC7_UNORM;
	default:                                return fmt;
	}
}


//--------------------------------------------------------------------------------------
static HRESULT CaptureTexture( _In_ ID3D11DeviceContext* pContext,
							  _In_ ID3D11Resource* pSource,
							  _Inout_ D3D11_TEXTURE2D_DESC& desc,
							  _Inout_ ComPtr<ID3D11Texture2D>& pStaging )
{
	if ( !pContext || !pSource )
		return E_INVALIDARG;

	D3D11_RESOURCE_DIMENSION resType = D3D11_RESOURCE_DIMENSION_UNKNOWN;
	pSource->GetType( &resType );

	if ( resType != D3D11_RESOURCE_DIMENSION_TEXTURE2D )
		return HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED );

	ComPtr<ID3D11Texture2D> pTexture;
	HRESULT hr = pSource->QueryInterface( __uuidof(ID3D11Texture2D), reinterpret_cast<void**>( pTexture.GetAddressOf() ) );
	if ( FAILED(hr) )
		return hr;

	assert( pTexture );

	pTexture->GetDesc( &desc );

	ComPtr<ID3D11Device> d3dDevice;
	pContext->GetDevice( d3dDevice.GetAddressOf() );

	if ( desc.SampleDesc.Count > 1 )
	{
		// MSAA content must be resolved before being copied to a staging texture
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		ComPtr<ID3D11Texture2D> pTemp;
		hr = d3dDevice->CreateTexture2D( &desc, 0, pTemp.GetAddressOf() );
		if ( FAILED(hr) )
			return hr;

		assert( pTemp );

		DXGI_FORMAT fmt = EnsureNotTypeless( desc.Format );

		UINT support = 0;
		hr = d3dDevice->CheckFormatSupport( fmt, &support );
		if ( FAILED(hr) )
			return hr;

		if ( !(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) )
			return E_FAIL;

		for( UINT item = 0; item < desc.ArraySize; ++item )
		{
			for( UINT level = 0; level < desc.MipLevels; ++level )
			{
				UINT index = D3D11CalcSubresource( level, item, desc.MipLevels );
				pContext->ResolveSubresource( pTemp.Get(), index, pSource, index, fmt );
			}
		}

		desc.BindFlags = 0;
		desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.Usage = D3D11_USAGE_STAGING;

		hr = d3dDevice->CreateTexture2D( &desc, 0, pStaging.GetAddressOf() );
		if ( FAILED(hr) )
			return hr;

		assert( pStaging );

		pContext->CopyResource( pStaging.Get(), pTemp.Get() );
	}
	else if ( (desc.Usage == D3D11_USAGE_STAGING) && (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) )
	{
		// Handle case where the source is already a staging texture we can use directly
		pStaging = pTexture;
	}
	else
	{
		// Otherwise, create a staging texture from the non-MSAA source
		desc.BindFlags = 0;
		desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.Usage = D3D11_USAGE_STAGING;

		hr = d3dDevice->CreateTexture2D( &desc, 0, pStaging.GetAddressOf() );
		if ( FAILED(hr) )
			return hr;

		assert( pStaging );

		pContext->CopyResource( pStaging.Get(), pSource );
	}

	return S_OK;
}


//--------------------------------------------------------------------------------------
TextureInfo GetTextureData( _In_ ID3D11DeviceContext* pContext,
						   _In_ ID3D11Resource* pSource)
{
	D3D11_TEXTURE2D_DESC desc = { 0 };
	ComPtr<ID3D11Texture2D> pStaging;
	HRESULT hr = CaptureTexture( pContext, pSource, desc, pStaging );
	if ( FAILED(hr) )
		return TextureInfo();

	size_t rowPitch, slicePitch, rowCount;
	GetSurfaceInfo( desc.Width, desc.Height, desc.Format, &slicePitch, &rowPitch, &rowCount );


	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = pContext->Map( pStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped );
	if ( FAILED(hr) )
		return TextureInfo();

	auto sptr = reinterpret_cast<const uint8_t*>( mapped.pData );
	if ( !sptr )
	{
		pContext->Unmap( pStaging.Get(), 0 );
		return TextureInfo();
	}

	std::unique_ptr<uint8_t[]> * pixels = new std::unique_ptr<uint8_t[]>( new (std::nothrow) uint8_t[ slicePitch ] );
	if (!pixels)
		return TextureInfo();
	uint8_t* dptr = pixels->get() + (rowCount - 1) * rowPitch;

	size_t msize = std::min<size_t>( rowPitch, mapped.RowPitch );
	for( size_t h = 0; h < rowCount; ++h )
	{
		memcpy_s( dptr, rowPitch, sptr, msize );
		sptr += mapped.RowPitch;
		dptr -= rowPitch;
	}

	pContext->Unmap( pStaging.Get(), 0 );

	auto result = TextureInfo();
	result.width = desc.Width;
	result.height = desc.Height;
	result.pixels = pixels;

	return result;
}

