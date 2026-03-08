#pragma once

// ============================================================================
// FEATURE SWITCHES
// Ligue/desligue módulos definindo 1 (ativo) ou 0 (inativo)
//
// HIERARQUIA DE MASTERS (do mais amplo para o mais específico):
//
//   USE_ALL                         → força TUDO ligado
//     ├── USE_CLASSIC_C_ALL         → força todos os sub-switches de C clássico
//     │     └── USE_CLASSIC_C + USE_CLASSIC_C_IO/TEXT/MATH/DIAG/MISC/C99/C11
//     ├── USE_STL_C_ALL             → força todos os sub-switches de STL C
//     │     └── USE_STL_C + USE_STL_C_IO/TEXT/MATH/DIAG/MISC
//     └── USE_CPP_ALL               → força todos os sub-switches de C++ STL
//           └── USE_CPP + USE_CPP_IO/CONTAINERS/ALGORITHMS/STRINGS/
//                         MEMORY/UTILS/TYPES/CONCURRENCY/MATH_DIAG
//
// Regra: masters NÃO desligam sub-switches já definidos como 0 pelo usuário.
//        Eles apenas garantem que os sub-switches sejam pelo menos 1.
// ============================================================================

// ----------------------------------------------------------------------------
// [0] SUPER MASTER — USE_ALL
//     Define 1 para ligar absolutamente tudo de uma vez.
// ----------------------------------------------------------------------------
#define USE_ALL 0


// ----------------------------------------------------------------------------
// [1] MASTERS DE GRUPO
//     Cada um liga toda a sua família de includes.
//     São forçados para 1 automaticamente se USE_ALL == 1.
// ----------------------------------------------------------------------------
#define USE_CLASSIC_C_ALL 0 // Liga USE_CLASSIC_C + todos os USE_CLASSIC_C_*
#define USE_STL_C_ALL 0		// Liga USE_STL_C   + todos os USE_STL_C_*
#define USE_CPP_ALL 0		// Liga USE_CPP     + todos os USE_CPP_*


// ----------------------------------------------------------------------------
// Propagação automática de USE_ALL
// ----------------------------------------------------------------------------
#if USE_ALL
#undef USE_CLASSIC_C_ALL
#define USE_CLASSIC_C_ALL 1
#undef USE_STL_C_ALL
#define USE_STL_C_ALL 1
#undef USE_CPP_ALL
#define USE_CPP_ALL 1
#endif // USE_ALL


// ----------------------------------------------------------------------------
// [2] C CLÁSSICO  (<xxx.h>)
// ----------------------------------------------------------------------------
#define USE_CLASSIC_C 0		 //   Master do grupo
#define USE_CLASSIC_C_IO 1	 //   <stdio.h>, <stdlib.h>, <time.h>
#define USE_CLASSIC_C_TEXT 1 //   <ctype.h>, <string.h>, <wchar.h>, <wctype.h>, <locale.h>
#define USE_CLASSIC_C_MATH                                                                         \
	1 //   <math.h>, <float.h>, <limits.h>, <complex.h>, <fenv.h>, <inttypes.h>, <stdint.h>,
	  //   <tgmath.h>
#define USE_CLASSIC_C_DIAG 1 //   <assert.h>, <errno.h>, <signal.h>, <setjmp.h>
#define USE_CLASSIC_C_MISC 1 //   <stdarg.h>, <stddef.h>, <iso646.h>
#define USE_CLASSIC_C_C99 1	 //   <stdbool.h>
#define USE_CLASSIC_C_C11                                                                          \
	1 //   <stdalign.h>, <stdatomic.h>, <stdnoreturn.h>, <threads.h>, <uchar.h>

// Propagação de USE_CLASSIC_C_ALL
#if USE_CLASSIC_C_ALL
#undef USE_CLASSIC_C
#define USE_CLASSIC_C 1
#undef USE_CLASSIC_C_IO
#define USE_CLASSIC_C_IO 1
#undef USE_CLASSIC_C_TEXT
#define USE_CLASSIC_C_TEXT 1
#undef USE_CLASSIC_C_MATH
#define USE_CLASSIC_C_MATH 1
#undef USE_CLASSIC_C_DIAG
#define USE_CLASSIC_C_DIAG 1
#undef USE_CLASSIC_C_MISC
#define USE_CLASSIC_C_MISC 1
#undef USE_CLASSIC_C_C99
#define USE_CLASSIC_C_C99 1
#undef USE_CLASSIC_C_C11
#define USE_CLASSIC_C_C11 1
#endif // USE_CLASSIC_C_ALL


