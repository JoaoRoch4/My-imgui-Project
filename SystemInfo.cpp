/**
 * @file SystemInfo.cpp
 * @brief Coleta e exibe especificações completas de hardware, APIs gráficas e sistema.
 *
 * SEÇÕES COLETADAS
 * ----------------
 *  CPU           GetSystemInfo() + GetLogicalProcessorInformation() + Registro
 *  RAM           GlobalMemoryStatusEx() → física + paginação
 *  SO            RtlGetVersion() via ntdll.dll
 *  GPU (DXGI)    IDXGIFactory1 → nome, VRAM dedicada, VRAM compartilhada
 *  DX12 Ultimate D3D12 feature checks (DXR, VRS, Mesh Shaders, Sampler Feedback)
 *  Vulkan        vkEnumerateInstanceVersion + vkGetPhysicalDeviceProperties
 *  DirectX       D3D12CreateDevice + D3D11CreateDevice via LoadLibrary
 *  OpenGL        Janela oculta temporária + WGL context → glGetString
 *  Discos        SetupAPI (SetupDiGetClassDevsW) + DeviceIoControl → tipo (SSD/HDD)
 *  Monitores     EnumDisplayDevices + SetupDiGetClassDevsW + EDID do Registro
 *  Máquina       Registro BIOS + GetComputerNameExW → fabricante, modelo, nome
 *
 * NÚCLEOS FÍSICOS vs LÓGICOS
 * --------------------------
 * GetSystemInfo() retorna dwNumberOfProcessors = núcleos LÓGICOS (com hyperthreading).
 * Para núcleos FÍSICOS usamos GetLogicalProcessorInformation():
 *   Iteramos a lista de SYSTEM_LOGICAL_PROCESSOR_INFORMATION procurando entradas
 *   com Relationship == RelationProcessorCore. Cada entrada representa UM núcleo físico,
 *   independente de quantas threads (HT) ele expõe.
 *   Núcleos lógicos = soma de ProcessorMask bits por entrada de RelationProcessorCore.
 *
 * DX12 ULTIMATE
 * -------------
 * DirectX 12 Ultimate é um conjunto de 4 features que a Microsoft definiu em 2020:
 *   1. DirectX Raytracing (DXR) Tier 1.1
 *      → D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier >= D3D12_RAYTRACING_TIER_1_1
 *   2. Variable Rate Shading (VRS) Tier 2
 *      → D3D12_FEATURE_DATA_D3D12_OPTIONS6::VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2
 *   3. Mesh Shaders
 *      → D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier >= D3D12_MESH_SHADER_TIER_1
 *   4. Sampler Feedback
 *      → D3D12_FEATURE_DATA_D3D12_OPTIONS7::SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9
 *   Todas as 4 devem ser verdadeiras para o rótulo "DX12 Ultimate".
 *   GPUs que qualificam: NVIDIA RTX 20xx/30xx/40xx, AMD RX 6000/7000.
 *   D3D12_FEATURE_DATA_D3D12_OPTIONS5/6/7 são structs da D3D12 que consultamos via
 *   ID3D12Device::CheckFeatureSupport() sem criar swapchain.
 *
 * DETECÇÃO DE DISCOS (SSD vs HDD)
 * --------------------------------
 * Usamos DeviceIoControl com IOCTL_STORAGE_QUERY_PROPERTY e StorageDeviceSeekPenaltyProperty:
 *   DEVICE_SEEK_PENALTY_DESCRIPTOR::IncursSeekPenalty == FALSE → SSD (sem seek penalty)
 *   DEVICE_SEEK_PENALTY_DESCRIPTOR::IncursSeekPenalty == TRUE  → HDD (tem seek penalty)
 * Para o nome amigável usamos IOCTL_STORAGE_QUERY_PROPERTY com StorageDeviceProperty
 * (STORAGE_DEVICE_DESCRIPTOR → ProductId).
 * Para o tamanho usamos IOCTL_DISK_GET_LENGTH_INFO.
 * Abrimos \\\\.\\PhysicalDrive0, \\.\PhysicalDrive1, ... até falhar.
 *
 * DETECÇÃO DE MONITORES
 * ---------------------
 * A detecção de monitores combina três fontes de dados:
 *
 *  1. EnumDisplayDevicesW (GDI32)
 *     Enumera todos os adaptadores de vídeo lógicos (DISPLAY1, DISPLAY2...) e
 *     para cada adaptador enumera os monitores conectados (MONITOR1, MONITOR2...).
 *     Fornece: device name (ex.: \\.\DISPLAY1\Monitor0), device string (nome genérico).
 *     O campo DeviceName do monitor é o caminho GDI usado para o segundo nível.
 *
 *  2. EnumDisplaySettingsW (GDI32)
 *     Para cada display ativo, lê DEVMODEW com:
 *       dmPelsWidth / dmPelsHeight  → resolução atual em pixels
 *       dmDisplayFrequency          → taxa de atualização em Hz
 *     O flag EDS_CURRENT retorna as configurações ativas (não as do modo listado).
 *
 *  3. SetupAPI + EDID do Registro (setupapi.h)
 *     O Windows armazena o EDID bruto de cada monitor em:
 *       HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<ID>\<instance>\Device Parameters\EDID
 *     Acessamos via SetupDiGetClassDevsW(GUID_DEVCLASS_MONITOR) + SetupDiOpenDevRegKey.
 *     Do EDID (128 bytes padronizados pela VESA) extraímos:
 *       Bytes [8-9]   → Manufacturer ID (3 letras codificadas em 5 bits cada, big-endian)
 *       Bytes [21-22] → Tamanho físico em cm (x10 = mm): width_mm, height_mm
 *       Bytes [54-125]→ 4 Descriptor Blocks de 18 bytes cada:
 *         Tag 0xFC → Monitor Name (ASCII, até 13 chars)
 *         Tag 0xFF → Serial Number (ASCII)
 *     DPI calculado como: dpi = pixels / (mm / 25.4)
 *
 *  4. MONITORINFOEXA (User32 / multimon)
 *     EnumDisplayMonitors + GetMonitorInfoA fornece o rect e o flag MONITORINFOF_PRIMARY
 *     para identificar qual é o monitor principal do sistema.
 *
 * EDID — ESTRUTURA RELEVANTE (128 bytes)
 * ---------------------------------------
 *   [0-7]   Header: 00 FF FF FF FF FF FF 00
 *   [8-9]   Manufacturer ID: 2 bytes big-endian, 3 chars A-Z (bits 14-10, 9-5, 4-0)
 *   [10-11] Product code
 *   [12-15] Serial number (numérico, diferente do descriptor de texto)
 *   [21]    Max horizontal image size em cm (0 = digital sem tamanho fixo)
 *   [22]    Max vertical image size em cm
 *   [54-71] Descriptor block 1 (18 bytes)
 *   [72-89] Descriptor block 2
 *   [90-107] Descriptor block 3
 *   [108-125] Descriptor block 4
 *   Cada descriptor: bytes [0-1]=0x0000 → extended; byte [3]=tag
 *     0xFC = nome do monitor (bytes [5-17], ASCII, terminado por 0x0A)
 *     0xFF = serial number   (bytes [5-17], ASCII)
 *
 * FABRICANTE E MODELO DO PC
 * -------------------------
 * O Windows armazena informações do BIOS/SMBIOS no Registro:
 *   HKLM\HARDWARE\DESCRIPTION\System\BIOS
 *     BaseBoardManufacturer → ex.: "ASUS" (placa-mãe)
 *     BaseBoardProduct      → ex.: "ROG STRIX Z790-E GAMING WIFI"
 *     BIOSVersion           → ex.: "American Megatrends Inc. 2601"
 *     BIOSReleaseDate       → ex.: "09/26/2023"
 *   SystemManufacturer / SystemProductName → fabricante/modelo do sistema completo
 *   (populados em notebooks Dell/HP/Lenovo, geralmente vazios em PCs montados)
 * GetComputerNameExW(ComputerNameNetBIOS) → nome do computador na rede.
 */

#include "pch.hpp"
#include "SystemInfo.hpp"
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "Console.hpp"

// Tipos OpenGL declarados manualmente (sem GL/gl.h)
typedef unsigned int  GLenum;
typedef unsigned char GLubyte;
#define GL_VERSION  0x1F02
#define GL_VENDOR   0x1F00
#define GL_RENDERER 0x1F01

// ============================================================================
// Helpers internos de SO/CPU
// ============================================================================

/**
 * @brief Lê o nome do CPU do Registro do Windows.
 * Caminho: HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString
 * @return Wide string com o nome, ou L"Desconhecido".
 */
static std::wstring ReadCpuNameFromRegistry() {
	const wchar_t* subkey =
		L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

	HKEY hkey = nullptr;
	if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
		return L"Desconhecido";

	wchar_t buf[256] = {};
	DWORD   buf_bytes = sizeof(buf); // RegQueryValueExW espera bytes

	const LSTATUS st = RegQueryValueExW(hkey, L"ProcessorNameString", nullptr, nullptr,
		std::bit_cast<LPBYTE>(&buf), &buf_bytes);
	RegCloseKey(hkey);

	if(st != ERROR_SUCCESS) return L"Desconhecido";

	std::wstring name(buf);
	while(!name.empty() && name.back() == L' ') name.pop_back(); // trailing spaces de OEMs
	return name;
}

/**
 * @brief Conta núcleos FÍSICOS via GetLogicalProcessorInformation.
 *
 * Cada entrada com Relationship == RelationProcessorCore representa 1 núcleo físico.
 * A função retorna o número de entradas desse tipo.
 * Em CPUs sem hyperthreading: físicos == lógicos.
 * Em CPUs com HT (ex.: Intel i9): físicos = lógicos / 2.
 *
 * @return Número de núcleos físicos, ou 0 em caso de falha.
 */
