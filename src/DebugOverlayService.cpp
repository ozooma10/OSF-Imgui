#include "DebugOverlayService.h"

#include "DebugPanelRegistry.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <windows.h>

#include <unordered_set>

#include "SFSE/API.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr UINT   kCreateSwapChainVtableIndex = 10;
	constexpr UINT   kCreateSwapChainForHwndVtableIndex = 15;
	constexpr UINT   kPresentVtableIndex = 8;
	constexpr UINT   kResizeBuffersVtableIndex = 13;
	constexpr UINT   kOverlayToggleVirtualKey = VK_F10;
	constexpr LPCWSTR kDummyWindowClassName = L"OSFMenuFrameworkDummyWindow";

	LRESULT CALLBACK DummyWindowProc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
	{
		return DefWindowProcW(a_hwnd, a_message, a_wParam, a_lParam);
	}

	bool EnsureDummyWindowClass()
	{
		static std::once_flag once;
		static bool           registered = false;

		std::call_once(once, []() {
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = DummyWindowProc;
			windowClass.hInstance = GetModuleHandleW(nullptr);
			windowClass.lpszClassName = kDummyWindowClassName;
			windowClass.style = CS_HREDRAW | CS_VREDRAW;

			registered = RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
		});

		return registered;
	}

	struct DummyDxgiContext
	{
		~DummyDxgiContext()
		{
			if (window) {
				DestroyWindow(window);
			}
		}

		HWND                   window{ nullptr };
		ComPtr<IDXGIFactory2>  factory;
		ComPtr<ID3D12Device>   device;
		ComPtr<ID3D12CommandQueue> commandQueue;
		ComPtr<IDXGISwapChain1> swapChain;
	};

	template <class T>
	ComPtr<T> QueryInterface(IUnknown* a_object)
	{
		ComPtr<T> result;
		if (a_object) {
			a_object->QueryInterface(IID_PPV_ARGS(result.ReleaseAndGetAddressOf()));
		}
		return result;
	}

	template <class TFunc>
	TFunc PatchVtableEntry(void** a_vtable, std::size_t a_index, TFunc a_detour)
	{
		DWORD previousProtect = 0;
		if (!VirtualProtect(&a_vtable[a_index], sizeof(void*), PAGE_EXECUTE_READWRITE, &previousProtect)) {
			return nullptr;
		}

		const auto original = reinterpret_cast<TFunc>(a_vtable[a_index]);
		a_vtable[a_index] = reinterpret_cast<void*>(a_detour);

		DWORD unused = 0;
		VirtualProtect(&a_vtable[a_index], sizeof(void*), previousProtect, &unused);
		FlushInstructionCache(GetCurrentProcess(), &a_vtable[a_index], sizeof(void*));

		return original;
	}

	template <class TInterface, class TFunc>
	void PatchInterfaceVtable(IUnknown* a_object, std::size_t a_index, TFunc a_detour, TFunc& a_original, std::unordered_set<void**>& a_seenVtables)
	{
		auto instance = QueryInterface<TInterface>(a_object);
		if (!instance) {
			return;
		}

		auto* vtable = *reinterpret_cast<void***>(instance.Get());
		if (!a_seenVtables.insert(vtable).second) {
			return;
		}

		const auto original = PatchVtableEntry(vtable, a_index, a_detour);
		if (!a_original) {
			a_original = original;
		}
	}

	bool CreateDummyDxgiContext(DummyDxgiContext& a_context, std::string& a_error)
	{
		if (!EnsureDummyWindowClass()) {
			a_error = "Failed to register the temporary DXGI hook window class.";
			return false;
		}

		a_context.window = CreateWindowExW(
			0,
			kDummyWindowClassName,
			L"OSF Menu Framework Dummy Window",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			32,
			32,
			nullptr,
			nullptr,
			GetModuleHandleW(nullptr),
			nullptr);
		if (!a_context.window) {
			a_error = "Failed to create the temporary DXGI hook window.";
			return false;
		}

		ComPtr<IDXGIFactory4> factory4;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory4.ReleaseAndGetAddressOf())))) {
			a_error = "CreateDXGIFactory1 failed while preparing the debug overlay hooks.";
			return false;
		}

		if (FAILED(factory4.As(&a_context.factory))) {
			a_error = "Failed to query IDXGIFactory2 from the temporary DXGI factory.";
			return false;
		}

		if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(a_context.device.ReleaseAndGetAddressOf())))) {
			a_error = "D3D12CreateDevice failed while preparing the debug overlay hooks.";
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		if (FAILED(a_context.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(a_context.commandQueue.ReleaseAndGetAddressOf())))) {
			a_error = "Failed to create a temporary D3D12 command queue for the overlay hooks.";
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = 2;
		swapChainDesc.Height = 2;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		if (FAILED(a_context.factory->CreateSwapChainForHwnd(a_context.commandQueue.Get(), a_context.window, &swapChainDesc, nullptr, nullptr, a_context.swapChain.ReleaseAndGetAddressOf()))) {
			a_error = "Failed to create a temporary DXGI swap chain for the overlay hooks.";
			return false;
		}

		return true;
	}

	std::string VersionToString(const REL::Version& a_version)
	{
		return a_version.string();
	}
}