// ----------------------------------------------------------------------------
// [3] STL C  (wrappers C++ — <cxxx>)
// ----------------------------------------------------------------------------
#define USE_STL_C 1		 //   Master do grupo
#define USE_STL_C_IO 1	 //   <cstdio>, <cstdlib>, <ctime>
#define USE_STL_C_TEXT 1 //   <cctype>, <cstring>, <cwchar>, <cwctype>, <clocale>
#define USE_STL_C_MATH                                                                             \
	1 //   <cmath>, <cfloat>, <climits>, <ccomplex>, <cfenv>, <cstdint>, <ctgmath>
#define USE_STL_C_DIAG 1 //   <cassert>, <cerrno>, <csignal>, <csetjmp>
#define USE_STL_C_MISC 1 //   <cstdarg>, <cstddef>

// Propagação de USE_STL_C_ALL
#if USE_STL_C_ALL
#undef USE_STL_C
#define USE_STL_C 1
#undef USE_STL_C_IO
#define USE_STL_C_IO 1
#undef USE_STL_C_TEXT
#define USE_STL_C_TEXT 1
#undef USE_STL_C_MATH
#define USE_STL_C_MATH 1
#undef USE_STL_C_DIAG
#define USE_STL_C_DIAG 1
#undef USE_STL_C_MISC
#define USE_STL_C_MISC 1
#endif // USE_STL_C_ALL


// ----------------------------------------------------------------------------
// [4] C++ STANDARD LIBRARY
// ----------------------------------------------------------------------------
#define USE_CPP 1	 //   Master do grupo
#define USE_CPP_IO 1 //   <iostream>, <fstream>, <sstream>, <iomanip>
#define USE_CPP_CONTAINERS                                                                         \
	1 //   <vector>, <array>, <deque>, <list>, <map>, <set>, <queue>, <stack>, <span>, ...
#define USE_CPP_ALGORITHMS 1 //   <algorithm>, <numeric>, <iterator>, <ranges>
#define USE_CPP_STRINGS 1	 //   <string>, <string_view>, <regex>, <format>
#define USE_CPP_MEMORY 1	 //   <memory>, <scoped_allocator>
#define USE_CPP_UTILS 1		 //   <utility>, <functional>, <chrono>, <random>, <filesystem>, <bit>
#define USE_CPP_TYPES 1 //   <any>, <optional>, <variant>, <type_traits>, <typeinfo>, <concepts>
#define USE_CPP_CONCURRENCY                                                                        \
	1 //   <thread>, <mutex>, <atomic>, <future>, <semaphore>, <latch>, <barrier>, ...
#define USE_CPP_MATH_DIAG 1 //   <complex>, <valarray>, <bitset>, <stdexcept>, <exception>

// Propagação de USE_CPP_ALL
#if USE_CPP_ALL
#undef USE_CPP
#define USE_CPP 1
#undef USE_CPP_IO
#define USE_CPP_IO 1
#undef USE_CPP_CONTAINERS
#define USE_CPP_CONTAINERS 1
#undef USE_CPP_ALGORITHMS
#define USE_CPP_ALGORITHMS 1
#undef USE_CPP_STRINGS
#define USE_CPP_STRINGS 1
#undef USE_CPP_MEMORY
#define USE_CPP_MEMORY 1
#undef USE_CPP_UTILS
#define USE_CPP_UTILS 1
#undef USE_CPP_TYPES
#define USE_CPP_TYPES 1
#undef USE_CPP_CONCURRENCY
#define USE_CPP_CONCURRENCY 1
#undef USE_CPP_MATH_DIAG
#define USE_CPP_MATH_DIAG 1
#endif // USE_CPP_ALL


// ----------------------------------------------------------------------------
// [5] PLATAFORMA
// ----------------------------------------------------------------------------
#define USE_WINDOWS 1 // Windows API (windows.h, winrt, wil, etc.)
#define USE_WINRT 1	  // C++/WinRT (requer USE_WINDOWS)
#define USE_WIL 1	  // Windows Implementation Library (requer USE_WINDOWS)

// ----------------------------------------------------------------------------
// [6] RENDERIZAÇÃO / GPU
// ----------------------------------------------------------------------------
#define USE_DIRECTX11 1 // Direct3D 11
#define USE_DIRECTX12 1 // Direct3D 12
#define USE_VULKAN 1	// Vulkan SDK + VMA
#define USE_OPENGL 0	// OpenGL (legado)
#define USE_SDL3 1		// SDL3 (janela/input/GPU)