static uint32_t CountPhysicalCores() {
	DWORD byte_count = 0;

	// Primeira chamada: consulta o tamanho do buffer necessário
	GetLogicalProcessorInformation(nullptr, &byte_count);
	if(byte_count == 0) return 0;

	// Aloca o buffer com o tamanho exato
	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(
		byte_count / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

	// Segunda chamada: preenche o buffer
	if(!GetLogicalProcessorInformation(buf.data(), &byte_count))
		return 0;

	uint32_t physical = 0;
	for(const auto& entry : buf) {
		// RelationProcessorCore: uma entrada por núcleo físico real
		// RelationProcessorPackage seria por socket/chip físico
		if(entry.Relationship == RelationProcessorCore)
			++physical; // cada entrada = 1 core físico (independente de HT)
	}

	return physical;
}

/**
 * @brief Lê nome e build do Windows via RtlGetVersion (sem compat shims).
 * Build >= 22000 → Windows 11 (ambos têm dwMajorVersion = 10).
 * @return ex.: L"Windows 11 (Build 22631)"
 */
static std::wstring ReadOsName() {
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if(!ntdll) return L"Windows (versao desconhecida)";

	using RtlGetVersionFn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
	auto fn = std::bit_cast<RtlGetVersionFn>(
		GetProcAddress(ntdll, "RtlGetVersion")); // GetProcAddress só aceita char*
	if(!fn) return L"Windows (RtlGetVersion nao encontrado)";

	RTL_OSVERSIONINFOW info = {};
	info.dwOSVersionInfoSize = sizeof(info); // obrigatório
	if(fn(&info) != 0) return L"Windows (falha ao ler versao)"; // STATUS_SUCCESS == 0

	const wchar_t* name = L"Windows";
	if(info.dwMajorVersion == 10)
		name = (info.dwBuildNumber >= 22000) ? L"Windows 11" : L"Windows 10";
	else if(info.dwMajorVersion == 6) {
		if(info.dwMinorVersion == 3) name = L"Windows 8.1";
		else if(info.dwMinorVersion == 2) name = L"Windows 8";
		else if(info.dwMinorVersion == 1) name = L"Windows 7";
	}
	return std::wstring(name) + L" (Build " + std::to_wstring(info.dwBuildNumber) + L")";
}

// ============================================================================
// Helper — lê string do Registro (HKLM, wchar_t)
// ============================================================================

/**
 * @brief Lê um valor REG_SZ do Registro e retorna como wstring.
 *
 * Utilitário genérico usado por ReadMachineInfo para ler fabricante,
 * modelo e informações de BIOS de HKLM\HARDWARE\DESCRIPTION\System\BIOS.
 *
 * @param subkey  Subchave relativa a HKEY_LOCAL_MACHINE.
 * @param value   Nome do valor a ler.
 * @param fallback Retorno em caso de falha (default L"Desconhecido").
 * @return        Wide string com o conteúdo do valor.
 */
static std::wstring ReadRegistryStringW(
	const wchar_t* subkey,
	const wchar_t* value,
	const wchar_t* fallback = L"Desconhecido") {
	HKEY hkey = nullptr;
	if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
		return fallback;

	wchar_t buf[512] = {};
	DWORD   buf_bytes = sizeof(buf);
	const LSTATUS st = RegQueryValueExW(hkey, value, nullptr, nullptr,
		std::bit_cast<LPBYTE>(&buf), &buf_bytes);
	RegCloseKey(hkey);

	if(st != ERROR_SUCCESS) return fallback;

	std::wstring s(buf);
	while(!s.empty() && (s.back() == L' ' || s.back() == L'\0')) s.pop_back();
	return s.empty() ? fallback : s;
}

// ============================================================================
// Helper DXGI — enumera GPUs
// ============================================================================

/**
 * @brief Enumera GPUs físicas via DXGI, preenchendo nome, VRAM e shared memory.
 * Campos dx12_ultimate / dx12_ultimate_str são preenchidos depois em CollectDX12Ultimate.
 * @return std::vector<GpuInfo> com uma entrada por GPU física real.
 */
static std::vector<GpuInfo> EnumerateGpus() {
	std::vector<GpuInfo> gpus;

	IDXGIFactory1* factory = nullptr;
	if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
		std::bit_cast<void**>(&factory))) || !factory)
		return gpus;

	for(UINT i = 0; ; ++i) {
		IDXGIAdapter1* adapter = nullptr;
		HRESULT hr = factory->EnumAdapters1(i, &adapter);
		if(hr == DXGI_ERROR_NOT_FOUND) break;
		if(FAILED(hr) || !adapter)     continue;

		DXGI_ADAPTER_DESC1 desc = {};
		if(SUCCEEDED(adapter->GetDesc1(&desc)) &&
			!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) // filtra software renderer
		{
			GpuInfo g;
			g.name = desc.Description;
			g.vram_bytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
			g.shared_bytes = static_cast<uint64_t>(desc.SharedSystemMemory);
			g.driver_ver = L"(veja secao Vulkan)";
			g.dx12_ultimate = false;
			g.dx12_ultimate_str = L"Verificando...";
			gpus.push_back(std::move(g));
		}
		adapter->Release();
	}
	factory->Release();
	return gpus;
}

// ============================================================================
// Helper DX12 Ultimate — verifica as 4 features obrigatórias
// ============================================================================

/**
 * @brief Verifica se a GPU ativa suporta DX12 Ultimate.
 *
 * DX12 Ultimate exige TODAS as 4 features (definidas pela Microsoft em 2020):
 *
 *  1. DirectX Raytracing Tier 1.1 (DXR)
 *     Verificado via D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier
 *     Tier 1.1 adicionou inline raytracing e mais primitivos.
 *
 *  2. Variable Rate Shading Tier 2 (VRS)
 *     Verificado via D3D12_FEATURE_DATA_D3D12_OPTIONS6::VariableShadingRateTier
 *     Tier 2 = VRS por primitivo + combinadores de taxa de shading.
 *
 *  3. Mesh Shaders Tier 1
 *     Verificado via D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier
 *     Substitui os estágios vertex/hull/domain/geometry por mesh shaders.
 *
 *  4. Sampler Feedback Tier 0.9
 *     Verificado via D3D12_FEATURE_DATA_D3D12_OPTIONS7::SamplerFeedbackTier
 *     Permite que a GPU reporte quais mip levels de textura foram usados.
 *
 * ABORDAGEM:
 *  Carregamos d3d12.dll via LoadLibrary, criamos um device temporário sem swapchain,
 *  chamamos CheckFeatureSupport para cada OPTIONS struct e destruímos o device.
 *
 * @param[out] gpus  Vetor de GpuInfo a atualizar com o resultado.
 */
static void CollectDX12Ultimate(std::vector<GpuInfo>& gpus) {
	if(gpus.empty()) return;

	using PFN_D3D12CreateDevice = HRESULT(WINAPI*)(
		IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

	HMODULE d3d12_dll = LoadLibraryW(L"d3d12.dll");
	if(!d3d12_dll) {
		for(auto& g : gpus) g.dx12_ultimate_str = L"DX12 nao disponivel";
		return;
	}

	auto fn = std::bit_cast<PFN_D3D12CreateDevice>(
		GetProcAddress(d3d12_dll, "D3D12CreateDevice"));

	if(!fn) {
		FreeLibrary(d3d12_dll);
		for(auto& g : gpus) g.dx12_ultimate_str = L"D3D12CreateDevice nao encontrado";
		return;
	}

	// Tentamos criar um device para o adaptador padrão (índice 0)
	// Para múltiplas GPUs seria necessário enumerar IDXGIAdapter e passar cada uma.
	// Para fins de exibição, usamos o adaptador padrão (o mesmo que o DXGI retorna primeiro).
	ID3D12Device* dev = nullptr;
	HRESULT hr = fn(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device),
		std::bit_cast<void**>(&dev));

	if(FAILED(hr) || !dev) {
		FreeLibrary(d3d12_dll);
		for(auto& g : gpus) g.dx12_ultimate_str = L"Nao suporta DX12";
		return;
	}

	// ---- Feature 1: DXR Tier 1.1 -------------------------------------------
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
	bool dxr = SUCCEEDED(dev->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))
		&& opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
	// opts5.RaytracingTier: NOT_SUPPORTED=0, TIER_1_0=10, TIER_1_1=11

	// ---- Feature 2: VRS Tier 2 ----------------------------------------------
	D3D12_FEATURE_DATA_D3D12_OPTIONS6 opts6 = {};
	bool vrs = SUCCEEDED(dev->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS6, &opts6, sizeof(opts6)))
		&& opts6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;

	// ---- Feature 3: Mesh Shaders Tier 1 + Feature 4: Sampler Feedback -------
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7 = {};
	bool mesh = false;
	bool sfb = false;
	if(SUCCEEDED(dev->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7)))) {
		mesh = opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
		// ---- Feature 4: Sampler Feedback Tier 0.9 ---------------------------
		sfb = opts7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
	}

	dev->Release();         // libera o device temporário
	FreeLibrary(d3d12_dll); // libera a DLL

	bool ultimate = dxr && vrs && mesh && sfb; // TODAS as 4 devem ser verdadeiras

	// Monta a string de detalhes com o status de cada feature
	std::wstring detail;
	detail += std::wstring(dxr  ? L"[ok] " : L"[no] ") + L"DXR Tier 1.1\n";
	detail += std::wstring(vrs  ? L"[ok] " : L"[no] ") + L"VRS Tier 2\n";
	detail += std::wstring(mesh ? L"[ok] " : L"[no] ") + L"Mesh Shaders\n";
	detail += std::wstring(sfb  ? L"[ok] " : L"[no] ") + L"Sampler Feedback";

	// Aplica o resultado a TODAS as entradas de GPU
	// (Para multiplas GPUs distintas seria necessário criar um device por IDXGIAdapter)
	for(auto& g : gpus) {
		g.dx12_ultimate = ultimate;
		g.dx12_ultimate_str = detail;
	}
}

