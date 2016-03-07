// Example low level rendering Unity plugin
#include "Unity/IUnityInterface.h"
#include "Unity/IUnityGraphics.h"
#include "ScreenGrab.h"
#include "lodepng.h"
#include "Unity/IUnityGraphicsD3D11.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <d3d11.h>
#include <time.h>
#include <queue>

static FILE * logFile = NULL;
static void Log(std::string message)
{
	if (logFile == NULL) {
		logFile = fopen("renderingPlugin.log", "w");
	}
	fprintf(logFile, "%s\n", message.c_str());
}

static bool writeThreadEnabled = true;
static HANDLE writeThreadHandle;
static std::queue<TextureInfo> writeThreadQueue = std::queue<TextureInfo>();
static DWORD WINAPI WriteThreadLoop(LPVOID lpParameter)
{
	while (writeThreadEnabled) {
		while (!writeThreadQueue.empty()) {
			auto current = writeThreadQueue.front();
			writeThreadQueue.pop();
			try {
				if (current.pixels != NULL) {
					while (writeThreadEnabled && lodepng::encode(*current.filePath, current.pixels->get(), current.width, current.height, LCT_RGBA) != 0);
				}
				delete current.pixels;
			} catch (...) { }
		}
		Sleep(50);
	}
	CloseHandle(writeThreadHandle);
	return 0;
}

static void* g_TexturePointer = NULL;
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTexture(void* texturePtr)
{
	g_TexturePointer = texturePtr;
}
static std::string filePath;
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetFilePath(const char* path)
{
	filePath = path;
}

// UnitySetInterfaces
static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;
static ID3D11Device* g_D3D11Device = NULL;
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	UnityGfxRenderer currentDeviceType = s_DeviceType;

	switch (eventType)
	{
	case kUnityGfxDeviceEventInitialize:
		{
			s_DeviceType = s_Graphics->GetRenderer();
			currentDeviceType = s_DeviceType;
			break;
		}

	case kUnityGfxDeviceEventShutdown:
		{
			s_DeviceType = kUnityGfxRendererNull;
			g_TexturePointer = NULL;
			break;
		}

	case kUnityGfxDeviceEventBeforeReset:
		{
			break;
		}

	case kUnityGfxDeviceEventAfterReset:
		{
			break;
		}
	};

	if (currentDeviceType == kUnityGfxRendererD3D11 && eventType == kUnityGfxDeviceEventInitialize) {
		IUnityGraphicsD3D11* d3d11 = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();
		g_D3D11Device = d3d11->GetDevice();
	}
}
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	DWORD myThreadID;
	writeThreadHandle = CreateThread(0, 0, WriteThreadLoop, NULL, 0, &myThreadID);

	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	if (logFile != NULL) {
		fclose(logFile);
	}
	writeThreadEnabled = false;

	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	if (s_DeviceType != kUnityGfxRendererD3D11)
		return;

	if (g_TexturePointer && !filePath.empty()) {
		ID3D11Texture2D* d3dtex = (ID3D11Texture2D*)g_TexturePointer;
		ID3D11DeviceContext* ctx = NULL;
		g_D3D11Device->GetImmediateContext (&ctx);
		auto result = GetTextureData(ctx, d3dtex);
		ctx->Release();

		if (result.pixels != NULL) {
			result.filePath = new std::string(filePath);
			writeThreadQueue.push(result);
		}
	}
}
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