DebugOverlayService& DebugOverlayService::GetSingleton()
{
	static DebugOverlayService singleton;
	return singleton;
}

bool DebugOverlayService::Install()
{
	std::scoped_lock lock(_lock);

	if (_hooksInstalled) {
		return true;
	}

	DummyDxgiContext dummyContext;
	std::string      error;
	if (!CreateDummyDxgiContext(dummyContext, error)) {
		SetLastError(std::move(error));
		return false;
	}

	std::unordered_set<void**> patchedFactoryCreateSwapChainVtables;
	std::unordered_set<void**> patchedFactoryCreateSwapChainForHwndVtables;
	std::unordered_set<void**> patchedSwapChainPresentVtables;
	std::unordered_set<void**> patchedSwapChainResizeBuffersVtables;

	PatchInterfaceVtable<IDXGIFactory>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory1>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory2>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory3>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory4>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory5>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory6>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);
	PatchInterfaceVtable<IDXGIFactory7>(dummyContext.factory.Get(), kCreateSwapChainVtableIndex, &HookCreateSwapChain, _originalCreateSwapChain, patchedFactoryCreateSwapChainVtables);

	PatchInterfaceVtable<IDXGIFactory2>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);
	PatchInterfaceVtable<IDXGIFactory3>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);
	PatchInterfaceVtable<IDXGIFactory4>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);
	PatchInterfaceVtable<IDXGIFactory5>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);
	PatchInterfaceVtable<IDXGIFactory6>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);
	PatchInterfaceVtable<IDXGIFactory7>(dummyContext.factory.Get(), kCreateSwapChainForHwndVtableIndex, &HookCreateSwapChainForHwnd, _originalCreateSwapChainForHwnd, patchedFactoryCreateSwapChainForHwndVtables);

	PatchInterfaceVtable<IDXGISwapChain>(dummyContext.swapChain.Get(), kPresentVtableIndex, &HookPresent, _originalPresent, patchedSwapChainPresentVtables);
	PatchInterfaceVtable<IDXGISwapChain1>(dummyContext.swapChain.Get(), kPresentVtableIndex, &HookPresent, _originalPresent, patchedSwapChainPresentVtables);
	PatchInterfaceVtable<IDXGISwapChain2>(dummyContext.swapChain.Get(), kPresentVtableIndex, &HookPresent, _originalPresent, patchedSwapChainPresentVtables);
	PatchInterfaceVtable<IDXGISwapChain3>(dummyContext.swapChain.Get(), kPresentVtableIndex, &HookPresent, _originalPresent, patchedSwapChainPresentVtables);
	PatchInterfaceVtable<IDXGISwapChain4>(dummyContext.swapChain.Get(), kPresentVtableIndex, &HookPresent, _originalPresent, patchedSwapChainPresentVtables);

	PatchInterfaceVtable<IDXGISwapChain>(dummyContext.swapChain.Get(), kResizeBuffersVtableIndex, &HookResizeBuffers, _originalResizeBuffers, patchedSwapChainResizeBuffersVtables);
	PatchInterfaceVtable<IDXGISwapChain1>(dummyContext.swapChain.Get(), kResizeBuffersVtableIndex, &HookResizeBuffers, _originalResizeBuffers, patchedSwapChainResizeBuffersVtables);
	PatchInterfaceVtable<IDXGISwapChain2>(dummyContext.swapChain.Get(), kResizeBuffersVtableIndex, &HookResizeBuffers, _originalResizeBuffers, patchedSwapChainResizeBuffersVtables);
	PatchInterfaceVtable<IDXGISwapChain3>(dummyContext.swapChain.Get(), kResizeBuffersVtableIndex, &HookResizeBuffers, _originalResizeBuffers, patchedSwapChainResizeBuffersVtables);
	PatchInterfaceVtable<IDXGISwapChain4>(dummyContext.swapChain.Get(), kResizeBuffersVtableIndex, &HookResizeBuffers, _originalResizeBuffers, patchedSwapChainResizeBuffersVtables);

	_hooksInstalled =
		_originalCreateSwapChain &&
		_originalCreateSwapChainForHwnd &&
		_originalPresent &&
		_originalResizeBuffers;

	if (!_hooksInstalled) {
		std::vector<std::string_view> missingHooks;
		if (!_originalCreateSwapChain) {
			missingHooks.push_back("IDXGIFactory::CreateSwapChain");
		}
		if (!_originalCreateSwapChainForHwnd) {
			missingHooks.push_back("IDXGIFactory2::CreateSwapChainForHwnd");
		}
		if (!_originalPresent) {
			missingHooks.push_back("IDXGISwapChain::Present");
		}
		if (!_originalResizeBuffers) {
			missingHooks.push_back("IDXGISwapChain::ResizeBuffers");
		}

		std::string message = "Failed to patch all required DXGI vtables for the debug overlay. Missing hooks: ";
		for (std::size_t index = 0; index < missingHooks.size(); ++index) {
			if (index > 0) {
				message += ", ";
			}
			message += missingHooks[index];
		}

		SetLastError(std::move(message));
		return false;
	}

	REX::INFO("OSF Menu Framework installed Dear ImGui DXGI hooks");
	return true;
}