// ============================================================================
// Helper DirectX — versão máxima (sem linkar d3d11/d3d12)
// ============================================================================

/**
 * @brief Detecta a versão máxima de DirectX via LoadLibrary (sem linkar .lib).
 * @return DirectXInfo preenchido.
 */
static DirectXInfo CollectDirectXInfo() {
	DirectXInfo info;
	info.dx12_support = L"Nao disponivel";
	info.dx11_max = L"N/A";
	info.max_version = L"Desconhecido";

	using PFN_D3D12CreateDevice = HRESULT(WINAPI*)(
		IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

	HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
	if(d3d12) {
		auto fn12 = std::bit_cast<PFN_D3D12CreateDevice>(
			GetProcAddress(d3d12, "D3D12CreateDevice"));
		if(fn12) {
			ID3D12Device* dev = nullptr;

			// Tenta FL 12_1 primeiro (mais alto comum)
			HRESULT hr = fn12(nullptr, D3D_FEATURE_LEVEL_12_1,
				__uuidof(ID3D12Device), std::bit_cast<void**>(&dev));
			if(SUCCEEDED(hr) && dev) {
				info.dx12_support = L"Sim (Feature Level 12_1)";
				info.max_version = L"DirectX 12 (FL 12_1)";
				dev->Release();
			} else {
				// Fallback: FL 12_0
				hr = fn12(nullptr, D3D_FEATURE_LEVEL_12_0,
					__uuidof(ID3D12Device), std::bit_cast<void**>(&dev));
				if(SUCCEEDED(hr) && dev) {
					info.dx12_support = L"Sim (Feature Level 12_0)";
					info.max_version = L"DirectX 12 (FL 12_0)";
					dev->Release();
				}
			}
		}
		FreeLibrary(d3d12);
	}

	using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(
		IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
		const D3D_FEATURE_LEVEL*, UINT, UINT,
		ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

	HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
	if(d3d11) {
		auto fn11 = std::bit_cast<PFN_D3D11CreateDevice>(
			GetProcAddress(d3d11, "D3D11CreateDevice"));
		if(fn11) {
			const std::array<D3D_FEATURE_LEVEL, 4> levels = {
				D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
			};
			ID3D11Device* dev11 = nullptr;
			ID3D11DeviceContext* ctx = nullptr;
			D3D_FEATURE_LEVEL actual = D3D_FEATURE_LEVEL_10_0;

			HRESULT hr = fn11(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
				&dev11, &actual, &ctx);
			if(SUCCEEDED(hr)) {
				switch(actual) {
					case D3D_FEATURE_LEVEL_11_1: info.dx11_max = L"11_1"; break;
					case D3D_FEATURE_LEVEL_11_0: info.dx11_max = L"11_0"; break;
					case D3D_FEATURE_LEVEL_10_1: info.dx11_max = L"10_1"; break;
					default:                     info.dx11_max = L"10_0"; break;
				}
				if(info.max_version == L"Desconhecido")
					info.max_version = L"DirectX 11 (FL " + info.dx11_max + L")";
				if(dev11) dev11->Release();
				if(ctx)   ctx->Release();
			}
		}
		FreeLibrary(d3d11);
	}
	return info;
}

// ============================================================================
// Helper OpenGL — versão via janela oculta + WGL
// ============================================================================

/**
 * @brief Obtém versão OpenGL criando um contexto WGL temporário e oculto.
 *
 * Fluxo: RegisterClassExW → CreateWindowExW (oculto) → GetDC → ChoosePixelFormat
 *        → SetPixelFormat → wglCreateContext → wglMakeCurrent → glGetString
 *        → cleanup completo (wglMakeCurrent null, wglDeleteContext, ReleaseDC, DestroyWindow)
 *
 * @return OpenGLInfo preenchido, ou campos em L"Nao disponivel".
 */
static OpenGLInfo CollectOpenGLInfo() {
	OpenGLInfo info;
	info.version = L"Nao disponivel";
	info.vendor = L"N/A";
	info.renderer = L"N/A";

	HMODULE gl = LoadLibraryW(L"opengl32.dll");
	if(!gl) return info;

	using PFN_glGetString   = const GLubyte*(WINAPI*)(GLenum);
	using PFN_wglCreateCtx  = HGLRC(WINAPI*)(HDC);
	using PFN_wglMakeCurrent = BOOL(WINAPI*)(HDC, HGLRC);
	using PFN_wglDeleteCtx  = BOOL(WINAPI*)(HGLRC);

	auto fn_glGet  = std::bit_cast<PFN_glGetString>   (GetProcAddress(gl, "glGetString"));
	auto fn_create = std::bit_cast<PFN_wglCreateCtx>  (GetProcAddress(gl, "wglCreateContext"));
	auto fn_make   = std::bit_cast<PFN_wglMakeCurrent>(GetProcAddress(gl, "wglMakeCurrent"));
	auto fn_del    = std::bit_cast<PFN_wglDeleteCtx>  (GetProcAddress(gl, "wglDeleteContext"));

	if(!fn_glGet || !fn_create || !fn_make || !fn_del) { FreeLibrary(gl); return info; }

	WNDCLASSEXW wc = {};
	wc.cbSize        = sizeof(wc);
	wc.style         = CS_OWNDC;             // janela com seu próprio DC
	wc.lpfnWndProc   = DefWindowProcW;       // procedimento padrão
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"__TmpGLQueryWnd";
	if(!RegisterClassExW(&wc)) { FreeLibrary(gl); return info; }

	// Janela oculta 1×1 pixel — WS_OVERLAPPED sem WS_VISIBLE
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
		0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
	if(!hwnd) {
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		FreeLibrary(gl); return info;
	}

	HDC hdc = GetDC(hwnd);

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize        = sizeof(pfd);
	pfd.nVersion     = 1;
	pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType   = PFD_TYPE_RGBA;
	pfd.cColorBits   = 32;
	pfd.cDepthBits   = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType   = PFD_MAIN_PLANE;

	int pf = ChoosePixelFormat(hdc, &pfd);
	if(!pf || !SetPixelFormat(hdc, pf, &pfd)) {
		ReleaseDC(hwnd, hdc); DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		FreeLibrary(gl); return info;
	}

	HGLRC hglrc = fn_create(hdc); // cria o context OpenGL legacy (WGL 1.0)
	if(!hglrc) {
		ReleaseDC(hwnd, hdc); DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		FreeLibrary(gl); return info;
	}

	fn_make(hdc, hglrc); // ativa o contexto neste thread

	// Converte const char* → wstring usando a localidade ANSI do sistema
	auto toW = [](const char* s) -> std::wstring {
		if(!s) return L"N/A";
		int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
		if(n <= 0) return L"N/A";
		std::wstring w(n - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
		return w;
	};

	info.version  = toW(std::bit_cast<const char*>(fn_glGet(GL_VERSION)));
	info.vendor   = toW(std::bit_cast<const char*>(fn_glGet(GL_VENDOR)));
	info.renderer = toW(std::bit_cast<const char*>(fn_glGet(GL_RENDERER)));

	// Cleanup completo
	fn_make(nullptr, nullptr); // desativa o contexto
	fn_del(hglrc);             // destrói o HGLRC
	ReleaseDC(hwnd, hdc);
	DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	FreeLibrary(gl);

	return info;
}

// ============================================================================
// Helper Discos — SSD vs HDD via DeviceIoControl
// ============================================================================

/**
 * @brief Abre um disco físico e retorna seu HANDLE para DeviceIoControl.
 *
 * @param index  Índice do disco (0 = PhysicalDrive0, 1 = PhysicalDrive1, ...).
 * @return       HANDLE válido ou INVALID_HANDLE_VALUE se o disco não existir.
 */
static HANDLE OpenDisk(uint32_t index) {
	wchar_t path[32];
	swprintf_s(path, L"\\\\.\\PhysicalDrive%u", index); // ex.: \\.\PhysicalDrive0

	return CreateFileW(
		path,
		0,                                       // sem permissões de leitura/escrita de dados
		FILE_SHARE_READ | FILE_SHARE_WRITE,      // compartilhado para não bloquear outros processos
		nullptr,
		OPEN_EXISTING,                           // apenas abre se existir
		0,
		nullptr);
}

/**
 * @brief Obtém o nome amigável de um disco via IOCTL_STORAGE_QUERY_PROPERTY.
 *
 * StorageDeviceProperty retorna um STORAGE_DEVICE_DESCRIPTOR com VendorId e ProductId.
 * Concatenamos os dois para formar o nome completo (ex.: "Samsung SSD 980 PRO 1TB").
 *
 * @param hDisk  HANDLE aberto por OpenDisk().
 * @return       Wide string com o nome, ou L"Disco Desconhecido".
 */
static std::wstring GetDiskFriendlyName(HANDLE hDisk) {
	STORAGE_PROPERTY_QUERY query = {};
	query.PropertyId = StorageDeviceProperty; // propriedades gerais do dispositivo
	query.QueryType  = PropertyStandardQuery; // consulta padrão

	// Aloca buffer generoso para a descriptor variável
	char buf[1024] = {};
	DWORD returned = 0;

	if(!DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY,
		&query, sizeof(query),
		buf, sizeof(buf), &returned, nullptr))
		return L"Disco Desconhecido";

	auto* desc = std::bit_cast<STORAGE_DEVICE_DESCRIPTOR*>(&buf);

	// VendorId e ProductId são offsets dentro do próprio buffer
	const char* vendor  = (desc->VendorIdOffset  && desc->VendorIdOffset  < returned)
		? buf + desc->VendorIdOffset  : "";
	const char* product = (desc->ProductIdOffset && desc->ProductIdOffset < returned)
		? buf + desc->ProductIdOffset : "";

	// Remove espaços e monta o nome: "vendor product" ou só "product"
	std::string full = std::string(vendor) + " " + std::string(product);
	while(!full.empty() && full.front() == ' ') full.erase(full.begin());
	while(!full.empty() && full.back()  == ' ') full.pop_back();
	if(full.empty()) return L"Disco Desconhecido";

	int n = MultiByteToWideChar(CP_ACP, 0, full.c_str(), -1, nullptr, 0);
	if(n <= 0) return L"Disco Desconhecido";
	std::wstring w(n - 1, L'\0');
	MultiByteToWideChar(CP_ACP, 0, full.c_str(), -1, &w[0], n);
	return w;
}

/**
 * @brief Detecta se um disco é SSD ou HDD via StorageDeviceSeekPenaltyProperty.
 *
 * DEVICE_SEEK_PENALTY_DESCRIPTOR::IncursSeekPenalty:
 *   FALSE = sem seek penalty → SSD (acesso aleatório sem custo mecânico)
 *   TRUE  = com seek penalty → HDD (cabeçote precisa se mover)
 *
 * @param hDisk  HANDLE aberto por OpenDisk().
 * @return       L"SSD", L"HDD" ou L"Desconhecido".
 */
static std::wstring GetDiskType(HANDLE hDisk) {
	STORAGE_PROPERTY_QUERY query = {};
	query.PropertyId = StorageDeviceSeekPenaltyProperty; // seek penalty
	query.QueryType  = PropertyStandardQuery;

	DEVICE_SEEK_PENALTY_DESCRIPTOR desc = {};
	DWORD returned = 0;

	if(!DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY,
		&query, sizeof(query),
		&desc, sizeof(desc), &returned, nullptr))
		return L"Desconhecido";

	// IncursSeekPenalty: TRUE = HDD (movimento mecânico), FALSE = SSD
	return desc.IncursSeekPenalty ? L"HDD" : L"SSD";
}

/**
 * @brief Obtém a capacidade total de um disco via IOCTL_DISK_GET_LENGTH_INFO.
 *
 * @param hDisk  HANDLE aberto por OpenDisk().
 * @return       Tamanho em bytes, ou 0 em caso de falha.
 */
static uint64_t GetDiskSize(HANDLE hDisk) {
	GET_LENGTH_INFORMATION len = {};
	DWORD returned = 0;

	if(!DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO,
		nullptr, 0,
		&len, sizeof(len), &returned, nullptr))
		return 0;

	return static_cast<uint64_t>(len.Length.QuadPart); // LargeInteger → uint64_t
}