// ----------------------------------------------------------------------------
// [7] UI / DEBUG VISUAL
// ----------------------------------------------------------------------------
#define USE_IMGUI 1				// Dear ImGui
#define USE_IMPLOT 1			// ImPlot (requer USE_IMGUI)
#define USE_IMPLOT3D 0			// ImPlot3D (requer USE_IMGUI + USE_IMPLOT)
#define USE_IMGUI_TEST_ENGINE 0 // ImGui Test Engine (requer USE_IMGUI)

// ----------------------------------------------------------------------------
// [8] FONTES / GRÁFICOS 2D
// ----------------------------------------------------------------------------
#define USE_FREETYPE 1	// FreeType (rasterização de fontes)
#define USE_PLUTO_SVG 0 // PlutoSVG + PlutoVG

// ----------------------------------------------------------------------------
// [9] IMAGEM / TEXTURA
// ----------------------------------------------------------------------------
#define USE_STB 0 // stb_image, stb_image_write, etc.

// ----------------------------------------------------------------------------
// [10] SERIALIZAÇÃO / JSON
// ----------------------------------------------------------------------------
#define USE_REFLECT_CPP 1	// reflect-cpp (serialização reflexiva)
#define USE_NLOHMANN_JSON 1 // nlohmann/json

// ----------------------------------------------------------------------------
// [11] UTILITÁRIOS
// ----------------------------------------------------------------------------
#define USE_TERMCOLOR 1	 // termcolor (saída colorida no terminal)
#define USE_FMT 0		 // {fmt} (formatação de strings)
#define USE_CONV_WCHAR 1 // Helpers Win32 de conversão char <-> wchar_t

// ----------------------------------------------------------------------------
// [12] FERRAMENTAS DE DIAGNÓSTICO
// ----------------------------------------------------------------------------
#define USE_RENDERDOC 0 // RenderDoc API (captura de frames GPU)


// ============================================================================
// COMPATIBILIDADE E ENCODING
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UTF8
#define UTF8
#endif
#ifndef _UTF8
#define _UTF8
#endif
#ifndef _MBCS
#define _MBCS
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#endif

#ifdef _MSC_VER
#define INLINE __forceinline
#else
#define INLINE inline
#endif


// ============================================================================
// WINDOWS API
// ============================================================================

#if USE_WINDOWS && defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// --- Core Win32 ---
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4005)
#include <ntstatus.h>
#pragma warning(pop)
#include <winternl.h>
#include <winbase.h>
#include <winuser.h>
#include <sal.h>
#include <stringapiset.h>
#include <winioctl.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>


// --- CRT / Runtime ---
#include <corecrt.h>		// _countof, _vsnprintf_s, _vscprintf
#include <corecrt_wstdio.h> // _vscwprintf, vswprintf_s

// --- Dispositivos / Hardware ---
#include <setupapi.h>
#include <initguid.h> // Deve vir ANTES dos headers que usam GUID
#include <devguid.h>
#include <Ntddvdeo.h>

// --- Shell / COM ---
#include <shobjidl.h>	// IFileOpenDialog, IShellItemArray, IShellItem
#include <combaseapi.h> // CoInitializeEx, CoUninitialize
#include <consoleapi.h>
#include <consoleapi2.h>
#include <consoleapi3.h>

// --- Bibliotecas ---
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")



// --- WinRT (C++/WinRT) ---
#if USE_WINRT
#include <winrt/base.h>
#pragma comment(lib, "windowsapp")
#endif // USE_WINRT

// --- WIL (Windows Implementation Library) ---
#if USE_WIL
#include <wil/resource.h>
#endif // USE_WIL

#endif // USE_WINDOWS && _WIN32


// ============================================================================
// DIRECT3D 11
// ============================================================================

#if USE_DIRECTX11 && defined(_WIN32)

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "DirectXTK.lib")

#endif // USE_DIRECTX11


// ============================================================================
// DIRECT3D 12
// ============================================================================

#if USE_DIRECTX12 && defined(_WIN32)

#include <d3dx12/d3dx12.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "DirectXTK12.lib")
#pragma comment(lib, "DirectXTK12_Spectre.lib")

#endif // USE_DIRECTX12


// --- DXGI (compartilhado entre DX11 / DX12) ---
#if (USE_DIRECTX11 || USE_DIRECTX12) && defined(_WIN32)

