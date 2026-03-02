#pragma once

#define CLASSIC_C 1
#define STL_C 1
#define NO_WINDOWS 0
#define SDL3 1
#define DIRECTX11 1
#define DIRECTX12 1
#define VULKAN 1
#define OPENGL 0
#define IMGUI 1
#define IMPLOT 1
#define PLUTO_SVG 1
#define IMGUI_ENABLE_TEST_ENGINE 0
#define FREETYPE 1
#define REFLECT_CPP 1
#define NLOHMANN_JSON 1
#define TERMCOLOR 1
#define FMT 1
#define CONVWCHARWIN32 1
#define MSVC_INLINE 1

// ============================================================================
// Windows
// ============================================================================

#if NO_WINDOWS
#undef _WIN32
#undef WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4005)
#include <ntstatus.h>
#pragma warning(pop)
#include <winternl.h>
// SetupAPI para enumerar dispositivos de armazenamento
#include <setupapi.h>
// DeviceIoControl para tipo de disco e capacidade
#include <sal.h>
#include <stringapiset.h>
#include <winioctl.h>

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

// ============================================================================
// DirectX 11
// ============================================================================

#if DIRECTX11
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#endif //DIRECTX11

#if DIRECTX12
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#endif //DIRECTX12

#if defined(DIRECTX11) || defined(DIRECTX12)
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#endif //DIRECTX11 || DIRECTX12

#endif //_WIN32

// ============================================================================
// VULKAN
// ============================================================================

#if VULKAN
#include <vulkan\vulkan.h>
#include <vulkan\vulkan.hpp>
#include <vma\vk_mem_alloc.h>
#pragma comment(lib, "vulkan-1.lib")

#endif

#if OPENGL
#include <GL/gl.h>
#pragma comment(lib,"opengl32.lib")
#endif

//#include <vma\vk_mem_alloc.h>
//#include <vulkan-memory-allocator-hpp\vk_mem_alloc.hpp>

// ============================================================================
// C Standard Library
// ============================================================================

#if CLASSIC_C

#include <assert.h> // Diagnósticos e asserções
#include <ctype.h>  // Classificação de caracteres
#include <errno.h>  // Teste de códigos de erro
#include <float.h>  // Limites de tipos de ponto flutuante
#include <limits.h> // Limites de tamanhos de tipos inteiros
#include <locale.h> // Funções de localização (idioma/moeda)
#include <math.h>   // Funções matemáticas comuns
#include <setjmp.h> // Pulos não-locais (controle de fluxo)
#include <signal.h> // Manipulação de sinais (interrupções)
#include <stdarg.h> // Argumentos variáveis em funções
#include <stddef.h> // Definições padrão (size_t, NULL)
#include <stdio.h>  // Entrada e saída padrão (printf, scanf)
#include <string.h> // Manipulação de strings
#include <time.h>   // Funções de data e hora

/* --- C94 / C95 --- */
#include <iso646.h> // Macros para operadores lógicos
#include <wchar.h>  // Suporte a caracteres largos (Wide characters)
#include <wctype.h> // Classificação de caracteres largos

/* --- C99 (Novas bibliotecas) --- */
#include <complex.h>  // Aritmética de números complexos
#include <fenv.h>     // Acesso ao ambiente de ponto flutuante
#include <inttypes.h> // Formatação de tipos inteiros de tamanho fixo
#include <stdbool.h>  // Tipo de dado booleano (_Bool)
#include <stdint.h>   // Tipos inteiros com larguras exatas
#include <tgmath.h>   // Macros matemáticas genéricas (Type-generic)

/* --- C11 (Suporte a threads e novas utilidades) --- */
#include <stdalign.h>    // Alinhamento de estruturas
#include <stdatomic.h>   // Operações atômicas para concorrência
#include <stdnoreturn.h> // Especificador de função que não retorna
#include <threads.h>     // Biblioteca de threads padrão (pode exigir C11+)
#include <uchar.h>       // Manipulação de caracteres Unicode

#endif // CLASSIC_C

#ifdef __cplusplus
#if STL_C
// --- I/O e Sistema ---
#include <cstdio>  // Antigo stdio.h (printf, scanf, arquivos)
#include <cstdlib> // Antigo stdlib.h (exit, malloc, rand)
#include <ctime>   // Antigo time.h (manipulação de data/hora)

// --- Manipulação de Texto e Caracteres ---
#include <cctype>  // Antigo ctype.h (isupper, isalpha, tolower)
#include <cstring> // Antigo string.h (strlen, strcpy, memcpy)
#include <cwchar>  // Antigo wchar.h (caracteres largos/unicode)
#include <cwctype> // Antigo wctype.h (classificação de caracteres largos)