/**
 * @brief Enumera todos os discos físicos abrindo PhysicalDrive0, 1, 2...
 *
 * Para cada disco abre o HANDLE, consulta nome, tipo e capacidade.
 * O loop termina quando CreateFileW retorna INVALID_HANDLE_VALUE (disco não existe).
 * Limitamos a 16 discos para evitar espera em sistemas sem discos nesse range.
 *
 * @return std::vector<DiskInfo> com uma entrada por disco físico.
 */
static std::vector<DiskInfo> EnumerateDisks() {
	std::vector<DiskInfo> disks;

	for(uint32_t i = 0; i < 16; ++i) { // máximo de 16 discos físicos
		HANDLE hDisk = OpenDisk(i);
		if(hDisk == INVALID_HANDLE_VALUE) break; // sem mais discos neste índice

		DiskInfo d;
		d.index        = i;
		d.device_path  = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
		d.friendly_name = GetDiskFriendlyName(hDisk);
		d.type         = GetDiskType(hDisk);
		d.size_bytes   = GetDiskSize(hDisk);

		disks.push_back(std::move(d));
		CloseHandle(hDisk); // fecha o handle após as consultas
	}
	return disks;
}

// ============================================================================
// Helper MachineInfo — fabricante, modelo, nome do PC
// ============================================================================

/**
 * @brief Coleta fabricante, modelo, nome do computador e versão do BIOS.
 *
 * FONTES:
 *  HKLM\HARDWARE\DESCRIPTION\System\BIOS
 *    BaseBoardManufacturer → fabricante da placa-mãe
 *    BaseBoardProduct      → modelo da placa-mãe
 *    SystemManufacturer    → fabricante do sistema (notebooks/OEMs)
 *    SystemProductName     → modelo do sistema (notebooks/OEMs)
 *    BIOSVersion           → versão do BIOS
 *    BIOSReleaseDate       → data do BIOS
 *
 *  GetComputerNameExW(ComputerNameNetBIOS) → nome da máquina na rede
 *
 * Em PCs montados (sem OEM), SystemManufacturer costuma ser vazio
 * ou genérico ("To Be Filled By O.E.M."), por isso preferimos BaseBoardManufacturer.
 *
 * @return MachineInfo preenchido.
 */
static MachineInfo ReadMachineInfo() {
	MachineInfo info;

	const wchar_t* bios_key = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

	// Lê fabricante do sistema (OEMs como Dell, HP, Lenovo preenchem isso)
	std::wstring sys_mfr = ReadRegistryStringW(bios_key, L"SystemManufacturer");
	std::wstring sys_prd = ReadRegistryStringW(bios_key, L"SystemProductName");

	// Em PCs montados, esses valores ficam como "To Be Filled By O.E.M."
	const bool sys_generic = (sys_mfr.find(L"Fill") != std::wstring::npos ||
		sys_mfr.find(L"fill") != std::wstring::npos ||
		sys_mfr == L"Desconhecido");

	if(!sys_generic && !sys_mfr.empty()) {
		// OEM: usa informações do sistema completo
		info.manufacturer = sys_mfr;
		info.model        = sys_prd;
	} else {
		// PC montado: usa informações da placa-mãe
		info.manufacturer = ReadRegistryStringW(bios_key, L"BaseBoardManufacturer");
		info.model        = ReadRegistryStringW(bios_key, L"BaseBoardProduct");
	}

	// Versão do BIOS: junta BIOSVersion + BIOSReleaseDate
	std::wstring bios_ver  = ReadRegistryStringW(bios_key, L"BIOSVersion");
	std::wstring bios_date = ReadRegistryStringW(bios_key, L"BIOSReleaseDate", L"");
	info.bios_version = bios_ver + (bios_date.empty() ? L"" : L" (" + bios_date + L")");

	// Nome do computador na rede (NetBIOS, máximo 16 chars no Windows)
	wchar_t comp_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
	DWORD   comp_len = MAX_COMPUTERNAME_LENGTH + 1;
	if(GetComputerNameExW(ComputerNameNetBIOS, comp_name, &comp_len))
		info.computer_name = std::wstring(comp_name);
	else
		info.computer_name = L"Desconhecido";

	return info;
}

// ============================================================================
// Helpers Vulkan
// ============================================================================

/**
 * @brief Converte uint32_t Vulkan para L"major.minor.patch".
 * Bits: [28:22]=major, [21:12]=minor, [11:0]=patch.
 */
static std::wstring VkVersionToWstr(uint32_t v) {
	return std::to_wstring(VK_API_VERSION_MAJOR(v)) + L"."
		+ std::to_wstring(VK_API_VERSION_MINOR(v)) + L"."
		+ std::to_wstring(VK_API_VERSION_PATCH(v));
}

/**
 * @brief Decodifica driverVersion por fabricante.
 * NVIDIA (0x10DE): major.minor (ex.: "561.09")
 * Intel  (0x8086): build.sub-build
 * AMD e outros:    VK_MAKE_VERSION padrão
 */
static std::wstring DecodeDriverVersion(uint32_t v, uint32_t vid) {
	if(vid == 0x10DE) {
		wchar_t buf[32];
		swprintf_s(buf, L"%u.%02u", (v >> 22) & 0x3FF, (v >> 14) & 0xFF);
		return std::wstring(buf);
	}
	if(vid == 0x8086)
		return std::to_wstring((v >> 14) & 0x3FFFF) + L"." + std::to_wstring(v & 0x3FFF);
	return VkVersionToWstr(v);
}

/**
 * @brief Coleta versões Vulkan da instância e do physical device ativo.
 * @param vk  VulkanContext já inicializado; nullptr deixa campos em L"N/A".
 */
static VulkanInfo CollectVulkanInfo(VulkanContext* vk) {
	VulkanInfo info;

	auto fn = std::bit_cast<PFN_vkEnumerateInstanceVersion>(
		vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));

	if(fn) {
		uint32_t ver = 0; fn(&ver);
		info.instance_version = ver;
		info.instance_str = VkVersionToWstr(ver);
	} else {
		info.instance_version = VK_MAKE_API_VERSION(0, 1, 0, 0);
		info.instance_str = L"1.0.0 (legado)";
	}

	if(!vk) {
		info.driver_str = info.device_name = info.api_str = L"N/A";
		info.api_version = 0;
		return info;
	}

	VkPhysicalDevice phys = vk->GetPhysicalDevice();
	if(phys == VK_NULL_HANDLE) {
		info.driver_str = info.device_name = info.api_str = L"N/A";
		info.api_version = 0;
		return info;
	}

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(phys, &props);

	{
		int n = MultiByteToWideChar(CP_UTF8, 0, props.deviceName, -1, nullptr, 0);
		if(n > 0) {
			std::wstring w(n - 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, props.deviceName, -1, &w[0], n);
			info.device_name = std::move(w);
		} else { info.device_name = L"(ilegivel)"; }
	}

	info.api_version = props.apiVersion;
	info.api_str     = VkVersionToWstr(props.apiVersion);
	info.driver_str  = DecodeDriverVersion(props.driverVersion, props.vendorID);
	return info;
}

// ============================================================================
// Helper Monitores — EnumDisplayDevices + EDID via SetupAPI
// ============================================================================