#include <dxgi1_4.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")

#endif // USE_DIRECTX11 || USE_DIRECTX12


// ============================================================================
// VULKAN
// ============================================================================

#if USE_VULKAN
//#define VOLK_IMPLEMENTATION
//#define VK_NO_PROTOTYPES
//#define VK_ONLY_EXPORTED_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR


#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_win32.h>
#include <vma/vk_mem_alloc.h>
//#include <Volk/volk.h>
#pragma comment(lib, "vulkan-1.lib")
//#pragma comment(lib, "volk.lib")
//#pragma comment(lib, "volkd.lib")

#endif // USE_VULKAN


// ============================================================================
// OPENGL
// ============================================================================

#if USE_OPENGL

#include <GL/gl.h>
#pragma comment(lib, "opengl32.lib")

#endif // USE_OPENGL


// ============================================================================
// SDL3
// ============================================================================

#if USE_SDL3

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "SDL3-static.lib")

#endif // USE_SDL3


// ============================================================================
// RENDERDOC
// ============================================================================

#if USE_RENDERDOC

#include <renderdoc_app.h>
#pragma comment(lib, "renderdoc.lib")

#endif // USE_RENDERDOC


// ============================================================================
// C STANDARD LIBRARY  (headers clássicos — estilo .h)
// ============================================================================

#if USE_CLASSIC_C

#if USE_CLASSIC_C_IO
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#endif // USE_CLASSIC_C_IO

#if USE_CLASSIC_C_TEXT
#include <ctype.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#endif // USE_CLASSIC_C_TEXT

#if USE_CLASSIC_C_MATH
#include <math.h>
#include <float.h>
#include <limits.h>
#include <complex.h>
#include <fenv.h>
#include <inttypes.h>
#include <stdint.h>
#include <tgmath.h>
#endif // USE_CLASSIC_C_MATH

#if USE_CLASSIC_C_DIAG
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#endif // USE_CLASSIC_C_DIAG

#if USE_CLASSIC_C_MISC
#include <stdarg.h>
#include <stddef.h>
#include <iso646.h>
#endif // USE_CLASSIC_C_MISC

#if USE_CLASSIC_C_C99
#include <stdbool.h>
#endif // USE_CLASSIC_C_C99

#if USE_CLASSIC_C_C11
#include <stdalign.h>
#include <stdatomic.h>
#include <stdnoreturn.h>
#include <threads.h>
#include <uchar.h>
#endif // USE_CLASSIC_C_C11

#endif // USE_CLASSIC_C


// ============================================================================
// C STANDARD LIBRARY  (wrappers C++ — estilo <cXXX>)
// ============================================================================

#if USE_STL_C

#if USE_STL_C_IO
#include <cstdio>
#include <cstdlib>
#include <ctime>
#endif // USE_STL_C_IO

#if USE_STL_C_TEXT
#include <cctype>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <clocale>
#endif // USE_STL_C_TEXT

#if USE_STL_C_MATH
#include <cmath>
#include <cfloat>
#include <climits>
#include <ccomplex>
#include <cfenv>
#include <cstdint>
#include <ctgmath>
#endif // USE_STL_C_MATH

#if USE_STL_C_DIAG
#include <cassert>
#include <cerrno>
#include <csignal>
#include <csetjmp>
#endif // USE_STL_C_DIAG

#if USE_STL_C_MISC
#include <cstdarg>
#include <cstddef>
#endif // USE_STL_C_MISC

#endif // USE_STL_C


// ============================================================================
// C++ STANDARD LIBRARY
// ============================================================================

#if USE_CPP

#if USE_CPP_IO
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#endif // USE_CPP_IO

#if USE_CPP_CONTAINERS
#include <vector>
#include <array>
#include <deque>
#include <list>
#include <forward_list>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <stack>
#include <span> // C++20
#endif			// USE_CPP_CONTAINERS

#if USE_CPP_ALGORITHMS
#include <algorithm>
#include <numeric>
#include <iterator>
#include <ranges> // C++20
#endif			  // USE_CPP_ALGORITHMS

#if USE_CPP_STRINGS
#include <string>
#include <string_view> // C++17
#include <regex>
#include <format> // C++20
#endif			  // USE_CPP_STRINGS

#if USE_CPP_MEMORY
#include <memory>
#include <scoped_allocator>
#endif // USE_CPP_MEMORY

