#include "Overlay.h"
#include "UIManager.h"

#include <cfloat>
#include <cstdint>
#include <mutex>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include "REX/REX.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"

namespace Overlay
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		struct DescriptorHeapAllocator
		{
			bool Create(ID3D12Device *a_device, ID3D12DescriptorHeap *a_heap)
			{
				if (!a_device || !a_heap)
				{
					return false;
				}

				const auto desc = a_heap->GetDesc();
				if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
					(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0)
				{
					return false;
				}

				std::scoped_lock lock(mutex);

				heap = a_heap;
				heapStartCpu = heap->GetCPUDescriptorHandleForHeapStart();
				heapStartGpu = heap->GetGPUDescriptorHandleForHeapStart();
				increment = a_device->GetDescriptorHandleIncrementSize(desc.Type);

				freeIndices.clear();
				freeIndices.reserve(desc.NumDescriptors);
				for (UINT i = 0; i < desc.NumDescriptors; ++i)
				{
					freeIndices.push_back(static_cast<int>(desc.NumDescriptors - i - 1));
				}

				return true;
			}

			bool Allocate(
				D3D12_CPU_DESCRIPTOR_HANDLE *a_outCpuHandle,
				D3D12_GPU_DESCRIPTOR_HANDLE *a_outGpuHandle)
			{
				if (!a_outCpuHandle || !a_outGpuHandle)
				{
					return false;
				}

				std::scoped_lock lock(mutex);

				if (!heap || freeIndices.empty())
				{
					return false;
				}

				const auto index = static_cast<UINT>(freeIndices.back());
				freeIndices.pop_back();

				a_outCpuHandle->ptr = heapStartCpu.ptr + static_cast<SIZE_T>(index) * increment;
				a_outGpuHandle->ptr = heapStartGpu.ptr + static_cast<UINT64>(index) * increment;
				return true;
			}

			void Free(
				D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
				D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
			{
				std::scoped_lock lock(mutex);

				if (!heap || increment == 0)
				{
					return;
				}

				const auto cpuOffset = a_cpuHandle.ptr - heapStartCpu.ptr;
				const auto gpuOffset = a_gpuHandle.ptr - heapStartGpu.ptr;
				const auto cpuIndex = static_cast<int>(cpuOffset / increment);
				const auto gpuIndex = static_cast<int>(gpuOffset / increment);

				if (cpuIndex >= 0 && cpuIndex == gpuIndex)
				{
					freeIndices.push_back(cpuIndex);
				}
			}

			std::mutex mutex;
			ComPtr<ID3D12DescriptorHeap> heap;
			D3D12_CPU_DESCRIPTOR_HANDLE heapStartCpu{};
			D3D12_GPU_DESCRIPTOR_HANDLE heapStartGpu{};
			UINT increment{0};
			std::vector<int> freeIndices;
		};

		struct RuntimeState
		{
			std::mutex mutex;
			bool initialized{false};
			ComPtr<IDXGISwapChain3> swapChain;
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12CommandQueue> commandQueue;
			ComPtr<ID3D12DescriptorHeap> srvHeap;
			HWND hwnd{nullptr};
			UINT framesInFlight{2};
			DXGI_FORMAT rtvFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
			DescriptorHeapAllocator srvAllocator;
			ComPtr<ID3D12DescriptorHeap> rtvHeap;
			UINT rtvDescriptorSize{0};
			std::vector<ComPtr<ID3D12Resource>> backBuffers;
			std::vector<ComPtr<ID3D12CommandAllocator>> commandAllocators;
			ComPtr<ID3D12GraphicsCommandList> commandList;
			INT64 ticksPerSecond{0};
			INT64 lastFrameTicks{0};
			bool hadFocus{false};
		};

		RuntimeState g_state;

		void AllocateSrvDescriptor(
			ImGui_ImplDX12_InitInfo *,
			D3D12_CPU_DESCRIPTOR_HANDLE *a_outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE *a_outGpuHandle)
		{
			if (!g_state.srvAllocator.Allocate(a_outCpuHandle, a_outGpuHandle))
			{
				REX::ERROR("Overlay: failed to allocate a shader-visible SRV descriptor for ImGui");
			}
		}

		void FreeSrvDescriptor(
			ImGui_ImplDX12_InitInfo *,
			D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
		{
			g_state.srvAllocator.Free(a_cpuHandle, a_gpuHandle);
		}

		bool ReadRawInput(void *a_rawInputHandle, RAWINPUT &a_rawInput)
		{
			if (!a_rawInputHandle)
			{
				return false;
			}

			UINT dataSize = sizeof(RAWINPUT);
			const auto headerSize = static_cast<UINT>(sizeof(RAWINPUTHEADER));
			if (::GetRawInputData(
					reinterpret_cast<HRAWINPUT>(a_rawInputHandle),
					RID_INPUT,
					&a_rawInput,
					&dataSize,
					headerSize) == UINT_MAX)
			{
				return false;
			}

			return dataSize >= sizeof(RAWINPUTHEADER);
		}

		bool IsVkDown(int a_virtualKey)
		{
			return (::GetKeyState(a_virtualKey) & 0x8000) != 0;
		}

		void AddKeyEvent(ImGuiIO &a_io, ImGuiKey a_key, bool a_isDown, int a_virtualKey, int a_scanCode = -1)
		{
			a_io.AddKeyEvent(a_key, a_isDown);
			a_io.SetKeyEventNativeData(a_key, a_virtualKey, a_scanCode);
		}

		void ProcessKeyEventsWorkarounds(ImGuiIO &a_io)
		{
			if (ImGui::IsKeyDown(ImGuiKey_LeftShift) && !IsVkDown(VK_LSHIFT))
			{
				AddKeyEvent(a_io, ImGuiKey_LeftShift, false, VK_LSHIFT);
			}
			if (ImGui::IsKeyDown(ImGuiKey_RightShift) && !IsVkDown(VK_RSHIFT))
			{
				AddKeyEvent(a_io, ImGuiKey_RightShift, false, VK_RSHIFT);
			}
			if (ImGui::IsKeyDown(ImGuiKey_LeftSuper) && !IsVkDown(VK_LWIN))
			{
				AddKeyEvent(a_io, ImGuiKey_LeftSuper, false, VK_LWIN);
			}
			if (ImGui::IsKeyDown(ImGuiKey_RightSuper) && !IsVkDown(VK_RWIN))
			{
				AddKeyEvent(a_io, ImGuiKey_RightSuper, false, VK_RWIN);
			}
		}

		void UpdateKeyModifiers(ImGuiIO &a_io)
		{
			a_io.AddKeyEvent(ImGuiMod_Ctrl, IsVkDown(VK_CONTROL));
			a_io.AddKeyEvent(ImGuiMod_Shift, IsVkDown(VK_SHIFT));
			a_io.AddKeyEvent(ImGuiMod_Alt, IsVkDown(VK_MENU));
			a_io.AddKeyEvent(ImGuiMod_Super, IsVkDown(VK_LWIN) || IsVkDown(VK_RWIN));
		}

		ImGuiKey KeyEventToImGuiKey(WPARAM a_virtualKey, LPARAM a_lParam)
		{
			if ((a_virtualKey == VK_RETURN) && (HIWORD(a_lParam) & KF_EXTENDED))
			{
				return ImGuiKey_KeypadEnter;
			}

			const int scanCode = static_cast<int>(LOBYTE(HIWORD(a_lParam)));
			switch (a_virtualKey)
			{
			case VK_TAB:
				return ImGuiKey_Tab;
			case VK_LEFT:
				return ImGuiKey_LeftArrow;
			case VK_RIGHT:
				return ImGuiKey_RightArrow;
			case VK_UP:
				return ImGuiKey_UpArrow;
			case VK_DOWN:
				return ImGuiKey_DownArrow;
			case VK_PRIOR:
				return ImGuiKey_PageUp;
			case VK_NEXT:
				return ImGuiKey_PageDown;
			case VK_HOME:
				return ImGuiKey_Home;
			case VK_END:
				return ImGuiKey_End;
			case VK_INSERT:
				return ImGuiKey_Insert;
			case VK_DELETE:
				return ImGuiKey_Delete;
			case VK_BACK:
				return ImGuiKey_Backspace;
			case VK_SPACE:
				return ImGuiKey_Space;
			case VK_RETURN:
				return ImGuiKey_Enter;
			case VK_ESCAPE:
				return ImGuiKey_Escape;
			case VK_OEM_COMMA:
				return ImGuiKey_Comma;
			case VK_OEM_PERIOD:
				return ImGuiKey_Period;
			case VK_CAPITAL:
				return ImGuiKey_CapsLock;
			case VK_SCROLL:
				return ImGuiKey_ScrollLock;
			case VK_NUMLOCK:
				return ImGuiKey_NumLock;
			case VK_SNAPSHOT:
				return ImGuiKey_PrintScreen;
			case VK_PAUSE:
				return ImGuiKey_Pause;
			case VK_NUMPAD0:
				return ImGuiKey_Keypad0;
			case VK_NUMPAD1:
				return ImGuiKey_Keypad1;
			case VK_NUMPAD2:
				return ImGuiKey_Keypad2;
			case VK_NUMPAD3:
				return ImGuiKey_Keypad3;
			case VK_NUMPAD4:
				return ImGuiKey_Keypad4;
			case VK_NUMPAD5:
				return ImGuiKey_Keypad5;
			case VK_NUMPAD6:
				return ImGuiKey_Keypad6;
			case VK_NUMPAD7:
				return ImGuiKey_Keypad7;
			case VK_NUMPAD8:
				return ImGuiKey_Keypad8;
			case VK_NUMPAD9:
				return ImGuiKey_Keypad9;
			case VK_DECIMAL:
				return ImGuiKey_KeypadDecimal;
			case VK_DIVIDE:
				return ImGuiKey_KeypadDivide;
			case VK_MULTIPLY:
				return ImGuiKey_KeypadMultiply;
			case VK_SUBTRACT:
				return ImGuiKey_KeypadSubtract;
			case VK_ADD:
				return ImGuiKey_KeypadAdd;
			case VK_LSHIFT:
				return ImGuiKey_LeftShift;
			case VK_LCONTROL:
				return ImGuiKey_LeftCtrl;
			case VK_LMENU:
				return ImGuiKey_LeftAlt;
			case VK_LWIN:
				return ImGuiKey_LeftSuper;
			case VK_RSHIFT:
				return ImGuiKey_RightShift;
			case VK_RCONTROL:
				return ImGuiKey_RightCtrl;
			case VK_RMENU:
				return ImGuiKey_RightAlt;
			case VK_RWIN:
				return ImGuiKey_RightSuper;
			case VK_APPS:
				return ImGuiKey_Menu;
			case '0':
				return ImGuiKey_0;
			case '1':
				return ImGuiKey_1;
			case '2':
				return ImGuiKey_2;
			case '3':
				return ImGuiKey_3;
			case '4':
				return ImGuiKey_4;
			case '5':
				return ImGuiKey_5;
			case '6':
				return ImGuiKey_6;
			case '7':
				return ImGuiKey_7;
			case '8':
				return ImGuiKey_8;
			case '9':
				return ImGuiKey_9;
			case 'A':
				return ImGuiKey_A;
			case 'B':
				return ImGuiKey_B;
			case 'C':
				return ImGuiKey_C;
			case 'D':
				return ImGuiKey_D;
			case 'E':
				return ImGuiKey_E;
			case 'F':
				return ImGuiKey_F;
			case 'G':
				return ImGuiKey_G;
			case 'H':
				return ImGuiKey_H;
			case 'I':
				return ImGuiKey_I;
			case 'J':
				return ImGuiKey_J;
			case 'K':
				return ImGuiKey_K;
			case 'L':
				return ImGuiKey_L;
			case 'M':
				return ImGuiKey_M;
			case 'N':
				return ImGuiKey_N;
			case 'O':
				return ImGuiKey_O;
			case 'P':
				return ImGuiKey_P;
			case 'Q':
				return ImGuiKey_Q;
			case 'R':
				return ImGuiKey_R;
			case 'S':
				return ImGuiKey_S;
			case 'T':
				return ImGuiKey_T;
			case 'U':
				return ImGuiKey_U;
			case 'V':
				return ImGuiKey_V;
			case 'W':
				return ImGuiKey_W;
			case 'X':
				return ImGuiKey_X;
			case 'Y':
				return ImGuiKey_Y;
			case 'Z':
				return ImGuiKey_Z;
			case VK_F1:
				return ImGuiKey_F1;
			case VK_F2:
				return ImGuiKey_F2;
			case VK_F3:
				return ImGuiKey_F3;
			case VK_F4:
				return ImGuiKey_F4;
			case VK_F5:
				return ImGuiKey_F5;
			case VK_F6:
				return ImGuiKey_F6;
			case VK_F7:
				return ImGuiKey_F7;
			case VK_F8:
				return ImGuiKey_F8;
			case VK_F9:
				return ImGuiKey_F9;
			case VK_F10:
				return ImGuiKey_F10;
			case VK_F11:
				return ImGuiKey_F11;
			case VK_F12:
				return ImGuiKey_F12;
			case VK_F13:
				return ImGuiKey_F13;
			case VK_F14:
				return ImGuiKey_F14;
			case VK_F15:
				return ImGuiKey_F15;
			case VK_F16:
				return ImGuiKey_F16;
			case VK_F17:
				return ImGuiKey_F17;
			case VK_F18:
				return ImGuiKey_F18;
			case VK_F19:
				return ImGuiKey_F19;
			case VK_F20:
				return ImGuiKey_F20;
			case VK_F21:
				return ImGuiKey_F21;
			case VK_F22:
				return ImGuiKey_F22;
			case VK_F23:
				return ImGuiKey_F23;
			case VK_F24:
				return ImGuiKey_F24;
			case VK_BROWSER_BACK:
				return ImGuiKey_AppBack;
			case VK_BROWSER_FORWARD:
				return ImGuiKey_AppForward;
			default:
				break;
			}

			switch (scanCode)
			{
			case 41:
				return ImGuiKey_GraveAccent;
			case 12:
				return ImGuiKey_Minus;
			case 13:
				return ImGuiKey_Equal;
			case 26:
				return ImGuiKey_LeftBracket;
			case 27:
				return ImGuiKey_RightBracket;
			case 86:
				return ImGuiKey_Oem102;
			case 43:
				return ImGuiKey_Backslash;
			case 39:
				return ImGuiKey_Semicolon;
			case 40:
				return ImGuiKey_Apostrophe;
			case 51:
				return ImGuiKey_Comma;
			case 52:
				return ImGuiKey_Period;
			case 53:
				return ImGuiKey_Slash;
			default:
				break;
			}

			return ImGuiKey_None;
		}

		UINT NormalizeVirtualKey(const RAWKEYBOARD &a_keyboard)
		{
			UINT virtualKey = a_keyboard.VKey;
			UINT scanCode = a_keyboard.MakeCode;

			if (virtualKey == 255)
			{
				return 0;
			}

			if (virtualKey == VK_SHIFT)
			{
				virtualKey = ::MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
			}
			else if (virtualKey == VK_CONTROL)
			{
				virtualKey = (a_keyboard.Flags & RI_KEY_E0) != 0 ? VK_RCONTROL : VK_LCONTROL;
			}
			else if (virtualKey == VK_MENU)
			{
				virtualKey = (a_keyboard.Flags & RI_KEY_E0) != 0 ? VK_RMENU : VK_LMENU;
			}

			return virtualKey;
		}

		LPARAM MakeKeyLParam(const RAWKEYBOARD &a_keyboard)
		{
			LPARAM lParam = static_cast<LPARAM>(a_keyboard.MakeCode) << 16;
			if ((a_keyboard.Flags & RI_KEY_E0) != 0)
			{
				lParam |= static_cast<LPARAM>(KF_EXTENDED) << 16;
			}
			return lParam;
		}

		void SubmitTextInput(ImGuiIO &a_io, const RAWKEYBOARD &a_keyboard, UINT a_virtualKey, bool a_isKeyDown)
		{
			if (!a_isKeyDown || a_virtualKey == 0 || a_virtualKey >= 256)
			{
				return;
			}

			BYTE keyboardState[256]{};
			if (!::GetKeyboardState(keyboardState))
			{
				return;
			}

			keyboardState[a_virtualKey] = static_cast<BYTE>(keyboardState[a_virtualKey] | 0x80);

			wchar_t chars[8]{};
			const UINT scanCode = a_keyboard.MakeCode | (((a_keyboard.Flags & RI_KEY_E0) != 0) ? 0xE000u : 0u);
			const auto rc = ::ToUnicodeEx(
				a_virtualKey,
				scanCode,
				keyboardState,
				chars,
				static_cast<int>(std::size(chars)),
				0,
				::GetKeyboardLayout(0));
			if (rc <= 0)
			{
				return;
			}

			for (int i = 0; i < rc; ++i)
			{
				a_io.AddInputCharacterUTF16(static_cast<ImWchar16>(chars[i]));
			}
		}

		void UpdateMousePosition(ImGuiIO &a_io)
		{
			if (!g_state.hwnd)
			{
				a_io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
				return;
			}

			POINT position{};
			if (::GetCursorPos(&position) && ::ScreenToClient(g_state.hwnd, &position))
			{
				a_io.AddMousePosEvent(static_cast<float>(position.x), static_cast<float>(position.y));
			}
		}

		void HandleKeyboardRawInput(const RAWKEYBOARD &a_keyboard)
		{
			const UINT virtualKey = NormalizeVirtualKey(a_keyboard);
			if (virtualKey == 0)
			{
				return;
			}

			ImGuiIO &io = ImGui::GetIO();
			const bool isKeyDown = (a_keyboard.Flags & RI_KEY_BREAK) == 0;
			const auto lParam = MakeKeyLParam(a_keyboard);
			const auto imguiKey = KeyEventToImGuiKey(virtualKey, lParam);
			const int scanCode = static_cast<int>(a_keyboard.MakeCode);

			UpdateKeyModifiers(io);

			if (imguiKey == ImGuiKey_PrintScreen && !isKeyDown)
			{
				AddKeyEvent(io, imguiKey, true, static_cast<int>(virtualKey), scanCode);
			}

			if (imguiKey != ImGuiKey_None)
			{
				AddKeyEvent(io, imguiKey, isKeyDown, static_cast<int>(virtualKey), scanCode);
			}

			if (virtualKey == VK_F10)
			{
				static bool toggleKeyDown = false;
				if (!isKeyDown)
				{
					toggleKeyDown = false;
				}
				else if (!toggleKeyDown)
				{
					UIManager::Toggle();
					toggleKeyDown = true;
				}
			}

			SubmitTextInput(io, a_keyboard, virtualKey, isKeyDown);
		}

		void HandleMouseRawInput(const RAWMOUSE &a_mouse)
		{
			ImGuiIO &io = ImGui::GetIO();
			io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
			UpdateMousePosition(io);

			const auto buttonFlags = a_mouse.usButtonFlags;
			if ((buttonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
			{
				io.AddMouseButtonEvent(0, true);
			}
			if ((buttonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
			{
				io.AddMouseButtonEvent(0, false);
			}
			if ((buttonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
			{
				io.AddMouseButtonEvent(1, true);
			}
			if ((buttonFlags & RI_MOUSE_RIGHT_BUTTON_UP) != 0)
			{
				io.AddMouseButtonEvent(1, false);
			}
			if ((buttonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0)
			{
				io.AddMouseButtonEvent(2, true);
			}
			if ((buttonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) != 0)
			{
				io.AddMouseButtonEvent(2, false);
			}
			if ((buttonFlags & RI_MOUSE_BUTTON_4_DOWN) != 0)
			{
				io.AddMouseButtonEvent(3, true);
			}
			if ((buttonFlags & RI_MOUSE_BUTTON_4_UP) != 0)
			{
				io.AddMouseButtonEvent(3, false);
			}
			if ((buttonFlags & RI_MOUSE_BUTTON_5_DOWN) != 0)
			{
				io.AddMouseButtonEvent(4, true);
			}
			if ((buttonFlags & RI_MOUSE_BUTTON_5_UP) != 0)
			{
				io.AddMouseButtonEvent(4, false);
			}
			if ((buttonFlags & RI_MOUSE_WHEEL) != 0)
			{
				const auto wheelDelta = static_cast<float>(static_cast<SHORT>(a_mouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
				io.AddMouseWheelEvent(0.0f, wheelDelta);
			}
			if ((buttonFlags & RI_MOUSE_HWHEEL) != 0)
			{
				const auto wheelDelta = static_cast<float>(static_cast<SHORT>(a_mouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
				io.AddMouseWheelEvent(-wheelDelta, 0.0f);
			}
		}

		void UpdateFrameInputState()
		{
			ImGuiIO &io = ImGui::GetIO();

			RECT clientRect{};
			if (::GetClientRect(g_state.hwnd, &clientRect))
			{
				io.DisplaySize = ImVec2(
					static_cast<float>(clientRect.right - clientRect.left),
					static_cast<float>(clientRect.bottom - clientRect.top));
			}

			LARGE_INTEGER now{};
			if (::QueryPerformanceCounter(&now) && g_state.ticksPerSecond > 0)
			{
				if (g_state.lastFrameTicks > 0)
				{
					const auto elapsedTicks = now.QuadPart - g_state.lastFrameTicks;
					io.DeltaTime = elapsedTicks > 0 ?
									   static_cast<float>(elapsedTicks) / static_cast<float>(g_state.ticksPerSecond) :
									   (1.0f / 60.0f);
				}
				else
				{
					io.DeltaTime = 1.0f / 60.0f;
				}
				g_state.lastFrameTicks = now.QuadPart;
			}
			else
			{
				io.DeltaTime = 1.0f / 60.0f;
			}

			const bool hasFocus = ::GetForegroundWindow() == g_state.hwnd;
			if (hasFocus != g_state.hadFocus)
			{
				io.AddFocusEvent(hasFocus);
				g_state.hadFocus = hasFocus;
			}

			if (hasFocus)
			{
				if (io.WantSetMousePos)
				{
					POINT position{
						static_cast<LONG>(io.MousePos.x),
						static_cast<LONG>(io.MousePos.y)};
					if (::ClientToScreen(g_state.hwnd, &position))
					{
						::SetCursorPos(position.x, position.y);
					}
				}

				UpdateMousePosition(io);
			}
			else
			{
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			}

			ProcessKeyEventsWorkarounds(io);
		}

		bool CreateRenderTargets()
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(g_state.swapChain->GetDesc(&desc)))
			{
				REX::ERROR("Overlay: failed to read swap-chain desc for render targets");
				return false;
			}

			const UINT bufferCount = desc.BufferCount;

			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.NumDescriptors = bufferCount;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			if (FAILED(g_state.device->CreateDescriptorHeap(
					&rtvHeapDesc, IID_PPV_ARGS(g_state.rtvHeap.ReleaseAndGetAddressOf()))))
			{
				REX::ERROR("Overlay: failed to create RTV descriptor heap");
				return false;
			}

			g_state.rtvDescriptorSize = g_state.device->GetDescriptorHandleIncrementSize(
				D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			g_state.backBuffers.resize(bufferCount);
			auto rtvHandle = g_state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < bufferCount; ++i)
			{
				if (FAILED(g_state.swapChain->GetBuffer(
						i, IID_PPV_ARGS(g_state.backBuffers[i].ReleaseAndGetAddressOf()))))
				{
					REX::ERROR("Overlay: failed to get back buffer {}", i);
					return false;
				}
				g_state.device->CreateRenderTargetView(
					g_state.backBuffers[i].Get(), nullptr, rtvHandle);
				rtvHandle.ptr += g_state.rtvDescriptorSize;
			}

			g_state.commandAllocators.resize(bufferCount);
			for (UINT i = 0; i < bufferCount; ++i)
			{
				if (FAILED(g_state.device->CreateCommandAllocator(
						D3D12_COMMAND_LIST_TYPE_DIRECT,
						IID_PPV_ARGS(g_state.commandAllocators[i].ReleaseAndGetAddressOf()))))
				{
					REX::ERROR("Overlay: failed to create command allocator {}", i);
					return false;
				}
			}

			if (FAILED(g_state.device->CreateCommandList(
					0, D3D12_COMMAND_LIST_TYPE_DIRECT,
					g_state.commandAllocators[0].Get(), nullptr,
					IID_PPV_ARGS(g_state.commandList.ReleaseAndGetAddressOf()))))
			{
				REX::ERROR("Overlay: failed to create command list");
				return false;
			}
			g_state.commandList->Close();

			REX::INFO("Overlay: render targets created buffers={}", bufferCount);
			return true;
		}

		void DestroyRenderTargets()
		{
			g_state.commandList.Reset();
			g_state.commandAllocators.clear();
			g_state.backBuffers.clear();
			g_state.rtvHeap.Reset();
			g_state.rtvDescriptorSize = 0;
		}
	}

	bool InitializeFromSwapChain(IDXGISwapChain *a_swapChain, ID3D12CommandQueue *a_commandQueue)
	{
		if (!a_swapChain || !a_commandQueue)
		{
			return false;
		}

		std::scoped_lock lock(g_state.mutex);

		if (g_state.initialized)
		{
			return true;
		}

		ComPtr<IDXGISwapChain3> swapChain3;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to upgrade the swap chain to IDXGISwapChain3");
			return false;
		}

		ComPtr<ID3D12Device> device;
		if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to retrieve ID3D12Device from the swap chain");
			return false;
		}

		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		if (FAILED(a_swapChain->GetDesc(&swapChainDesc)))
		{
			REX::ERROR("Overlay: failed to read the swap-chain description");
			return false;
		}

		if (!swapChainDesc.OutputWindow)
		{
			REX::ERROR("Overlay: swap-chain output window is null");
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 16;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ComPtr<ID3D12DescriptorHeap> srvHeap;
		if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(srvHeap.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to create the ImGui SRV descriptor heap");
			return false;
		}

		if (!g_state.srvAllocator.Create(device.Get(), srvHeap.Get()))
		{
			REX::ERROR("Overlay: failed to initialize the ImGui descriptor allocator");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO &io = ImGui::GetIO();
		io.BackendPlatformName = "osf_raw_input";
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = nullptr;

		LARGE_INTEGER frequency{};
		LARGE_INTEGER counter{};
		if (::QueryPerformanceFrequency(&frequency) && ::QueryPerformanceCounter(&counter))
		{
			g_state.ticksPerSecond = frequency.QuadPart;
			g_state.lastFrameTicks = counter.QuadPart;
		}

		ImGui::GetMainViewport()->PlatformHandle = swapChainDesc.OutputWindow;
		ImGui::GetMainViewport()->PlatformHandleRaw = swapChainDesc.OutputWindow;

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = device.Get();
		initInfo.CommandQueue = a_commandQueue;
		initInfo.NumFramesInFlight = static_cast<int>(std::max<UINT>(swapChainDesc.BufferCount, 2));
		initInfo.RTVFormat = swapChainDesc.BufferDesc.Format;
		initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap = srvHeap.Get();
		initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
		initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;

		if (!ImGui_ImplDX12_Init(&initInfo))
		{
			REX::ERROR("Overlay: ImGui DX12 backend initialization failed");
			ImGui::DestroyContext();
			return false;
		}

		g_state.swapChain = std::move(swapChain3);
		g_state.device = std::move(device);
		g_state.commandQueue = a_commandQueue;
		g_state.srvHeap = std::move(srvHeap);
		g_state.hwnd = swapChainDesc.OutputWindow;
		g_state.framesInFlight = std::max<UINT>(swapChainDesc.BufferCount, 2);
		g_state.rtvFormat = swapChainDesc.BufferDesc.Format;
		g_state.hadFocus = ::GetForegroundWindow() == g_state.hwnd;
		g_state.initialized = true;

		if (!CreateRenderTargets())
		{
			REX::ERROR("Overlay: render target creation failed; rendering will be disabled");
		}

		REX::INFO(
			"Overlay: ImGui initialized hwnd={:#x} buffers={} format={:#x}",
			reinterpret_cast<std::uintptr_t>(g_state.hwnd),
			g_state.framesInFlight,
			static_cast<std::uint32_t>(g_state.rtvFormat));

		return true;
	}

	bool IsInitialized()
	{
		std::scoped_lock lock(g_state.mutex);
		return g_state.initialized;
	}

	void HandleRawInput(void *a_rawInputHandle)
	{
		RAWINPUT rawInput{};
		if (!ReadRawInput(a_rawInputHandle, rawInput))
		{
			return;
		}

		std::scoped_lock lock(g_state.mutex);
		if (!g_state.initialized || ImGui::GetCurrentContext() == nullptr)
		{
			return;
		}

		if (rawInput.header.dwType == RIM_TYPEKEYBOARD)
		{
			HandleKeyboardRawInput(rawInput.data.keyboard);
		}
		else if (rawInput.header.dwType == RIM_TYPEMOUSE)
		{
			HandleMouseRawInput(rawInput.data.mouse);
		}
	}

	bool WantsInputCapture()
	{
		std::scoped_lock lock(g_state.mutex);
		if (!g_state.initialized || !UIManager::IsOpen() || ImGui::GetCurrentContext() == nullptr)
		{
			return false;
		}

		const ImGuiIO &io = ImGui::GetIO();
		return io.WantCaptureKeyboard || io.WantCaptureMouse;
	}

	void RenderFrame()
	{
		std::scoped_lock lock(g_state.mutex);

		if (!g_state.initialized || g_state.backBuffers.empty())
		{
			return;
		}

		const UINT frameIndex = g_state.swapChain->GetCurrentBackBufferIndex();

		auto &cmdAllocator = g_state.commandAllocators[frameIndex];
		cmdAllocator->Reset();
		g_state.commandList->Reset(cmdAllocator.Get(), nullptr);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = g_state.backBuffers[frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_state.commandList->ResourceBarrier(1, &barrier);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * g_state.rtvDescriptorSize;
		g_state.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		ID3D12DescriptorHeap *heaps[] = {g_state.srvHeap.Get()};
		g_state.commandList->SetDescriptorHeaps(1, heaps);

		UpdateFrameInputState();
		ImGui_ImplDX12_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("OSF");
		ImGui::Text("ImGui is alive");
		ImGui::End();
		UIManager::DrawFrame();

		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_state.commandList.Get());

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_state.commandList->ResourceBarrier(1, &barrier);

		g_state.commandList->Close();

		ID3D12CommandList *cmdLists[] = {g_state.commandList.Get()};
		g_state.commandQueue->ExecuteCommandLists(1, cmdLists);
	}

	void ReleaseRenderTargets()
	{
		std::scoped_lock lock(g_state.mutex);
		DestroyRenderTargets();
	}

	void RebuildRenderTargets()
	{
		std::scoped_lock lock(g_state.mutex);

		if (!g_state.initialized)
		{
			return;
		}

		if (!CreateRenderTargets())
		{
			REX::ERROR("Overlay: failed to rebuild render targets after resize");
		}
	}
}