/**
 * @brief Decodifica o Manufacturer ID de 2 bytes do EDID para uma string de 3 letras.
 *
 * O EDID armazena o fabricante em 2 bytes big-endian nos offsets [8] e [9].
 * Cada letra é codificada em 5 bits (A=1, B=2, ..., Z=26):
 *   Byte [8]: bits [6:2] = primeira letra, bits [1:0] = bits altos da segunda
 *   Byte [9]: bits [7:5] = bits baixos da segunda letra, bits [4:0] = terceira letra
 *
 * Exemplos conhecidos: DEL=Dell, SAM=Samsung, LGD=LG Display, AUO=AU Optronics,
 *                      BOE=BOE Technology, CMN=Chimei Innolux, SDC=Samsung Display
 *
 * @param edid  Ponteiro para o buffer EDID de pelo menos 10 bytes.
 * @return      Wide string de 3 letras ex.: L"DEL", ou L"???" se inválido.
 */
static std::wstring DecodeEdidManufacturerId(std::span<const uint8_t> edid) {
	// Precisamos de pelo menos 10 bytes para chegar ao manufacturer ID
	if(edid.size() < 10) return L"???";

	// Os 2 bytes do manufacturer ID estão em big-endian (byte mais significativo primeiro)
	// Combinamos os dois bytes em um uint16_t para facilitar a extração dos bits
	const uint16_t raw = (static_cast<uint16_t>(edid[8]) << 8)
					   |  static_cast<uint16_t>(edid[9]);

	// Cada letra é um valor de 1 a 26 (A=1 ... Z=26), codificado em 5 bits
	// Letra 1: bits 14-10 do raw (5 bits mais altos, ignorando o bit 15 que é sempre 0)
	const uint8_t c1 = static_cast<uint8_t>((raw >> 10) & 0x1F);
	// Letra 2: bits 9-5 do raw
	const uint8_t c2 = static_cast<uint8_t>((raw >>  5) & 0x1F);
	// Letra 3: bits 4-0 do raw (5 bits mais baixos)
	const uint8_t c3 = static_cast<uint8_t>( raw        & 0x1F);

	// Valida que cada código está no range 1-26 (A-Z)
	if(c1 < 1 || c1 > 26 || c2 < 1 || c2 > 26 || c3 < 1 || c3 > 26)
		return L"???";

	// Converte de 1-26 para 'A'-'Z' adicionando o offset ASCII de 'A' - 1
	return std::wstring{
		static_cast<wchar_t>(L'A' + c1 - 1),
		static_cast<wchar_t>(L'A' + c2 - 1),
		static_cast<wchar_t>(L'A' + c3 - 1)
	};
}

/**
 * @brief Mapeia códigos de 3 letras do EDID para nomes de fabricantes legíveis.
 *
 * Esta tabela cobre os fabricantes de painéis mais comuns encontrados em
 * monitores de desktop e notebooks. O código de 3 letras é o PNP ID registrado
 * no UEFI Forum / Microsoft.
 *
 * @param code  Código de 3 letras ex.: L"DEL".
 * @return      Nome completo ex.: L"Dell", ou o próprio código se desconhecido.
 */
static std::wstring EdidCodeToManufacturerName(const std::wstring& code) {
	// Tabela de PNP IDs → nomes de fabricantes de monitores
	// Fonte: https://uefi.org/pnp_id_list e base de dados da Microsoft
	static const std::array<std::pair<std::wstring_view, std::wstring_view>, 40> table = {{
		{ L"DEL", L"Dell" },
		{ L"SAM", L"Samsung" },
		{ L"LGD", L"LG Display" },        // painel (notebook)
		{ L"GSM", L"LG Electronics" },    // monitor externo
		{ L"AUO", L"AU Optronics" },
		{ L"BOE", L"BOE Technology" },
		{ L"CMN", L"Chimei Innolux" },
		{ L"SDC", L"Samsung Display" },
		{ L"SHP", L"Sharp" },
		{ L"HPN", L"HP" },
		{ L"HWP", L"HP" },
		{ L"ACR", L"Acer" },
		{ L"VIZ", L"Vizio" },
		{ L"VSC", L"ViewSonic" },
		{ L"BNQ", L"BenQ" },
		{ L"AOC", L"AOC" },
		{ L"IVM", L"Iiyama" },
		{ L"NEC", L"NEC" },
		{ L"EIZ", L"EIZO" },
		{ L"PHL", L"Philips" },
		{ L"MSI", L"MSI" },
		{ L"ASU", L"ASUS" },
		{ L"AUS", L"ASUS" },
		{ L"GBT", L"Gigabyte" },
		{ L"RTK", L"Realtek" },
		{ L"LEN", L"Lenovo" },
		{ L"IBM", L"IBM" },
		{ L"SNY", L"Sony" },
		{ L"PNS", L"Panasonic" },
		{ L"TOS", L"Toshiba" },
		{ L"CPT", L"Chunghwa Picture Tubes" },
		{ L"HSD", L"HannStar Display" },
		{ L"INX", L"Innolux" },
		{ L"KTC", L"KTC" },
		{ L"MEI", L"Panasonic" },
		{ L"ONN", L"ONN" },
		{ L"APP", L"Apple" },
		{ L"COG", L"Costar" },
		{ L"FUJ", L"Fujitsu" },
		{ L"MAT", L"Matsushita" },
	}};

	for(const auto& [key, name] : table) {
		// Comparação case-sensitive: os IDs do EDID são sempre maiúsculos
		if(key == code) return std::wstring(name);
	}

	// Código desconhecido: retorna o próprio código de 3 letras para o usuário investigar
	return code;
}

/**
 * @brief Extrai o nome do monitor e serial number dos Descriptor Blocks do EDID.
 *
 * O EDID contém 4 Descriptor Blocks de 18 bytes cada, começando no offset 54.
 * Cada bloco começa com 2 bytes: se ambos forem 0x00, é um extended descriptor.
 * O byte [3] do bloco é o tag:
 *   0xFC = Monitor Name       (bytes [5-17], ASCII, terminado por 0x0A ou espaços)
 *   0xFF = Monitor Serial No  (bytes [5-17], ASCII)
 *
 * @param edid         Buffer EDID de pelo menos 128 bytes.
 * @param[out] name    Nome do monitor extraído, ou vazio se não encontrado.
 * @param[out] serial  Serial number extraído, ou vazio se não encontrado.
 */
static void ExtractEdidDescriptors(
	std::span<const uint8_t> edid,
	std::wstring& name,
	std::wstring& serial)
{
	// O EDID padrão tem 128 bytes; os descriptors começam no offset 54
	if(edid.size() < 128) return;

	// Há 4 descriptor blocks de 18 bytes cada
	constexpr uint32_t k_descriptor_base   = 54;  // offset do primeiro descriptor
	constexpr uint32_t k_descriptor_size   = 18;  // tamanho de cada descriptor
	constexpr uint32_t k_descriptor_count  =  4;  // número de descriptors
	constexpr uint8_t  k_tag_name          = 0xFC; // tag para Monitor Name
	constexpr uint8_t  k_tag_serial        = 0xFF; // tag para Serial Number

	for(uint32_t i = 0; i < k_descriptor_count; ++i) {
		// Calcula o offset de início deste descriptor block
		const uint32_t base = k_descriptor_base + i * k_descriptor_size;

		// Verifica que temos bytes suficientes no buffer
		if(base + k_descriptor_size > static_cast<uint32_t>(edid.size())) break;

		// Um extended descriptor começa com bytes [0] e [1] iguais a 0x00
		if(edid[static_cast<const uint8_t>(base + 0)] != 0x00 || edid[static_cast<const uint8_t>(base + 1)] != 0x00) continue;

		// Byte [3] é o tag que identifica o tipo do descriptor
		const uint8_t tag = edid[static_cast<const uint8_t>(base + 3)];

		// Apenas processamos os tags 0xFC (nome) e 0xFF (serial)
		if(tag != k_tag_name && tag != k_tag_serial) continue;

		// Os dados ASCII estão nos bytes [5] a [17] (13 bytes úteis)
		// Extraímos como string narrow e convertemos para wide
		std::string ascii;
		ascii.reserve(13);
		for(uint32_t j = 5; j < k_descriptor_size; ++j) {
			const uint8_t ch = edid[static_cast<const uint8_t>(base + j)];
			// 0x0A (LF) é o terminador do campo de texto no EDID
			if(ch == 0x0A) break;
			// Ignora padding (caracteres de controle além do LF)
			if(ch < 0x20) continue;
			ascii.push_back(static_cast<char>(ch));
		}

		// Remove trailing spaces que alguns fabricantes adicionam como padding
		while(!ascii.empty() && ascii.back() == ' ') ascii.pop_back();

		if(ascii.empty()) continue;

		// Converte ASCII → wstring
		const int n = MultiByteToWideChar(CP_ACP, 0, ascii.c_str(), -1, nullptr, 0);
		if(n <= 0) continue;
		std::wstring w(n - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, ascii.c_str(), -1, w.data(), n);

		// Armazena no campo correto conforme o tag
		if(tag == k_tag_name)   name   = std::move(w);
		if(tag == k_tag_serial) serial = std::move(w);
	}
}

/**
 * @brief Lê o blob EDID de um monitor a partir do Registro do Windows via SetupAPI.
 *
 * CAMINHO NO REGISTRO:
 *   HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<ModelID>\<InstanceID>\
 *     Device Parameters\EDID
 *
 * ESTRATÉGIA:
 *   1. SetupDiGetClassDevsW(GUID_DEVCLASS_MONITOR) → enumera todos os monitores
 *   2. SetupDiEnumDeviceInfo → itera cada instância de monitor
 *   3. SetupDiGetDeviceInstanceIdW → obtém o Instance ID ex.: "DISPLAY\DEL4141\4&..."
 *   4. Verificamos se o Instance ID contém o DeviceID do monitor desejado
 *   5. SetupDiOpenDevRegKey → abre HKLM\...\Device Parameters
 *   6. RegQueryValueExW(L"EDID") → lê os 128+ bytes brutos
 *
 * @param monitor_device_id  Device ID do monitor ex.: L"MONITOR\DEL4141".
 * @return                   Buffer EDID ou vetor vazio se não encontrado.
 */