void DebugOverlayService::RegisterBuiltInPanels()
{
	std::scoped_lock lock(_lock);

	if (_builtInPanelsRegistered) {
		return;
	}

	_builtInPanelsRegistered = true;

	RegisterDebugPanel("Overview", [this]() {
		const auto pluginVersion = VersionToString(SFSE::GetPluginVersion());
		const auto runtimeVersion = VersionToString(REX::FModule::GetExecutingModule().GetFileVersion());
		const auto pluginName = std::string(SFSE::GetPluginName());

		ImGui::Text("Plugin: %s", pluginName.c_str());
		ImGui::Text("Plugin version: %s", pluginVersion.c_str());
		ImGui::Text("Starfield runtime: %s", runtimeVersion.c_str());
		ImGui::Text("SFSE version: %u", SFSE::GetSFSEVersion());
		ImGui::Separator();
		ImGui::Text("Hooks installed: %s", _hooksInstalled ? "yes" : "no");
		ImGui::Text("ImGui initialized: %s", _imguiInitialized ? "yes" : "no");
		ImGui::Text("Overlay visible: %s", _overlayVisible ? "yes" : "no");
		ImGui::Text("Window handle: 0x%p", _window);
		ImGui::Text("Tracked back buffers: %zu", _frames.size());
		ImGui::Text("Command queue: 0x%p", _commandQueue.Get());
		ImGui::Text("Swap chain: 0x%p", _swapChain.Get());

		if (!_lastError.empty()) {
			ImGui::Separator();
			ImGui::TextWrapped("Last overlay error: %s", _lastError.c_str());
		}
	});
}

