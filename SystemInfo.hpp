#pragma once

#include "pch.hpp"
// ============================================================================
// GpuInfo — uma entrada por GPU física detectada via DXGI
// ============================================================================

struct GpuInfo {
    std::wstring name;              ///< Nome do adaptador  ex.: "NVIDIA GeForce RTX 4090"
    std::wstring driver_ver;        ///< Versão do driver (preenchida pela seção Vulkan)
    uint64_t     vram_bytes;        ///< VRAM dedicada em bytes (DedicatedVideoMemory)
    uint64_t     shared_bytes;      ///< Memória compartilhada com RAM (SharedSystemMemory)
    bool         dx12_ultimate;     ///< true se a GPU suporta DX12 Ultimate (4 features obrigatórias)
    std::wstring dx12_ultimate_str; ///< Descrição detalhada dos tiers DX12 Ultimate
};

// ============================================================================
// VulkanInfo — versões da instância Vulkan e da GPU ativa
// ============================================================================

struct VulkanInfo {
    uint32_t     instance_version; ///< vkEnumerateInstanceVersion (runtime do SO)
    std::wstring instance_str;     ///< ex.: L"1.3.290"
    std::wstring driver_str;       ///< Versão do driver decodificada por fabricante
    std::wstring device_name;      ///< Nome do physical device reportado pelo driver
    uint32_t     api_version;      ///< VkPhysicalDeviceProperties::apiVersion (máx. da GPU)
    std::wstring api_str;          ///< ex.: L"1.3.277"
};

// ============================================================================
// DirectXInfo — versão máxima de DirectX suportada pelo hardware
// ============================================================================

struct DirectXInfo {
    std::wstring dx12_support;  ///< ex.: L"Sim (Feature Level 12_1)" ou L"Nao disponivel"
    std::wstring dx11_max;      ///< ex.: L"11_1" ou L"N/A"
    std::wstring max_version;   ///< ex.: L"DirectX 12 (FL 12_1)"
};

// ============================================================================
// OpenGLInfo — versão OpenGL reportada pelo driver
// ============================================================================

struct OpenGLInfo {
    std::wstring version;    ///< ex.: L"4.6.0 NVIDIA 561.09"
    std::wstring vendor;     ///< ex.: L"NVIDIA Corporation"
    std::wstring renderer;   ///< ex.: L"NVIDIA GeForce RTX 4090/PCIe/SSE2"
};

// ============================================================================
// DiskInfo — uma entrada por disco físico detectado
// ============================================================================

struct DiskInfo {
    std::wstring device_path;   ///< ex.: L"\\\\.\\PhysicalDrive0"
    std::wstring friendly_name; ///< ex.: L"Samsung SSD 980 PRO 1TB"
    std::wstring type;          ///< L"SSD", L"HDD" ou L"Desconhecido"
    uint64_t     size_bytes;    ///< Capacidade total em bytes
    uint32_t     index;         ///< Índice do disco (0, 1, 2...)
};

// ============================================================================
// MachineInfo — fabricante, modelo e nome do computador
// ============================================================================

struct MachineInfo {
    std::wstring manufacturer;    ///< ex.: L"ASUS", L"Dell Inc.", L"Gigabyte Technology"
    std::wstring model;           ///< ex.: L"ROG STRIX Z790-E", L"Inspiron 15 3000"
    std::wstring computer_name;   ///< Nome de rede do PC (ex.: L"DESKTOP-A1B2C3")
    std::wstring bios_version;    ///< ex.: L"American Megatrends Inc. 2601 26/09/2023"
};

// ============================================================================
// SystemInfo — struct principal com todos os campos
// ============================================================================

struct SystemInfo {
    std::wstring             cpu_name;          ///< Nome completo do processador
    uint32_t                 cpu_logical_cores; ///< Núcleos lógicos (threads)
    uint32_t                 cpu_physical_cores;///< Núcleos físicos (sem hyperthreading)
    uint64_t                 ram_bytes;         ///< RAM física total em bytes
    uint64_t                 page_total_bytes;  ///< Tamanho total do arquivo de paginação
    uint64_t                 page_avail_bytes;  ///< Paginação disponível atualmente
    std::wstring             os_name;           ///< ex.: L"Windows 11 (Build 22631)"
    std::vector<GpuInfo>     gpus;              ///< GPUs detectadas via DXGI
    VulkanInfo               vulkan;            ///< Versões Vulkan
    DirectXInfo              directx;           ///< Versão máxima DirectX
    OpenGLInfo               opengl;            ///< Versão OpenGL
    std::vector<DiskInfo>    disks;             ///< Discos físicos (SSD/HDD)
    MachineInfo              machine;           ///< Fabricante, modelo, nome do PC
    std::wstring             current_api;       ///< API gráfica em uso

    /**
     * @brief Preenche todos os campos consultando as APIs do SO.
     *
     * @param vk           VulkanContext já inicializado (nullptr → campos Vulkan em N/A).
     * @param current_api  Nome da API em uso (nullptr → inferido de vk).
     */
    static SystemInfo Collect(class VulkanContext* vk = nullptr,
        const wchar_t* current_api = nullptr);

    /**
     * @brief Envia todas as informações ao Console ImGui via AddLog.
     * @param con  Ponteiro não-nulo para o Console.
     */
    void PrintToConsole(class Console* con) const;
};   