#if USE_CPP_UTILS
#include <utility>
#include <functional>
#include <chrono>
#include <random>
#include <filesystem> // C++17
#include <bit>		  // C++20
#endif				  // USE_CPP_UTILS

#if USE_CPP_TYPES
#include <any>		// C++17
#include <optional> // C++17
#include <variant>	// C++17
#include <type_traits>
#include <typeinfo>
#include <concepts> // C++20
#endif				// USE_CPP_TYPES

#if USE_CPP_CONCURRENCY
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <condition_variable>
#include <semaphore> // C++20
#include <latch>	 // C++20
#include <barrier>	 // C++20
#endif				 // USE_CPP_CONCURRENCY

#if USE_CPP_MATH_DIAG
#include <complex>
#include <valarray>
#include <bitset>
#include <stdexcept>
#include <exception>
#endif // USE_CPP_MATH_DIAG

#endif // USE_CPP


// ============================================================================
// DEAR IMGUI
// ============================================================================

#if USE_IMGUI

#if !USE_SDL3
#error "USE_IMGUI requer USE_SDL3. Defina USE_SDL3 como 1."
#endif
#if !USE_VULKAN
#error "USE_IMGUI requer USE_VULKAN. Defina USE_VULKAN como 1."
#endif

#include "imconfig.h"
#include "imgui_user.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_freetype.h>
// --- Backend SDL3 ---
#include <imgui_impl_sdl3.h>

// --- Backend Vulkan ---
//#define VK_USE_PLATFORM_WIN32_KHR
#define APP_USE_VULKAN_DEBUG_REPORT
//#define IMGUI_IMPL_VULKAN_USE_VOLK
#include <imgui_impl_vulkan.h>
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#include <Volk/volk.h>
#endif

// --- Backend Win32 (opcional) ---
#if USE_WINDOWS && defined(_WIN32)
#include <imgui_impl_win32.h>
#endif

#endif // USE_IMGUI


// ============================================================================
// IMGUI TEST ENGINE
// ============================================================================

#if USE_IMGUI_TEST_ENGINE

#if !USE_IMGUI
#error "USE_IMGUI_TEST_ENGINE requer USE_IMGUI. Defina USE_IMGUI como 1."
#endif

#include <imgui_te_context.h>
#include <imgui_te_coroutine.h>
#include <imgui_te_engine.h>
#include <imgui_te_imconfig.h>
#include <imgui_te_perftool.h>
#include <imgui_te_utils.h>
#include <imgui_test_suite.h>

#endif // USE_IMGUI_TEST_ENGINE


// ============================================================================
// IMPLOT
// ============================================================================

#if USE_IMPLOT

#if !USE_IMGUI
#error "USE_IMPLOT requer USE_IMGUI. Defina USE_IMGUI como 1."
#endif

#include <implot.h>

#endif // USE_IMPLOT


// ============================================================================
// IMPLOT 3D
// ============================================================================

#if USE_IMPLOT3D

#if !USE_IMGUI || !USE_IMPLOT
#error "USE_IMPLOT3D requer USE_IMGUI e USE_IMPLOT. Defina ambos como 1."
#endif

#include <implot3d.h>
#include <implot3d_internal.h>

#endif // USE_IMPLOT3D


// ============================================================================
// FREETYPE
// ============================================================================

#if USE_FREETYPE

#if defined(IMGUI_ENABLE_FREETYPE) && USE_IMGUI
#include <imgui_freetype.h>
#endif

#include <freetype/config/ftheader.h>
#include <freetype/freetype.h>

#endif // USE_FREETYPE


// ============================================================================
// PLUTO SVG
// ============================================================================

#if USE_PLUTO_SVG

#include "plutovg.h"
#include "plutosvg.h"
#include "plutosvg-ft.h"

#endif // USE_PLUTO_SVG


// ============================================================================
// STB  (adicione os #define de implementação no .cpp correspondente)
// ============================================================================

#if USE_STB

// Exemplo:
// #include <stb_image.h>
// #include <stb_image_write.h>
// #include <stb_image_resize2.h>
// #include <stb_truetype.h>

#endif // USE_STB


// ============================================================================
// REFLECT-CPP  (serialização reflexiva)
// ============================================================================

#if USE_REFLECT_CPP

#pragma warning(push)
#pragma warning(disable : 4324)
#pragma warning(disable : 4996)

#include <rfl/AllOf.hpp>
#include <rfl/json.hpp>

#pragma warning(pop)

#endif // USE_REFLECT_CPP