bool DebugOverlayService::IsHookInstalled() const
{
	std::scoped_lock lock(_lock);
	return _hooksInstalled;
}

bool DebugOverlayService::IsInitialized() const
{
	std::scoped_lock lock(_lock);
	return _imguiInitialized;
}

bool DebugOverlayService::IsVisible() const
{
	std::scoped_lock lock(_lock);
	return _overlayVisible;
}

HWND DebugOverlayService::GetWindowHandle() const
{
	std::scoped_lock lock(_lock);
	return _window;
}

std::size_t DebugOverlayService::GetBufferCount() const
{
	std::scoped_lock lock(_lock);
	return _frames.size();
}

std::string DebugOverlayService::GetLastError() const
{
	std::scoped_lock lock(_lock);
	return _lastError;
}

void DebugOverlayService::SetLastError(std::string a_message)
{
	_lastError = std::move(a_message);
	REX::ERROR("{}", _lastError);
}

void DebugOverlayService::ToggleVisibility()
{
	std::scoped_lock lock(_lock);

	_overlayVisible = !_overlayVisible;
	if (_imguiInitialized) {
		ImGui::GetIO().MouseDrawCursor = _overlayVisible;
	}
}

void DebugOverlayService::CaptureSwapChain(IUnknown* a_deviceCandidate, IUnknown* a_swapChainCandidate, HWND a_hwnd)
{
	std::scoped_lock lock(_lock);

	const auto swapChain = QueryInterface<IDXGISwapChain3>(a_swapChainCandidate);
	if (!swapChain) {
		return;
	}

	if (_swapChain && _swapChain.Get() != swapChain.Get()) {
		CleanupOverlayState();
	}

	_swapChain = swapChain;

	if (a_deviceCandidate) {
		const auto commandQueue = QueryInterface<ID3D12CommandQueue>(a_deviceCandidate);
		if (commandQueue) {
			_commandQueue = commandQueue;
		}
	}

	if (!_device && _commandQueue) {
		_commandQueue->GetDevice(IID_PPV_ARGS(_device.ReleaseAndGetAddressOf()));
	}

	if (!a_hwnd) {
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		if (SUCCEEDED(_swapChain->GetDesc(&swapChainDesc))) {
			a_hwnd = swapChainDesc.OutputWindow;
		}
	}

	if (a_hwnd) {
		AttachWindow(a_hwnd);
	}
}

bool DebugOverlayService::EnsureInitialized()
{
	std::scoped_lock lock(_lock);

	if (_imguiInitialized) {
		return true;
	}

	if (!_swapChain || !_commandQueue) {
		return false;
	}

	if (!_device && FAILED(_commandQueue->GetDevice(IID_PPV_ARGS(_device.ReleaseAndGetAddressOf())))) {
		SetLastError("Failed to retrieve the D3D12 device from the captured command queue.");
		return false;
	}

	if (!_window) {
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		if (SUCCEEDED(_swapChain->GetDesc(&swapChainDesc)) && swapChainDesc.OutputWindow) {
			AttachWindow(swapChainDesc.OutputWindow);
		}
	}

	if (!_window) {
		SetLastError("The overlay could not find the game window handle.");
		return false;
	}

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	if (FAILED(_swapChain->GetDesc(&swapChainDesc))) {
		SetLastError("Failed to read the captured swap chain description.");
		return false;
	}

	_rtvFormat = swapChainDesc.BufferDesc.Format;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(_srvHeap.ReleaseAndGetAddressOf())))) {
		SetLastError("Failed to create the ImGui SRV descriptor heap.");
		return false;
	}

	_fontSrvCpuHandle = _srvHeap->GetCPUDescriptorHandleForHeapStart();
	_fontSrvGpuHandle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
	_srvAllocated = false;

	if (!CreateRenderTargets()) {
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;
	io.MouseDrawCursor = _overlayVisible;

	ImGui::StyleColorsDark();

	if (!ImGui_ImplWin32_Init(_window)) {
		SetLastError("ImGui Win32 initialization failed.");
		ImGui::DestroyContext();
		return false;
	}

	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = _device.Get();
	initInfo.CommandQueue = _commandQueue.Get();
	initInfo.NumFramesInFlight = static_cast<int>(_frames.size());
	initInfo.RTVFormat = _rtvFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.UserData = this;
	initInfo.SrvDescriptorHeap = _srvHeap.Get();
	initInfo.SrvDescriptorAllocFn = &AllocateSrvDescriptor;
	initInfo.SrvDescriptorFreeFn = &FreeSrvDescriptor;

	if (!ImGui_ImplDX12_Init(&initInfo)) {
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		SetLastError("ImGui DirectX 12 initialization failed.");
		return false;
	}

	_imguiInitialized = true;
	return true;
}

