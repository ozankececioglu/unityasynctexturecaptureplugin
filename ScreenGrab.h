#ifdef _MSC_VER
#pragma once
#endif

#include <d3d11_1.h>
#include <ocidl.h>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <stdint.h>
#pragma warning(pop)

#include <functional>

struct TextureInfo
{
	std::string * filePath;
	std::unique_ptr<uint8_t[]> * pixels;
	unsigned width;
	unsigned height;

	TextureInfo()
	{
		pixels = NULL;
		width = 0;
		height = 0;
		filePath = NULL;
	}
};

TextureInfo GetTextureData( _In_ ID3D11DeviceContext* pContext,
						   _In_ ID3D11Resource* pSource);