// ============================================================================
// NLOHMANN JSON
// ============================================================================

#if USE_NLOHMANN_JSON

#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"

#endif // USE_NLOHMANN_JSON


// ============================================================================
// TERMCOLOR
// ============================================================================

#if USE_TERMCOLOR

#include "termcolor.hpp"

#endif // USE_TERMCOLOR


// ============================================================================
// {fmt}
// ============================================================================

#if USE_FMT

#define FMT_UNICODE 1
#include "fmt/core.h"
#include "fmt/format.h"
#include "fmt/printf.h"

#endif // USE_FMT


// ============================================================================
// HELPERS  Win32 — Conversão char <-> wchar_t
// ============================================================================

#if USE_CONV_WCHAR && USE_WINDOWS && defined(_WIN32)

#pragma warning(push)
#pragma warning(disable : 4267)
#pragma warning(disable : 4365)

// --- Get_Len ---

[[nodiscard]] INLINE static size_t Get_Len(const char* s) noexcept {
	return static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0));
}

[[nodiscard]] INLINE static size_t Get_Len(const std::string& s) noexcept {
	return static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0));
}

[[nodiscard]] INLINE static size_t Get_Len(const wchar_t* s) noexcept {
	return static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL));
}

[[nodiscard]] INLINE static size_t Get_Len(const std::wstring& s) noexcept {
	return static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL));
}

// --- char* / std::string  →  wchar_t* ---

[[nodiscard]] INLINE static const wchar_t* Win_char_to_Wchar(const char* s) {
	if (s == nullptr || *s == '\0') {
		constexpr static const wchar_t* err = L"error";
		return err;
	}
	const static size_t len = Get_Len(s);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);
	return ws.c_str();
}

[[nodiscard]] INLINE static const wchar_t* Win_char_to_Wchar(const std::string& s) {
	if (s.empty()) {
		constexpr static const wchar_t* err = L"error";
		return err;
	}
	const static size_t len = Get_Len(s.c_str());
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
	return ws.c_str();
}

// --- char* / std::string  →  std::wstring ---

[[nodiscard]] INLINE static const std::wstring& Win_str_to_Wchar(const char* s) {
	if (s == NULL || *s == '\0') {
		static const std::wstring err(L"Error");
		return err;
	}
	const static size_t len = Get_Len(s);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);
	return ws;
}

[[nodiscard]] INLINE static const std::wstring& Win_str_to_Wchar(const std::string& s) {
	if (s.empty()) {
		static const std::wstring err(L"Error");
		return err;
	}
	const static size_t len = Get_Len(s.c_str());
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
	return ws;
}

// --- wchar_t* / std::wstring  →  std::string ---

[[nodiscard]] INLINE static const std::string& Win_wstr_to_str(const wchar_t* wstr) {
	if (wstr == NULL || *wstr == '\0') {
		static const std::string& err("Error");
		return err;
	}
	static size_t	   size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	static std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
	return strTo;
}

[[nodiscard]] INLINE static const std::string& Win_wstr_to_str(const std::wstring& wstr) {
	if (wstr.empty()) {
		static const std::string& err("Error");
		return err;
	}
	static size_t size_needed = WideCharToMultiByte(
		CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
	static std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], size_needed,
						NULL, NULL);
	return strTo;
}

// --- wchar_t* / std::wstring  →  const char* ---

[[nodiscard]] INLINE static const char* Win_Wchar_to_char(const wchar_t* wstr) {
	if (wstr == nullptr || *wstr == '\0') return "error";
	const static size_t size_needed = Get_Len(wstr);
	static std::string	strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
	static const char* result = strTo.c_str();
	return result;
}

[[nodiscard]] INLINE static const char* Win_Wchar_to_char(const std::wstring& wstr) {
	if (wstr.empty()) return "error";
	const static size_t size_needed = Get_Len(wstr);
	static std::string	strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], size_needed,
						NULL, NULL);
	static const char* result = strTo.c_str();
	return result;
}

// --- Macros de conveniência ---
#define ToWStr(s) Win_char_to_Wchar(s)
#define StrToWStr(s) Win_str_to_Wchar(s)
#define WstrToStr(s) Win_wstr_to_str(s)
#define WcharToStr(s) Win_Wchar_to_char(s)

#pragma warning(pop)

#endif // USE_CONV_WCHAR


// ============================================================================
// MACRO DE LITERAL WIDE STRING
// ============================================================================

#define TXT(msg) L##msg