bool DebugOverlayService::CreateRenderTargets()
{
	if (!_swapChain || !_device) {
		SetLastError("CreateRenderTargets was called before the swap chain and device were available.");
		return false;
	}

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	if (FAILED(_swapChain->GetDesc(&swapChainDesc))) {
		SetLastError("Failed to query the swap chain description while creating render targets.");
		return false;
	}

	CleanupRenderTargets();

	const auto bufferCount = std::max<UINT>(swapChainDesc.BufferCount, 2);
	_frames.resize(bufferCount);

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = bufferCount;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	if (FAILED(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(_rtvHeap.ReleaseAndGetAddressOf())))) {
		SetLastError("Failed to create the RTV descriptor heap for the debug overlay.");
		return false;
	}

	_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	auto rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT index = 0; index < bufferCount; ++index) {
		if (FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_frames[index].commandAllocator.ReleaseAndGetAddressOf())))) {
			SetLastError("Failed to create a per-frame command allocator for the debug overlay.");
			return false;
		}

		if (FAILED(_swapChain->GetBuffer(index, IID_PPV_ARGS(_frames[index].renderTarget.ReleaseAndGetAddressOf())))) {
			SetLastError("Failed to retrieve a swap chain back buffer for the debug overlay.");
			return false;
		}

		_frames[index].rtvHandle = rtvHandle;
		_device->CreateRenderTargetView(_frames[index].renderTarget.Get(), nullptr, _frames[index].rtvHandle);
		rtvHandle.ptr += _rtvDescriptorSize;
	}

	if (FAILED(_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			_frames[0].commandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(_commandList.ReleaseAndGetAddressOf())))) {
		SetLastError("Failed to create the debug overlay command list.");
		return false;
	}

	_commandList->Close();
	return true;
}

void DebugOverlayService::CleanupRenderTargets()
{
	_commandList.Reset();
	_frames.clear();
	_rtvHeap.Reset();
	_rtvDescriptorSize = 0;
}

void DebugOverlayService::CleanupOverlayState()
{
	if (_imguiInitialized) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		_imguiInitialized = false;
	}

	CleanupRenderTargets();

	_srvHeap.Reset();
	_srvAllocated = false;
	_device.Reset();
	_commandQueue.Reset();
	_swapChain.Reset();
	_rtvFormat = DXGI_FORMAT_UNKNOWN;
}