// --- Matemática e Números ---
#include <ccomplex> // Antigo complex.h (números complexos)
#include <cfenv>    // Antigo fenv.h (controle de ponto flutuante)
#include <cfloat>   // Antigo float.h (limites de tipos flutuantes)
#include <climits>  // Antigo limits.h (limites de tipos inteiros)
#include <cmath>    // Antigo math.h (sin, cos, sqrt, pow)
#include <cstdint>  // Antigo stdint.h (tipos como int32_t, uint64_t)
#include <ctgmath>  // Antigo tgmath.h (matemática tipo-genérica)

// --- Diagnóstico e Erros ---
#include <cassert> // Antigo assert.h (macros de depuração)
#include <cerrno>  // Antigo errno.h (códigos de erro do sistema)
#include <csetjmp> // Antigo setjmp.h (pulos de execução não locais)
#include <csignal> // Antigo signal.h (sinais de interrupção)

// --- Localização e Argumentos Variáveis ---
#include <clocale> // Antigo locale.h (configurações regionais)
#include <cstdarg> // Antigo stdarg.h (funções com (...) argumentos)
#include <cstddef> // Antigo stddef.h (definição de NULL, size_t, ptrdiff_t)
#endif // STL_C	

// ============================================================================
// C++ Standard Library
// ============================================================================
/* --- Entrada e Saída (I/O) --- */
#include <cstdio>   // printf/scanf (versão C)
#include <fstream>  // Manipulação de arquivos
#include <iomanip>  // Manipuladores de formatação
#include <iostream> // Fluxos padrão (cin, cout)
#include <sstream>  // Fluxos de strings

/* --- Containers (Estruturas de Dados) --- */
#include <array>         // Array de tamanho fixo (C++11)
#include <deque>         // Fila de duas extremidades
#include <forward_list>  // Lista ligada simples (C++11)
#include <list>          // Lista duplamente ligada
#include <map>           // Dicionário ordenado (Chave/Valor)
#include <queue>         // Fila (FIFO) e priority_queue
#include <set>           // Conjunto ordenado (Árvore)
#include <span>          // Visualização de sequências (C++20)
#include <stack>         // Pilha (LIFO)
#include <unordered_map> // Dicionário usando Hash (C++11)
#include <unordered_set> // Conjunto usando Hash (C++11)
#include <vector>        // Array dinâmico

/* --- Algoritmos e Utilitários --- */
#include <algorithm>  // sort, find, copy, etc.
#include <chrono>     // Manipulação de tempo (C++11)
#include <functional> // Objetos de função e std::function
#include <iterator>   // Iteradores
#include <numeric>    // Operações numéricas (accumulate, iota)
#include <random>     // Geração de números aleatórios (C++11)
#include <ranges>     // Biblioteca de ranges (C++20)
#include <utility>    // std::pair, std::make_pair, std::move

/* --- Strings e Texto --- */
#include <format>      // Formatação estilo Python (C++20)
#include <regex>       // Expressões regulares (C++11)
#include <string>      // Classe std::string
#include <string_view> // Visualização leve de strings (C++17)

/* --- Gerenciamento de Memória e Smart Pointers --- */
#include <memory> // unique_ptr, shared_ptr, allocator
#include <scoped_allocator>

/* --- Multithreading e Concorrência (C++11+) --- */
#include <atomic>  // Operações atômicas
#include <barrier> // Barreiras (C++20)
#include <condition_variable>
#include <future>    // Operações assíncronas (async, promise)
#include <latch>     // Trincos (C++20)
#include <mutex>     // Sincronização (travas)
#include <semaphore> // Semáforos (C++20)
#include <thread>    // Criação de threads

/* --- Metaprogramação e Tipos --- */
#include <any>         // Tipo que pode guardar qualquer valor (C++17)
#include <concepts>    // Restrições para templates (C++20)
#include <exception>   // Manipulação de exceções
#include <optional>    // Valores que podem ou não existir (C++17)
#include <type_traits> // Propriedades de tipos em tempo de compilação
#include <typeinfo>    // Informações de tipo em tempo de execução (RTTI)
#include <variant>     // Uniões seguras (C++17)

/* --- Matemáticos e Diagnósticos --- */
#include <bitset>    // Manipulação de bits de tamanho fixo
#include <cassert>   // Macros de asserção (C)
#include <cmath>     // Funções matemáticas
#include <complex>   // Números complexos
#include <stdexcept> // Exceções padrão (runtime_error, logic_error)
#include <valarray>  // Arrays otimizados para computação vetorial