static std::vector<uint8_t> ReadEdidFromRegistry(const std::wstring& monitor_device_id) {
	// Abre o conjunto de dispositivos da classe Monitor (GUID_DEVCLASS_MONITOR)
	// DIGCF_PRESENT → apenas dispositivos conectados e ativos agora
	HDEVINFO dev_info = SetupDiGetClassDevsW(
		&GUID_DEVINTERFACE_MONITOR, // GUID da classe "Monitor"
		nullptr,                // sem enumerador específico
		nullptr,                // sem HWND pai
		DIGCF_PRESENT);         // apenas dispositivos presentes

	if(dev_info == INVALID_HANDLE_VALUE) return {};

	std::vector<uint8_t> edid_data; // resultado: bytes do EDID

	SP_DEVINFO_DATA dev_data = {};
	dev_data.cbSize = sizeof(dev_data); // obrigatório antes de chamar SetupDiEnumDeviceInfo

	// Itera todos os dispositivos da classe Monitor
	for(DWORD idx = 0; SetupDiEnumDeviceInfo(dev_info, idx, &dev_data); ++idx) {
		// Lê o Instance ID completo ex.: "DISPLAY\DEL4141\4&1abc2def&0&UID256"
		wchar_t instance_id[512] = {};
		if(!SetupDiGetDeviceInstanceIdW(dev_info, &dev_data,
			instance_id, static_cast<DWORD>(std::size(instance_id)), nullptr))
			continue; // falha ao ler o ID: pula para o próximo

		// Converte para uppercase para comparação case-insensitive
		std::wstring inst_upper(instance_id);
		for(auto& ch : inst_upper) ch = towupper(ch); // tolower/toupper para wchar_t

		std::wstring id_upper(monitor_device_id);
		for(auto& ch : id_upper) ch = towupper(ch);

		// Verifica se este dispositivo corresponde ao monitor que procuramos
		// O DeviceID do monitor (ex.: "MONITOR\DEL4141") aparece no início do Instance ID
		// mas com DISPLAY\ no início — toleramos ambos os prefixos
		// A busca por substring é suficiente porque o ID do modelo é único por monitor
		if(inst_upper.find(id_upper) == std::wstring::npos) continue;

		// Abre a chave "Device Parameters" deste dispositivo de monitor
		// DIREG_DEV → chave raiz do dispositivo no Registro
		HKEY hkey = SetupDiOpenDevRegKey(
			dev_info,    // handle do conjunto de dispositivos
			&dev_data,   // dispositivo específico
			DICS_FLAG_GLOBAL, // configuração global (não específica de hardware profile)
			0,           // reserved
			DIREG_DEV,   // abre a chave do dispositivo (Device Parameters)
			KEY_READ);   // apenas leitura

		if(hkey == INVALID_HANDLE_VALUE) continue;

		// Consulta o tamanho do blob EDID antes de alocar
		DWORD edid_size = 0;
		DWORD type      = REG_BINARY; // EDID é um valor binário
		RegQueryValueExW(hkey, L"EDID", nullptr, &type, nullptr, &edid_size);

		if(edid_size >= 128) { // EDID padrão tem no mínimo 128 bytes
			edid_data.resize(edid_size);
			if(RegQueryValueExW(hkey, L"EDID", nullptr, &type,
				edid_data.data(), &edid_size) != ERROR_SUCCESS)
				edid_data.clear(); // falha na leitura: descarta
		}

		RegCloseKey(hkey);

		if(!edid_data.empty()) break; // encontrou o EDID: para a busca
	}

	SetupDiDestroyDeviceInfoList(dev_info); // libera o handle do conjunto
	return edid_data;
}

/**
 * @brief Callback para EnumDisplayMonitors — coleta o rect e flag de primário.
 *
 * EnumDisplayMonitors chama esta função para cada monitor lógico do sistema.
 * Usamos para preencher o campo is_primary no MonitorInfo correspondente,
 * comparando o device name do MONITORINFOEXA com o device name do GDI.
 *
 * MONITORINFOEXA (versão ANSI de MONITORINFOEX):
 *   rcMonitor  → RECT com a posição e tamanho em coordenadas virtuais
 *   rcWork     → RECT da área de trabalho (sem taskbar)
 *   dwFlags    → MONITORINFOF_PRIMARY se for o monitor principal
 *   szDevice   → nome GDI ex.: "\\\\.\\DISPLAY1"
 *
 * @param hMonitor  Handle do monitor lógico.
 * @param hdcMonitor HDC do monitor (não usado aqui).
 * @param lprcMonitor Rect do monitor em coordenadas de tela.
 * @param dwData    Ponteiro para o vector<MonitorInfo> a preencher.
 * @return          TRUE para continuar a enumeração, FALSE para parar.
 */
static BOOL CALLBACK MonitorEnumProc(
	HMONITOR hMonitor,
	HDC      /*hdcMonitor*/,
	LPRECT   /*lprcMonitor*/,
	LPARAM   dwData)
{
	// dwData é o ponteiro para o vector<MonitorInfo> passado por EnumDisplayMonitors
	auto* monitors = std::bit_cast<std::vector<MonitorInfo>*>(dwData);

	MONITORINFOEXA info = {};
	info.cbSize = sizeof(info); // obrigatório
	if(!GetMonitorInfoA(hMonitor, &info)) return TRUE; // falha: continua

	// Converte o device name ANSI → wide para comparar com o que salvamos
	const int n = MultiByteToWideChar(CP_ACP, 0, info.szDevice, -1, nullptr, 0);
	if(n <= 0) return TRUE;
	std::wstring dev_name(n - 1, L'\0');
	MultiByteToWideChar(CP_ACP, 0, info.szDevice, -1, dev_name.data(), n);

	// Procura o MonitorInfo correspondente pelo device_name e marca como primário
	for(auto& mon : *monitors) {
		if(mon.device_name == dev_name) {
			// MONITORINFOF_PRIMARY == 1: este é o monitor padrão do sistema
			mon.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
			break;
		}
	}

	return TRUE; // TRUE = continua enumerando os próximos monitores
}

/**
 * @brief Enumera todos os monitores físicos conectados ao sistema.
 *
 * PIPELINE COMPLETO:
 *  1. EnumDisplayDevicesW(nullptr) → itera adaptadores de vídeo (DISPLAYn)
 *  2. Para cada adaptador ativo: EnumDisplayDevicesW(adapter) → itera monitores
 *  3. EnumDisplaySettingsW → lê resolução e refresh rate atual
 *  4. ReadEdidFromRegistry → lê EDID bruto (128 bytes) do Registro via SetupAPI
 *  5. DecodeEdidManufacturerId → decodifica os 3 chars do fabricante
 *  6. ExtractEdidDescriptors → extrai nome e serial dos Descriptor Blocks
 *  7. Calcula DPI a partir de pixels e tamanho físico em mm
 *  8. EnumDisplayMonitors → identifica qual monitor é o primário
 *
 * @return std::vector<MonitorInfo> com uma entrada por monitor físico conectado.
 */