void DebugOverlayService::DrawRootWindow()
{
	auto panels = DebugPanelRegistry::GetSingleton().Snapshot();

	ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_FirstUseEver);
	bool windowOpen = _overlayVisible;
	if (!ImGui::Begin("OSF Debug", &windowOpen, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		_overlayVisible = windowOpen;
		return;
	}

	_overlayVisible = windowOpen;

	ImGui::TextUnformatted("Press F10 to toggle this overlay.");
	ImGui::Checkbox("Show ImGui demo window", &_showDemoWindow);
	ImGui::Separator();

	if (ImGui::BeginTabBar("osf_debug_panels")) {
		for (auto& panel : panels) {
			if (ImGui::BeginTabItem(panel.name.c_str())) {
				panel.draw();
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	} else if (panels.empty()) {
		ImGui::TextUnformatted("No debug panels are currently registered.");
	}

	ImGui::End();

	if (_showDemoWindow) {
		ImGui::ShowDemoWindow(&_showDemoWindow);
	}
}

void DebugOverlayService::RenderOverlay()
{
	if (!EnsureInitialized()) {
		return;
	}

	std::scoped_lock lock(_lock);

	if (!_overlayVisible) {
		return;
	}

	ImGui::GetIO().MouseDrawCursor = _overlayVisible;

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	DrawRootWindow();
	ImGui::Render();

	auto* drawData = ImGui::GetDrawData();
	if (!drawData || drawData->CmdListsCount <= 0) {
		return;
	}

	const auto backBufferIndex = _swapChain->GetCurrentBackBufferIndex();
	if (backBufferIndex >= _frames.size()) {
		return;
	}

	auto& frame = _frames[backBufferIndex];

	if (FAILED(frame.commandAllocator->Reset())) {
		return;
	}

	if (FAILED(_commandList->Reset(frame.commandAllocator.Get(), nullptr))) {
		return;
	}

	D3D12_RESOURCE_BARRIER beginBarrier{};
	beginBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	beginBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	beginBarrier.Transition.pResource = frame.renderTarget.Get();
	beginBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	beginBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	beginBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_commandList->ResourceBarrier(1, &beginBarrier);

	_commandList->OMSetRenderTargets(1, &frame.rtvHandle, FALSE, nullptr);

	ID3D12DescriptorHeap* descriptorHeaps[] = { _srvHeap.Get() };
	_commandList->SetDescriptorHeaps(1, descriptorHeaps);

	ImGui_ImplDX12_RenderDrawData(drawData, _commandList.Get());

	D3D12_RESOURCE_BARRIER endBarrier{};
	endBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	endBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	endBarrier.Transition.pResource = frame.renderTarget.Get();
	endBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	endBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	endBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	_commandList->ResourceBarrier(1, &endBarrier);

	if (FAILED(_commandList->Close())) {
		return;
	}

	ID3D12CommandList* commandLists[] = { _commandList.Get() };
	_commandQueue->ExecuteCommandLists(1, commandLists);
}

void DebugOverlayService::AttachWindow(HWND a_hwnd)
{
	if (!a_hwnd || (_window == a_hwnd && _originalWndProc)) {
		return;
	}

	DetachWindow();

	const auto originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(a_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProc)));
	if (!originalProc) {
		SetLastError("Failed to subclass the Starfield game window for ImGui input.");
		return;
	}

	_window = a_hwnd;
	_originalWndProc = originalProc;
}

void DebugOverlayService::DetachWindow()
{
	if (_window && _originalWndProc) {
		SetWindowLongPtrW(_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_originalWndProc));
	}

	_window = nullptr;
	_originalWndProc = nullptr;
}

bool DebugOverlayService::IsMouseMessage(UINT a_message)
{
	switch (a_message) {
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
	case WM_SETCURSOR:
		return true;
	default:
		return false;
	}
}

bool DebugOverlayService::IsKeyboardMessage(UINT a_message)
{
	switch (a_message) {
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_CHAR:
	case WM_SYSCHAR:
	case WM_UNICHAR:
		return true;
	default:
		return false;
	}
}

void DebugOverlayService::AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* a_info, D3D12_CPU_DESCRIPTOR_HANDLE* a_cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* a_gpuHandle)
{
	auto* self = static_cast<DebugOverlayService*>(a_info->UserData);
	std::scoped_lock lock(self->_lock);

	*a_cpuHandle = self->_fontSrvCpuHandle;
	*a_gpuHandle = self->_fontSrvGpuHandle;
	self->_srvAllocated = true;
}

void DebugOverlayService::FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* a_info, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
	auto* self = static_cast<DebugOverlayService*>(a_info->UserData);
	std::scoped_lock lock(self->_lock);

	self->_srvAllocated = false;
}