#endif // __cplusplus

// ============================================================================
// SDL3
// ============================================================================

#if SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "SDL3-static.lib")
#endif // SDL3

// ============================================================================
// ImGui
// ============================================================================

#if IMGUI

#include "imconfig.h" // ImGui configuration (e.g. #define IMGUI_IMPL_VULKAN_USE_VOLK)
#include "imgui_user.h" // Configurações personalizadas do ImGui (estilo, cores, etc.)

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#if SDL3
#include <imgui_impl_sdl3.h>
#endif // SDL3
#if VULKAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <imgui_impl_vulkan.h>

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#include <volk.h>
#endif // IMGUI_IMPL_VULKAN_USE_VOLK
#endif //VULKAN
#ifdef _WIN32
#include <imgui_impl_win32.h>
#endif // _WIN32


// ============================================================================
// ImGui test engine (ferramenta de captura de UI para testes automatizados)
// ============================================================================

#if IMGUI_ENABLE_TEST_ENGINE
// Headers específicos da Test Engine
#include <imgui_te_context.h>
#include <imgui_te_coroutine.h>
#include <imgui_te_engine.h>
#include <imgui_te_imconfig.h>
#include <imgui_te_perftool.h>
#include <imgui_te_utils.h>
#include <imgui_test_suite.h>
#endif // IMGUI_ENABLE_TEST_ENGINE

// ============================================================================
// implot
// ============================================================================

#if IMPLOT
#include <implot.h>
#include <implot3d.h>
#endif // IMPLOT

// ============================================================================
// pluto
// ============================================================================

#if PLUTO_SVG

#ifdef IMGUI_ENABLE_FREETYPE_PLUTOSVG

#include "plutosvg-ft.h"
#include "plutosvg.h"
#include "plutovg.h"

#endif // IMGUI_ENABLE_FREETYPE_PLUTOSVG

#endif // PLUTO_SVG

// ============================================================================
// freetype
// ============================================================================

#if FREETYPE

#ifdef IMGUI_ENABLE_FREETYPE

#include <imgui_freetype.h>
#include <freetype/config/ftheader.h>
#include <freetype/freetype.h>

#endif // IMGUI_ENABLE_FREETYPE
#endif // FREETYPE
#endif // IMGUI

// ============================================================================
// reflect-cpp (serialização JSON)
// ============================================================================

#if REFLECT_CPP

#pragma warning(push)
#pragma warning(disable : 4324)                                                      
#pragma warning(disable : 4996)                                                        

#include <rfl\AllOf.hpp>
#include <rfl\json.hpp>

#pragma warning(pop)

#endif // REFLECT_CPP

// ============================================================================
// nlohmann-json
// ============================================================================

#if NLOHMANN_JSON
#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"
#endif // NLOHMANN_JSON

// ============================================================================
// termcolor
// ============================================================================

#if TERMCOLOR

#include "termcolor.hpp"

#endif // TERMCOLOR

// ============================================================================
// fmt
// ============================================================================

#if FMT

#include "fmt/core.h"
#include "fmt/format.h"
#include "fmt/printf.h"

#endif // FMT


#if MSVC_INLINE
#define INLINE __forceinline
#endif // MSVC_INLINE

#if !MSVC_INLINE
#define INLINE inline
#endif // !MSVC_INLINE

#if CONVWCHARWIN32

INLINE static const wchar_t* WinWiden(const std::string& s) {
	if(s.empty()) {
		constexpr static const wchar_t* empty = L"";
		return empty;
	}
	static int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
	return ws.c_str();
}

INLINE static const wchar_t* WinWiden(const char* s) {
	if(s == nullptr || *s == '\0') {
		constexpr static const wchar_t* empty = L"";
		return empty;
	}

	static int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);

	return ws.c_str();
}

INLINE static const std::wstring& StrWinWiden(const std::string& e) {
	if(e.empty()) {
		static const std::wstring& empty = L"";
		return empty;
	}

	static int len = MultiByteToWideChar(CP_UTF8, 0, e.c_str(), -1, NULL, 0);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, e.c_str(), -1, &ws[0], len);
	static std::wstring result = ws;
	return result;
}

INLINE static const std::wstring& StrWinWiden(const char* s) {
	if(s == nullptr || *s == '\0') {
		const static std::wstring empty = L"";
		return empty;
	}

	static int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	static std::wstring ws(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);

	return ws;
}

#define ToWStr(s) WinWiden(s)
#define StrToWStr(s) StrWinWiden(s)

#endif // CONVWCHARWIN32

#define TXT(msg) L##msg