static std::vector<MonitorInfo> EnumerateMonitors() {
	std::vector<MonitorInfo> monitors;

	uint32_t monitor_global_index = 0; // índice global entre todos os monitores

	// ---- Passo 1: Itera adaptadores de vídeo --------------------------------
	// EnumDisplayDevicesW(nullptr, iAdapter, &dd, 0) enumera adaptadores lógicos
	// dd.DeviceName ex.: "\\.\DISPLAY1"
	// dd.StateFlags & DISPLAY_DEVICE_ACTIVE → adaptador está em uso

	DISPLAY_DEVICEW adapter = {};
	adapter.cb = sizeof(adapter); // obrigatório antes de chamar EnumDisplayDevicesW

	for(DWORD i_adapter = 0;
		EnumDisplayDevicesW(nullptr, i_adapter, &adapter, 0);
		++i_adapter)
	{
		// Ignora adaptadores não ativos (desconectados ou desabilitados)
		if(!(adapter.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

		// ---- Passo 2: Itera monitores deste adaptador -----------------------
		// EnumDisplayDevicesW(adapter_name, iMonitor, &monitor_dd, EDD_GET_DEVICE_INTERFACE_NAME)
		// monitor_dd.DeviceID ex.: "MONITOR\DEL4141\{4d36e96e-...}\0001"
		// EDD_GET_DEVICE_INTERFACE_NAME → preenche DeviceID com o caminho completo

		DISPLAY_DEVICEW monitor_dd = {};
		monitor_dd.cb = sizeof(monitor_dd);

		for(DWORD i_mon = 0;
			EnumDisplayDevicesW(adapter.DeviceName, i_mon, &monitor_dd, EDD_GET_DEVICE_INTERFACE_NAME);
			++i_mon)
		{
			// Ignora monitores não ativos (ex.: porta de vídeo sem cabo)
			if(!(monitor_dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

			MonitorInfo mon = {};
			mon.index = monitor_global_index++;

			// device_name é o nome GDI do adaptador (ex.: "\\.\DISPLAY1")
			// Usamos este nome para correlacionar com EnumDisplayMonitors
			mon.device_name = adapter.DeviceName;

			// ---- Passo 3: Lê resolução e refresh rate -----------------------
			// EnumDisplaySettingsW com ENUM_CURRENT_SETTINGS lê a configuração ativa
			DEVMODEW dm = {};
			dm.dmSize = sizeof(dm); // obrigatório
			if(EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
				mon.width_px   = static_cast<uint32_t>(dm.dmPelsWidth);   // resolução H
				mon.height_px  = static_cast<uint32_t>(dm.dmPelsHeight);  // resolução V
				mon.refresh_hz = static_cast<float>(dm.dmDisplayFrequency); // taxa de atualização
			}

			// ---- Passo 4: Lê o EDID do Registro ----------------------------
			// DeviceID do monitor ex.: "MONITOR\DEL4141\{GUID}\0001"
			// Extraímos apenas a parte "MONITOR\DEL4141" para busca no Registro
			std::wstring device_id(monitor_dd.DeviceID);

			// Remove o prefixo "\\" se houver (device interface path começa com \\?\)
			// O DeviceID pode ter formato "\\?\DISPLAY#DELA141#..." (interface path)
			// ou "MONITOR\DEL4141\..." dependendo da flag EDD_GET_DEVICE_INTERFACE_NAME
			// Normalmente o formato é "MONITOR\<MODELID>\<GUID>\<INSTANCE>"
			// Extraímos os dois primeiros segmentos (até o 3º backslash)
			std::wstring monitor_model_id; // ex.: "DEL4141"
			{
				// Encontra os separadores '\' para extrair o model ID
				// DeviceID formato: "MONITOR\DEL4141\{...}\0001"
				const size_t first_bs  = device_id.find(L'\\');
				const size_t second_bs = (first_bs  != std::wstring::npos)
					? device_id.find(L'\\', first_bs  + 1) : std::wstring::npos;

				if(first_bs != std::wstring::npos && second_bs != std::wstring::npos) {
					// Extrai o segment entre os dois backslashes: "DEL4141"
					monitor_model_id = device_id.substr(first_bs + 1,
						second_bs - first_bs - 1);
				}
			}

			// ---- Passo 5: Lê EDID e decodifica -----------------------------
			// O EDID está em HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<ModelID>\...
			// Passamos o model ID para encontrar a chave correta no Registro
			std::vector<uint8_t> edid = ReadEdidFromRegistry(monitor_model_id);

			if(!edid.empty()) {
				std::span<const uint8_t> edid_span(edid); // span para acesso seguro

				// Decodifica o Manufacturer ID do EDID (bytes [8-9])
				const std::wstring mfr_code = DecodeEdidManufacturerId(edid_span);
				mon.manufacturer = EdidCodeToManufacturerName(mfr_code);

				// Extrai nome e serial number dos Descriptor Blocks (offsets 54-125)
				ExtractEdidDescriptors(edid_span, mon.friendly_name, mon.serial_number);

				// Tamanho físico do painel em centímetros → convertemos para mm
				// Byte [21] = largura máxima em cm, byte [22] = altura máxima em cm
				// Multiplicamos por 10 para converter de cm para mm
				if(edid.size() >= 23) {
					mon.physical_width_mm  = static_cast<uint32_t>(edid[21]) * 10;
					mon.physical_height_mm = static_cast<uint32_t>(edid[22]) * 10;
				}

				// ---- Passo 6: Calcula DPI -----------------------------------
				// DPI = pixels / (milímetros / 25.4)
				// 25.4 mm = 1 polegada (definição padrão)
				if(mon.physical_width_mm  > 0 && mon.width_px  > 0)
					mon.dpi_x = static_cast<float>(mon.width_px)
						/ (static_cast<float>(mon.physical_width_mm) / 25.4f);

				if(mon.physical_height_mm > 0 && mon.height_px > 0)
					mon.dpi_y = static_cast<float>(mon.height_px)
						/ (static_cast<float>(mon.physical_height_mm) / 25.4f);
			}

			// Se o EDID não forneceu nome, usa o DeviceString do GDI como fallback
			// DeviceString ex.: "Generic PnP Monitor" ou "Dell U2722D"
			if(mon.friendly_name.empty()) {
				mon.friendly_name = monitor_dd.DeviceString;
				// Remove trailing spaces que o GDI às vezes adiciona
				while(!mon.friendly_name.empty() && mon.friendly_name.back() == L' ')
					mon.friendly_name.pop_back();
			}

			// Se o EDID não forneceu fabricante, deixa vazio (será exibido como N/A)
			if(mon.manufacturer.empty())
				mon.manufacturer = L"Desconhecido";

			monitors.push_back(std::move(mon));
		}
	}

	// ---- Passo 7: Marca o monitor primário via EnumDisplayMonitors ----------
	// EnumDisplayMonitors itera todos os HMONITORs e chama MonitorEnumProc
	// Passamos o ponteiro para o vector como LPARAM para o callback
	EnumDisplayMonitors(
		nullptr, // HDC nullptr = enumera todos os monitores
		nullptr, // rect nullptr = sem clipping
		MonitorEnumProc,
		std::bit_cast<LPARAM>(&monitors)); // passado como dwData ao callback

	return monitors;
}

// ============================================================================
// SystemInfo::Collect
// ============================================================================

/**
 * @brief Preenche e retorna um SystemInfo completo.
 *
 * @param vk           VulkanContext já inicializado (nullptr → Vulkan em N/A).
 * @param current_api  Nome da API em uso (nullptr → inferido de vk).
 * @return             SystemInfo completamente preenchido.
 */
SystemInfo SystemInfo::Collect(VulkanContext* vk, const wchar_t* current_api) {
	SystemInfo info;

	// ---- CPU ----------------------------------------------------------------
	SYSTEM_INFO si = {};
	GetSystemInfo(&si);
	info.cpu_logical_cores  = static_cast<uint32_t>(si.dwNumberOfProcessors);
	info.cpu_physical_cores = CountPhysicalCores(); // usa GetLogicalProcessorInformation

	info.cpu_name = ReadCpuNameFromRegistry();

	// ---- RAM + paginação ----------------------------------------------------
	MEMORYSTATUSEX mem = {};
	mem.dwLength = sizeof(mem);
	if(GlobalMemoryStatusEx(&mem)) {
		info.ram_bytes        = mem.ullTotalPhys;
		info.page_total_bytes = mem.ullTotalPageFile;
		info.page_avail_bytes = mem.ullAvailPageFile;
	}

	// ---- SO -----------------------------------------------------------------
	info.os_name = ReadOsName();

	// ---- GPUs (DXGI) + DX12 Ultimate ----------------------------------------
	info.gpus = EnumerateGpus();
	CollectDX12Ultimate(info.gpus); // preenche dx12_ultimate e dx12_ultimate_str

	// ---- Vulkan -------------------------------------------------------------
	info.vulkan = CollectVulkanInfo(vk);

	// ---- DirectX ------------------------------------------------------------
	info.directx = CollectDirectXInfo();

	// ---- OpenGL -------------------------------------------------------------
	info.opengl = CollectOpenGLInfo();

	// ---- Discos -------------------------------------------------------------
	info.disks = EnumerateDisks();

	// ---- Máquina ------------------------------------------------------------
	info.machine = ReadMachineInfo();

	// ---- Monitores ----------------------------------------------------------
	// EnumerateMonitors combina EnumDisplayDevices + EDID + EnumDisplayMonitors
	info.monitors = EnumerateMonitors();

	// ---- API atual ----------------------------------------------------------
	if(current_api && current_api[0] != L'\0')
		info.current_api = current_api;
	else if(vk)
		info.current_api = L"Vulkan";
	else
		info.current_api = L"Desconhecida";

	return info;
}

// ============================================================================
// SystemInfo::PrintToConsole
// ============================================================================

/**
 * @brief Envia as especificações para o Console ImGui via AddLog.
 *
 * Seções exibidas:
 *   1. Máquina     — fabricante, modelo, nome, BIOS
 *   2. API em Uso
 *   3. SO
 *   4. CPU         — nome, núcleos físicos, núcleos lógicos
 *   5. Memória     — RAM, paginação total, paginação livre
 *   6. GPU(s)      — nome, VRAM dedicada, VRAM compartilhada, DX12 Ultimate
 *   7. Vulkan
 *   8. DirectX
 *   9. OpenGL
 *  10. Discos      — tipo (SSD/HDD), nome, capacidade
 *  11. Monitores   — marca, modelo, resolução, DPI, refresh rate, tamanho físico
 *
 * @param con  Ponteiro não-nulo para o Console ImGui.
 */
void SystemInfo::PrintToConsole(Console* con) const {
	if(!con) return;

	// Lambda: converte bytes para GiB (gibibytes, base 2)
	auto toGiB = [](uint64_t b) {
		return static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0);
	};

	con->AddLog(L"\n[yellow]\u2699 === ESPECIFICACOES DO SISTEMA ===[/]\n"); // ⚙

	// ---- Máquina ------------------------------------------------------------

	con->AddLog(L"[yellow]\U0001F3E0 MAQUINA[/]"); // 🏠
	con->AddLog(L"  [cyan]Nome do PC:[/]    [green]%ls[/]", machine.computer_name.c_str());
	con->AddLog(L"  [cyan]Fabricante:[/]    [green]%ls[/]", machine.manufacturer.c_str());
	con->AddLog(L"  [cyan]Modelo:[/]        [green]%ls[/]", machine.model.c_str());
	con->AddLog(L"  [cyan]BIOS:[/]          [gray]%ls[/]",  machine.bios_version.c_str());

	// ---- API em Uso ---------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F4E1 API GRAFICA EM USO[/]"); // 📡
	con->AddLog(L"  [cyan]API:[/]  [green]%ls[/]", current_api.c_str());

	// ---- SO -----------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F4BB SISTEMA OPERACIONAL[/]"); // 💻
	con->AddLog(L"  [cyan]SO:[/]  [green]%ls[/]", os_name.c_str());

	// ---- CPU ----------------------------------------------------------------

	con->AddLog(L"\n[yellow]\u26A1 PROCESSADOR[/]"); // ⚡
	con->AddLog(L"  [cyan]Modelo:[/]          [green]%ls[/]", cpu_name.c_str());
	con->AddLog(L"  [cyan]Nucleos fisicos:[/]  [green]%u[/]  [gray](cores reais, sem HT)[/]",
		cpu_physical_cores);
	con->AddLog(L"  [cyan]Nucleos logicos:[/]  [green]%u[/]  [gray](threads totais com HT)[/]",
		cpu_logical_cores);

	// ---- Memória ------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F4BE MEMORIA[/]"); // 💾
	if(ram_bytes > 0)
		con->AddLog(L"  [cyan]RAM fisica:[/]      [green]%.1f GiB[/]  [gray](%llu bytes)[/]",
			toGiB(ram_bytes), ram_bytes);
	if(page_total_bytes > 0) {
		con->AddLog(L"  [cyan]Paginacao total:[/]  [green]%.1f GiB[/]  [gray](%llu bytes)[/]",
			toGiB(page_total_bytes), page_total_bytes);
		con->AddLog(L"  [cyan]Paginacao livre:[/]  [green]%.1f GiB[/]  [gray](%llu bytes)[/]",
			toGiB(page_avail_bytes), page_avail_bytes);
	}

	// ---- GPUs ---------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F3AE GPU(S) — DXGI[/]"); // 🎮
	if(gpus.empty()) {
		con->AddLog(L"  [gray]Nenhuma GPU fisica detectada.[/]");
	} else {
		for(size_t i = 0; i < gpus.size(); ++i) {
			const GpuInfo& g = gpus[i];
			con->AddLog(L"  [cyan]GPU %zu:[/] [green]%ls[/]", i + 1, g.name.c_str());

			if(g.vram_bytes > 0)
				con->AddLog(L"    [cyan]VRAM dedicada:[/]     [green]%.1f GiB[/]  [gray](%llu bytes)[/]",
					toGiB(g.vram_bytes), g.vram_bytes);
			else
				con->AddLog(L"    [cyan]VRAM dedicada:[/]     [gray]0 (usa RAM do sistema)[/]");

			if(g.shared_bytes > 0)
				con->AddLog(L"    [cyan]Mem. compartilhada:[/] [green]%.1f GiB[/]  [gray](RAM usavel como VRAM)[/]",
					toGiB(g.shared_bytes));

			// DX12 Ultimate: mostra ícone colorido + detalhe de cada feature
			if(g.dx12_ultimate)
				con->AddLog(L"    [cyan]DX12 Ultimate:[/]     [green]SIM \u2713[/]");
			else
				con->AddLog(L"    [cyan]DX12 Ultimate:[/]     [red]NAO[/]");

			// Detalha os 4 tiers linha a linha (dx12_ultimate_str usa '\n' como separador)
			std::wstring detail = g.dx12_ultimate_str;
			size_t pos = 0;
			while(pos < detail.size()) {
				size_t nl = detail.find(L'\n', pos);
				std::wstring line = detail.substr(pos,
					(nl == std::wstring::npos ? detail.size() : nl) - pos);
				bool ok = (line.rfind(L"[ok]", 0) == 0); // [ok] = verde, [no] = cinza
				con->AddLog(ok ? L"      [green]%ls[/]" : L"      [gray]%ls[/]", line.c_str());
				if(nl == std::wstring::npos) break;
				pos = nl + 1;
			}
		}
	}

	// ---- Vulkan -------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F527 VULKAN[/]"); // 🔧
	con->AddLog(L"  [cyan]Versao da instancia:[/]  [green]%ls[/]  [gray](runtime do SO)[/]",
		vulkan.instance_str.c_str());
	if(vulkan.api_version > 0) {
		con->AddLog(L"  [cyan]GPU ativa:[/]           [green]%ls[/]", vulkan.device_name.c_str());
		con->AddLog(L"  [cyan]API max da GPU:[/]      [green]%ls[/]", vulkan.api_str.c_str());
		con->AddLog(L"  [cyan]Versao do driver:[/]    [green]%ls[/]", vulkan.driver_str.c_str());
	} else {
		con->AddLog(L"  [gray]%ls[/]", vulkan.driver_str.c_str());
	}

	// ---- DirectX ------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F4E6 DIRECTX[/]"); // 📦
	con->AddLog(L"  [cyan]Versao maxima:[/]  [green]%ls[/]", directx.max_version.c_str());
	con->AddLog(L"  [cyan]DX12:[/]           [green]%ls[/]", directx.dx12_support.c_str());
	con->AddLog(L"  [cyan]DX11 max FL:[/]    [green]%ls[/]", directx.dx11_max.c_str());

	// ---- OpenGL -------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F5A5 OPENGL[/]"); // 🖥
	if(opengl.version != L"Nao disponivel") {
		con->AddLog(L"  [cyan]Versao:[/]    [green]%ls[/]", opengl.version.c_str());
		con->AddLog(L"  [cyan]Vendor:[/]    [green]%ls[/]", opengl.vendor.c_str());
		con->AddLog(L"  [cyan]Renderer:[/]  [green]%ls[/]", opengl.renderer.c_str());
	} else {
		con->AddLog(L"  [gray]OpenGL nao disponivel.[/]");
	}

	// ---- Discos -------------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F4BF DISCOS[/]"); // 💿
	if(disks.empty()) {
		con->AddLog(L"  [gray]Nenhum disco detectado (requer privilegios de administrador).[/]");
	} else {
		for(const DiskInfo& d : disks) {
			// Cor do tipo: SSD = verde, HDD = amarelo, Desconhecido = cinza
			const wchar_t* type_color = (d.type == L"SSD") ? L"[green]"
				: (d.type == L"HDD") ? L"[yellow]"
				: L"[gray]";
			con->AddLog(L"  [cyan]Disco %u:[/] %ls%ls[/]  [green]%ls[/]",
				d.index, type_color, d.type.c_str(), d.friendly_name.c_str());

			if(d.size_bytes > 0)
				con->AddLog(L"    [cyan]Capacidade:[/]  [green]%.1f GiB[/]  [gray](%.0f GB)[/]",
					toGiB(d.size_bytes),
					static_cast<double>(d.size_bytes) / (1000.0 * 1000.0 * 1000.0));
		}
	}

	// ---- Monitores ----------------------------------------------------------

	con->AddLog(L"\n[yellow]\U0001F5B5 MONITORES[/]"); // 🖵
	if(monitors.empty()) {
		con->AddLog(L"  [gray]Nenhum monitor detectado.[/]");
	} else {
		// Exibe o total de monitores encontrados
		con->AddLog(L"  [cyan]Total:[/] [green]%zu monitor(es)[/]", monitors.size());

		for(const MonitorInfo& m : monitors) {
			// Cabeçalho do monitor: índice + nome amigável + flag de primário
			if(m.is_primary)
				con->AddLog(L"\n  [cyan]Monitor %u:[/] [green]%ls[/] [yellow](PRIMARIO)[/]",
					m.index + 1, m.friendly_name.c_str());
			else
				con->AddLog(L"\n  [cyan]Monitor %u:[/] [green]%ls[/]",
					m.index + 1, m.friendly_name.c_str());

			// Fabricante decodificado do EDID
			con->AddLog(L"    [cyan]Fabricante:[/]   [green]%ls[/]",
				m.manufacturer.c_str());

			// Serial number (pode estar vazio se o monitor não reportar)
			if(!m.serial_number.empty())
				con->AddLog(L"    [cyan]Serial:[/]       [gray]%ls[/]",
					m.serial_number.c_str());

			// Resolução atual em pixels e taxa de atualização
			if(m.width_px > 0 && m.height_px > 0)
				con->AddLog(L"    [cyan]Resolucao:[/]    [green]%u x %u px[/]  "
					L"[gray]@ %.0f Hz[/]",
					m.width_px, m.height_px, m.refresh_hz);

			// Tamanho físico do painel em milímetros e polegadas calculadas
			// Diagonal física = sqrt(w² + h²) / 25.4 para obter polegadas
			if(m.physical_width_mm > 0 && m.physical_height_mm > 0) {
				// Calcula diagonal em polegadas para o usuário
				const float diag_mm = std::sqrtf(
					static_cast<float>(m.physical_width_mm)  * static_cast<float>(m.physical_width_mm)  +
					static_cast<float>(m.physical_height_mm) * static_cast<float>(m.physical_height_mm));
				const float diag_in = diag_mm / 25.4f; // 25.4 mm = 1 polegada

				con->AddLog(L"    [cyan]Tamanho:[/]      [green]%u x %u mm[/]  "
					L"[gray](%.1f\" diagonal)[/]",
					m.physical_width_mm, m.physical_height_mm, diag_in);
			}

			// DPI horizontal e vertical calculados a partir do EDID
			if(m.dpi_x > 0.0f && m.dpi_y > 0.0f) {
				// Mostra DPI médio (normalmente dpi_x ≈ dpi_y para painéis quadrados de pixel)
				const float dpi_avg = (m.dpi_x + m.dpi_y) * 0.5f;
				con->AddLog(L"    [cyan]DPI:[/]          [green]%.0f DPI[/]  "
					L"[gray](H:%.1f  V:%.1f)[/]",
					dpi_avg, m.dpi_x, m.dpi_y);

				// Percentual de escala equivalente (96 DPI = 100% no Windows)
				// Isso corresponde ao slider de escala em Configurações → Display
				constexpr float k_base_dpi = 96.0f; // DPI de referência do Windows (100%)
				const float scale_pct = (dpi_avg / k_base_dpi) * 100.0f;
				con->AddLog(L"    [cyan]Escala equiv.:[/] [gray]%.0f%% (base 96 DPI)[/]",
					scale_pct);
			}

			// Nome GDI interno do display (\\.\DISPLAY1)
			con->AddLog(L"    [cyan]Device GDI:[/]   [gray]%ls[/]",
				m.device_name.c_str());
		}
	}

	// ---- Rodapé -------------------------------------------------------------

	con->AddLog(L"\n[yellow]\u2713 Coleta concluida.[/]\n"); // ✓
}