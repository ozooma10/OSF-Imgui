#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <map>
#include <mutex>
#include <string>

#include "imgui.h"

class TextureLoader
{
public:
	struct TextureEntry
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
	};

	static void DisposeTexture(const std::string& a_path);
	static ImTextureID GetTexture(const std::string& a_path, ImVec2 a_size = { 0.0f, 0.0f });

private:
	static ImTextureID LoadTextureAny(const std::string& a_path, ImVec2 a_size);
	static ImTextureID LoadDDS(const std::string& a_path);
	static ImTextureID LoadWIC(const std::string& a_path);
	static ImTextureID LoadSVG(const std::string& a_path, ImVec2 a_size);

	static bool TryGetRuntime(ID3D12Device** a_outDevice, ID3D12CommandQueue** a_outCommandQueue);
	static std::string MakeTextureKey(const std::string& a_path, ImVec2& a_size);
	static bool IsSvgPath(const std::string& a_path);
	static bool EndsWithInsensitive(const std::string& a_value, const char* a_suffix);

	static inline std::mutex locker;
	static inline std::map<std::string, TextureEntry> textures;
};
