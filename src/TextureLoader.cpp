#include "TextureLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

#include <Objbase.h>

#ifdef ERROR
#undef ERROR
#endif

#include "Overlay.h"
#include "REX/REX.h"
#include "directxtk12/DDSTextureLoader.h"
#include "directxtk12/ResourceUploadBatch.h"
#include "directxtk12/WICTextureLoader.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace
{
	using Microsoft::WRL::ComPtr;

	std::wstring Utf8ToWide(const std::string& a_path)
	{
		if (a_path.empty())
		{
			return {};
		}

		const auto required = ::MultiByteToWideChar(CP_UTF8, 0, a_path.c_str(), -1, nullptr, 0);
		if (required <= 0)
		{
			return {};
		}

		std::wstring widePath(static_cast<std::size_t>(required), L'\0');
		if (::MultiByteToWideChar(
				CP_UTF8, 0, a_path.c_str(), -1, widePath.data(), static_cast<int>(widePath.size())) <= 0)
		{
			return {};
		}

		if (!widePath.empty() && widePath.back() == L'\0')
		{
			widePath.pop_back();
		}

		return widePath;
	}

	bool EnsureWicFactoryAvailable()
	{
		const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
	}

	bool CommitUpload(DirectX::ResourceUploadBatch& a_uploadBatch, ID3D12CommandQueue* a_commandQueue)
	{
		try
		{
			auto uploadFinished = a_uploadBatch.End(a_commandQueue);
			uploadFinished.get();
			return true;
		}
		catch (const std::exception& e)
		{
			REX::ERROR("TextureLoader: GPU upload failed: {}", e.what());
			return false;
		}
		catch (...)
		{
			REX::ERROR("TextureLoader: GPU upload failed with an unknown exception");
			return false;
		}
	}

	bool CreateSrvForTexture(
		ID3D12Device* a_device,
		ComPtr<ID3D12Resource> a_resource,
		TextureLoader::TextureEntry& a_entry)
	{
		if (!a_device || !a_resource)
		{
			return false;
		}

		if (!Overlay::AllocateShaderVisibleSrv(&a_entry.cpuHandle, &a_entry.gpuHandle))
		{
			REX::ERROR("TextureLoader: failed to allocate a shader-visible SRV descriptor");
			return false;
		}

		a_device->CreateShaderResourceView(a_resource.Get(), nullptr, a_entry.cpuHandle);
		a_entry.resource = std::move(a_resource);
		return true;
	}

}

void TextureLoader::DisposeTexture(const std::string& a_path)
{
	std::scoped_lock lock(locker);

	if (IsSvgPath(a_path))
	{
		for (auto it = textures.begin(); it != textures.end();)
		{
			const bool matchesSource = (it->first == a_path) ||
				(it->first.size() > a_path.size() &&
				 it->first.compare(0, a_path.size(), a_path) == 0 &&
				 it->first[a_path.size()] == '-');
			if (!matchesSource)
			{
				++it;
				continue;
			}

			if (it->second.cpuHandle.ptr != 0 || it->second.gpuHandle.ptr != 0)
			{
				Overlay::FreeShaderVisibleSrv(it->second.cpuHandle, it->second.gpuHandle);
			}
			it = textures.erase(it);
		}
		return;
	}

	const auto found = textures.find(a_path);
	if (found == textures.end())
	{
		return;
	}

	if (found->second.cpuHandle.ptr != 0 || found->second.gpuHandle.ptr != 0)
	{
		Overlay::FreeShaderVisibleSrv(found->second.cpuHandle, found->second.gpuHandle);
	}
	textures.erase(found);
}

ImTextureID TextureLoader::GetTexture(const std::string& a_path, ImVec2 a_size)
{
	auto textureKey = MakeTextureKey(a_path, a_size);

	{
		std::scoped_lock lock(locker);
		const auto found = textures.find(textureKey);
		if (found != textures.end())
		{
			return static_cast<ImTextureID>(found->second.gpuHandle.ptr);
		}
	}

	const auto textureId = LoadTextureAny(a_path, a_size);
	if (textureId == ImTextureID_Invalid)
	{
		return ImTextureID_Invalid;
	}
	return textureId;
}

ImTextureID TextureLoader::LoadTextureAny(const std::string& a_path, ImVec2 a_size)
{
	if (EndsWithInsensitive(a_path, ".dds"))
	{
		return LoadDDS(a_path);
	}
	if (IsSvgPath(a_path))
	{
		return LoadSVG(a_path, a_size);
	}
	return LoadWIC(a_path);
}