HRESULT __stdcall DebugOverlayService::HookCreateSwapChain(IDXGIFactory* a_factory, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_swapChain)
{
	auto& service = GetSingleton();
	const auto result = service._originalCreateSwapChain(a_factory, a_device, a_desc, a_swapChain);

	if (SUCCEEDED(result) && a_swapChain && *a_swapChain) {
		service.CaptureSwapChain(a_device, *a_swapChain, a_desc ? a_desc->OutputWindow : nullptr);
	}

	return result;
}

HRESULT __stdcall DebugOverlayService::HookCreateSwapChainForHwnd(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fullscreenDesc, IDXGIOutput* a_restrictToOutput, IDXGISwapChain1** a_swapChain)
{
	auto& service = GetSingleton();
	const auto result = service._originalCreateSwapChainForHwnd(a_factory, a_device, a_hwnd, a_desc, a_fullscreenDesc, a_restrictToOutput, a_swapChain);

	if (SUCCEEDED(result) && a_swapChain && *a_swapChain) {
		service.CaptureSwapChain(a_device, *a_swapChain, a_hwnd);
	}

	return result;
}

HRESULT __stdcall DebugOverlayService::HookPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
{
	auto& service = GetSingleton();

	service.CaptureSwapChain(nullptr, a_swapChain, nullptr);
	service.RenderOverlay();

	return service._originalPresent(a_swapChain, a_syncInterval, a_flags);
}

HRESULT __stdcall DebugOverlayService::HookResizeBuffers(IDXGISwapChain* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_newFormat, UINT a_swapChainFlags)
{
	auto& service = GetSingleton();
	const auto incomingSwapChain = QueryInterface<IDXGISwapChain3>(a_swapChain);

	{
		std::scoped_lock lock(service._lock);

		if (service._swapChain && incomingSwapChain && service._swapChain.Get() == incomingSwapChain.Get()) {
			if (service._imguiInitialized) {
				ImGui_ImplDX12_InvalidateDeviceObjects();
			}

			service.CleanupRenderTargets();
		}
	}

	const auto result = service._originalResizeBuffers(a_swapChain, a_bufferCount, a_width, a_height, a_newFormat, a_swapChainFlags);
	if (FAILED(result)) {
		return result;
	}

	service.CaptureSwapChain(nullptr, a_swapChain, nullptr);

	std::scoped_lock lock(service._lock);
	if (service._swapChain && incomingSwapChain && service._swapChain.Get() == incomingSwapChain.Get()) {
		if (a_newFormat != DXGI_FORMAT_UNKNOWN) {
			service._rtvFormat = a_newFormat;
		}

		if (service._imguiInitialized && service.CreateRenderTargets()) {
			ImGui_ImplDX12_CreateDeviceObjects();
		}
	}

	return result;
}

LRESULT CALLBACK DebugOverlayService::HookWndProc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
{
	auto& service = GetSingleton();

	if ((a_message == WM_KEYUP || a_message == WM_SYSKEYUP) && a_wParam == kOverlayToggleVirtualKey) {
		service.ToggleVisibility();
		return 0;
	}

	WNDPROC originalWndProc = nullptr;
	bool    overlayVisible = false;
	bool    imguiInitialized = false;
	{
		std::scoped_lock lock(service._lock);
		originalWndProc = service._originalWndProc;
		overlayVisible = service._overlayVisible;
		imguiInitialized = service._imguiInitialized;
	}

	if (overlayVisible && imguiInitialized) {
		ImGui_ImplWin32_WndProcHandler(a_hwnd, a_message, a_wParam, a_lParam);

		if (IsMouseMessage(a_message) || IsKeyboardMessage(a_message)) {
			return 1;
		}
	}

	return originalWndProc ? CallWindowProcW(originalWndProc, a_hwnd, a_message, a_wParam, a_lParam) : DefWindowProcW(a_hwnd, a_message, a_wParam, a_lParam);
}
