#pragma once

// ============================================================================
// Windows
// ============================================================================
#define IMGUI_FONTS_FOLDER                                                     \
  "D:\\source\\cpp\\My imgui Project\\external\\imgui-docking\\misc\\fonts"
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
// DirectX
// ============================================================================

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#endif //_WIN32

// ============================================================================
// VULKAN
// ============================================================================
#include <vulkan\vulkan.h>
#include <vulkan\vulkan.hpp>

#pragma comment(lib, "vulkan-1.lib")
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#include <volk.h>
#endif

#include <vma\vk_mem_alloc.h>
#include <vulkan-memory-allocator-hpp\vk_mem_alloc.hpp>

// ============================================================================
// C Standard Library
// ============================================================================
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
#include <stdlib.h> // Utilidades gerais (malloc, free, rand)
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

#ifdef __cplusplus

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
//
 #include <SDL3/SDL.h>
// #include <SDL3/SDL_vulkan.h>
 #include <SDL3/SDL_video.h>
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "SDL3-static.lib")

// ============================================================================
// ImGui
// ============================================================================

#include "imconfig.h" // ImGui configuration (e.g. #define IMGUI_IMPL_VULKAN_USE_VOLK)
#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

// ============================================================================
// ImGui test engine (ferramenta de captura de UI para testes automatizados)
// ============================================================================

// Headers específicos da Test Engine
#include <imgui_te_context.h>
#include <imgui_te_coroutine.h>
#include <imgui_te_engine.h>
#include <imgui_te_imconfig.h>
#include <imgui_te_perftool.h>
#include <imgui_te_utils.h>
#include <imgui_test_suite.h>

// ============================================================================
// reflect-cpp (serialização JSON)
// ============================================================================

#pragma warning(push)
#pragma warning(disable                                                        \
                : 4324) // structure was padded due to alignment specifier
#pragma warning(disable                                                        \
                : 4996) // 'function': This function or variable may be unsafe

#include "rfl.hpp"
#include "rfl/AllOf.hpp"
#include "rfl/json.hpp"

#pragma warning(pop)

// ============================================================================
// nlohmann-json
// ============================================================================

#include "nlohmann/json.hpp"

// ============================================================================
// implot
// ============================================================================

#include "implot.h"
#include "implot_internal.h"

// ============================================================================
// freetype
// ============================================================================

#define FT2_BUILD_LIBRARY
#include <freetype/config/ftconfig.h>
#include <freetype/freetype.h>
#include <ft2build.h>

// ============================================================================
// pluto
// ============================================================================

#include "plutosvg/plutosvg-ft.h"
#include "plutosvg/plutosvg.h"
#include "plutovg/plutovg.h"


inline static const wchar_t *WinWiden(const std::string &s) {
  if (s.empty())
    return L"";
  static int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
  static std::wstring ws(len, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
  return ws.c_str();
}

inline static const wchar_t *WinWiden(const char *s) {
  if (s == nullptr || *s == '\0')
    return L"";

  static int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  static std::wstring ws(len, 0);
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);

  return ws.c_str();
}

inline static const std::wstring &StrWinWiden(const std::string &e) {
  if (e.empty())
    return L"";

  static int len = MultiByteToWideChar(CP_UTF8, 0, e.c_str(), -1, NULL, 0);
  static std::wstring ws(len, 0);
  MultiByteToWideChar(CP_UTF8, 0, e.c_str(), -1, &ws[0], len);
  static std::wstring result = ws;
  return result;
}

inline static const std::wstring &StrWinWiden(const char *s) {
  if (s == nullptr || *s == '\0')
    return L"";


  static int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  static std::wstring ws(len, 0);
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], len);

  return ws.c_str();
}

#define ToWStr(s) WinWiden(s)
#define StrToWStr(s) StrWinWiden(s)

#define TXT(msg) L##msg

// ============================================================================
// Projeto
// ============================================================================

#include "MyResult.hpp"
#include "AppSettings.hpp"
#include "Console.hpp"
#include "EmojiDebugHelper.h"
#include "FontManager.hpp"
#include "FontScale.hpp"
#include "Image.hpp"
#include "Memory.hpp"
#include "MenuBar.hpp"
#include "MicaTheme.h"
#include "StyleEditor.hpp"
#include "SystemInfo.hpp"
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "WindowsConsole.hpp"
#include "App.hpp"