ImTextureID TextureLoader::LoadDDS(const std::string& a_path)
{
	if (!std::filesystem::exists(a_path))
	{
		return ImTextureID_Invalid;
	}

	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	if (!TryGetRuntime(&device, &commandQueue))
	{
		return ImTextureID_Invalid;
	}

	const auto widePath = Utf8ToWide(a_path);
	if (widePath.empty())
	{
		REX::ERROR("TextureLoader: failed to convert UTF-8 path to wide string: {}", a_path);
		return ImTextureID_Invalid;
	}

	ComPtr<ID3D12Resource> texture;
	DirectX::ResourceUploadBatch uploadBatch(device);
	uploadBatch.Begin();

	const HRESULT hr = DirectX::CreateDDSTextureFromFile(
		device,
		uploadBatch,
		widePath.c_str(),
		texture.GetAddressOf());
	if (FAILED(hr))
	{
		REX::ERROR("TextureLoader: failed to decode DDS {} hr={:#x}", a_path, static_cast<std::uint32_t>(hr));
		return ImTextureID_Invalid;
	}

	if (!CommitUpload(uploadBatch, commandQueue))
	{
		return ImTextureID_Invalid;
	}

	TextureEntry entry;
	if (!CreateSrvForTexture(device, std::move(texture), entry))
	{
		return ImTextureID_Invalid;
	}

	std::scoped_lock lock(locker);
	const auto cpuHandle = entry.cpuHandle;
	const auto gpuHandle = entry.gpuHandle;
	const auto [it, inserted] = textures.emplace(a_path, std::move(entry));
	if (!inserted)
	{
		Overlay::FreeShaderVisibleSrv(cpuHandle, gpuHandle);
	}
	return static_cast<ImTextureID>(it->second.gpuHandle.ptr);
}

ImTextureID TextureLoader::LoadWIC(const std::string& a_path)
{
	if (!EnsureWicFactoryAvailable())
	{
		REX::ERROR("TextureLoader: COM initialization failed before WIC load");
		return ImTextureID_Invalid;
	}

	if (!std::filesystem::exists(a_path))
	{
		return ImTextureID_Invalid;
	}

	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	if (!TryGetRuntime(&device, &commandQueue))
	{
		return ImTextureID_Invalid;
	}

	const auto widePath = Utf8ToWide(a_path);
	if (widePath.empty())
	{
		REX::ERROR("TextureLoader: failed to convert UTF-8 path to wide string: {}", a_path);
		return ImTextureID_Invalid;
	}

	ComPtr<ID3D12Resource> texture;
	DirectX::ResourceUploadBatch uploadBatch(device);
	uploadBatch.Begin();

	const HRESULT hr = DirectX::CreateWICTextureFromFile(
		device,
		uploadBatch,
		widePath.c_str(),
		texture.GetAddressOf());
	if (FAILED(hr))
	{
		REX::ERROR("TextureLoader: failed to decode image {} hr={:#x}", a_path, static_cast<std::uint32_t>(hr));
		return ImTextureID_Invalid;
	}

	if (!CommitUpload(uploadBatch, commandQueue))
	{
		return ImTextureID_Invalid;
	}

	TextureEntry entry;
	if (!CreateSrvForTexture(device, std::move(texture), entry))
	{
		return ImTextureID_Invalid;
	}

	std::scoped_lock lock(locker);
	const auto cpuHandle = entry.cpuHandle;
	const auto gpuHandle = entry.gpuHandle;
	const auto [it, inserted] = textures.emplace(a_path, std::move(entry));
	if (!inserted)
	{
		Overlay::FreeShaderVisibleSrv(cpuHandle, gpuHandle);
	}
	return static_cast<ImTextureID>(it->second.gpuHandle.ptr);
}

ImTextureID TextureLoader::LoadSVG(const std::string& a_path, ImVec2 a_size)
{
	if (a_size.x <= 0.0f || a_size.y <= 0.0f)
	{
		REX::WARN("TextureLoader: SVG load requires a non-zero target size: {}", a_path);
		return ImTextureID_Invalid;
	}

	if (!std::filesystem::exists(a_path))
	{
		return ImTextureID_Invalid;
	}

	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	if (!TryGetRuntime(&device, &commandQueue))
	{
		return ImTextureID_Invalid;
	}

	NSVGimage* image = nsvgParseFromFile(a_path.c_str(), "px", 96.0f);
	if (!image)
	{
		REX::ERROR("TextureLoader: failed to parse SVG {}", a_path);
		return ImTextureID_Invalid;
	}

	const auto width = static_cast<int>(a_size.x);
	const auto height = static_cast<int>(a_size.y);
	std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);

	NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
	if (!rasterizer)
	{
		nsvgDelete(image);
		REX::ERROR("TextureLoader: failed to allocate SVG rasterizer for {}", a_path);
		return ImTextureID_Invalid;
	}

	const float scaleX = a_size.x / image->width;
	const float scaleY = a_size.y / image->height;
	const float scale = std::min(scaleX, scaleY);
	const float offsetX = (a_size.x - image->width * scale) * 0.5f;
	const float offsetY = (a_size.y - image->height * scale) * 0.5f;

	nsvgRasterize(
		rasterizer,
		image,
		offsetX,
		offsetY,
		scale,
		pixels.data(),
		width,
		height,
		width * 4);

	nsvgDeleteRasterizer(rasterizer);
	nsvgDelete(image);

	D3D12_RESOURCE_DESC textureDesc{};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Alignment = 0;
	textureDesc.Width = static_cast<UINT64>(width);
	textureDesc.Height = static_cast<UINT>(height);
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	ComPtr<ID3D12Resource> texture;
	const HRESULT hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(texture.GetAddressOf()));
	if (FAILED(hr))
	{
		REX::ERROR("TextureLoader: failed to create SVG texture {} hr={:#x}", a_path, static_cast<std::uint32_t>(hr));
		return ImTextureID_Invalid;
	}

	D3D12_SUBRESOURCE_DATA subresource{};
	subresource.pData = pixels.data();
	subresource.RowPitch = static_cast<LONG_PTR>(width * 4);
	subresource.SlicePitch = subresource.RowPitch * static_cast<LONG_PTR>(height);

	DirectX::ResourceUploadBatch uploadBatch(device);
	uploadBatch.Begin();
	uploadBatch.Upload(texture.Get(), 0, &subresource, 1);
	uploadBatch.Transition(
		texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	if (!CommitUpload(uploadBatch, commandQueue))
	{
		return ImTextureID_Invalid;
	}

	TextureEntry entry;
	if (!CreateSrvForTexture(device, std::move(texture), entry))
	{
		return ImTextureID_Invalid;
	}

	const auto textureKey = std::format("{}-{}-{}", a_path, width, height);
	std::scoped_lock lock(locker);
	const auto cpuHandle = entry.cpuHandle;
	const auto gpuHandle = entry.gpuHandle;
	const auto [it, inserted] = textures.emplace(textureKey, std::move(entry));
	if (!inserted)
	{
		Overlay::FreeShaderVisibleSrv(cpuHandle, gpuHandle);
	}
	return static_cast<ImTextureID>(it->second.gpuHandle.ptr);
}

bool TextureLoader::TryGetRuntime(ID3D12Device** a_outDevice, ID3D12CommandQueue** a_outCommandQueue)
{
	return Overlay::TryGetTextureLoaderContext(a_outDevice, a_outCommandQueue);
}

std::string TextureLoader::MakeTextureKey(const std::string& a_path, ImVec2& a_size)
{
	if (!IsSvgPath(a_path))
	{
		return a_path;
	}

	constexpr float snapFactor = 8.0f;
	a_size.x = std::ceil(a_size.x / snapFactor) * snapFactor;
	a_size.y = std::ceil(a_size.y / snapFactor) * snapFactor;
	return std::format("{}-{}-{}", a_path, static_cast<int>(a_size.x), static_cast<int>(a_size.y));
}

bool TextureLoader::IsSvgPath(const std::string& a_path)
{
	return EndsWithInsensitive(a_path, ".svg");
}

bool TextureLoader::EndsWithInsensitive(const std::string& a_value, const char* a_suffix)
{
	const std::string_view value(a_value);
	const std::string_view suffix(a_suffix);
	if (suffix.size() > value.size())
	{
		return false;
	}

	return std::equal(
		suffix.rbegin(),
		suffix.rend(),
		value.rbegin(),
		[](char a_left, char a_right) {
			return std::tolower(static_cast<unsigned char>(a_left)) ==
				std::tolower(static_cast<unsigned char>(a_right));
		});
}
