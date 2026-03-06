/**
 * @file ImageViewerFactory.cpp
 * @brief Factory de janelas de imagem — destruição adiada para após o submit.
 *
 * CAUSA DO VK_ERROR_DEVICE_LOST (-4)
 * ------------------------------------
 * A versão anterior chamava vkDeviceWaitIdle() dentro de RemoveClosed(),
 * que era chamado dentro de DrawAll(), que era chamado entre
 * ImGui::NewFrame() e ImGui::Render():
 *
 *   ImGui::NewFrame()
 *     DrawAll()
 *       RemoveClosed()
 *         vkDeviceWaitIdle()   ← AQUI: frame em construção, submit ainda não ocorreu
 *         ~Image()             ← descriptor sets destruídos
 *   ImGui::Render()
 *   FrameRender()
 *     vkQueueSubmit()          ← CRASH: device lost — estado Vulkan corrompido
 *
 * POR QUE CAUSA DEVICE LOST:
 *   vkDeviceWaitIdle() a meio de um frame pode apanhar semáforos em estado
 *   indefinido (image_acquired_semaphore já sinalizado mas ainda não consumido)
 *   e pode colidir com pools de comandos e descritores ainda em uso interno
 *   pelo driver. O resultado é corrupção de estado que se manifesta como
 *   VK_ERROR_DEVICE_LOST no vkQueueSubmit seguinte.
 *
 * SOLUÇÃO — DESTRUIÇÃO ADIADA (deferred destroy)
 * ------------------------------------------------
 * DrawAll() detecta janelas fechadas e move-as para m_pending_destroy.
 * Não chama nenhuma API Vulkan — só ImGui.
 *
 * PostFrameCleanup() é chamado DEPOIS de FrameRender() + FramePresent().
 * Nesse ponto o vkQueueSubmit já ocorreu, a GPU está a trabalhar ou terminou,
 * e é seguro fazer vkDeviceWaitIdle() + destruir os recursos.
 *
 *   ImGui::NewFrame()
 *     DrawAll()                ← só ImGui; move fechadas para m_pending_destroy
 *   ImGui::Render()
 *   FrameRender()              ← vkQueueSubmit com todos os recursos ainda vivos
 *   FramePresent()             ← vkQueuePresentKHR
 *   PostFrameCleanup()         ← agora seguro: vkDeviceWaitIdle + destruição
 *
 * INTEGRAÇÃO NO LOOP PRINCIPAL
 * ------------------------------
 * No ficheiro onde o loop reside (ex.: App.cpp ou MyWindows.cpp):
 *
 *   m_factory.DrawAll();                          // entre NewFrame e Render
 *   // ... ImGui::Render(), FrameRender(), FramePresent() ...
 *   m_factory.PostFrameCleanup();                 // após FramePresent
 */

#include "pch.hpp"
#include "ImageViewerFactory.hpp"
#include "Memory.hpp"        // Memory::GetVulkan()
#include "VulkanContext_Wrapper.hpp" // VulkanContext::GetDevice()

#include <shobjidl.h>    // IFileOpenDialog, IShellItemArray, COMDLG_FILTERSPEC
#include <combaseapi.h>  // CoInitializeEx, CoUninitialize

#include <winrt/base.h>   // winrt::com_ptr, winrt::check_hresult, winrt::hresult_error
#include <wil/resource.h> // wil::unique_cotaskmem_string

// ============================================================================
// Construtor & Destrutor
// ============================================================================

/**
 * @brief Inicializa COM (STA — obrigatório para IFileOpenDialog).
 *
 * S_OK    → nós inicializamos → m_com_init = true
 * S_FALSE → já inicializado   → m_com_init = false
 */
ImageViewerFactory::ImageViewerFactory() noexcept
    : m_next_id(1)
    , m_com_init(false)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_com_init = (hr == S_OK);
}

/**
 * @brief Destrói todas as janelas (com GPU sync) e uninit COM.
 *
 * Chamado durante shutdown, fora do loop de render — vkDeviceWaitIdle
 * é seguro neste contexto.
 */
ImageViewerFactory::~ImageViewerFactory() noexcept
{
    CloseAll();

    if(m_com_init)
        CoUninitialize();
}

// ============================================================================
// OpenFileDialog
// ============================================================================

/**
 * @brief Abre o explorador do Windows com suporte a multi-seleção.
 *
 * wil::unique_cotaskmem_string:
 *   operator& devolve wchar_t** compatível com GetDisplayName.
 *   Destrutor chama CoTaskMemFree automaticamente.
 *
 * @return true se pelo menos uma imagem foi aberta.
 */
bool ImageViewerFactory::OpenFileDialog()
{
    try
    {
        winrt::com_ptr<IFileOpenDialog> dlg;
        winrt::check_hresult(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(dlg.put())));

        // Filtros
        std::array<COMDLG_FILTERSPEC, FILE_FILTERS.size()> specs{};
        for(std::size_t i = 0; i < FILE_FILTERS.size(); ++i)
        {
            specs[i].pszName = FILE_FILTERS[i].first;
            specs[i].pszSpec = FILE_FILTERS[i].second;
        }
        winrt::check_hresult(dlg->SetFileTypes(
            static_cast<UINT>(specs.size()), specs.data()));

        dlg->SetFileTypeIndex(1);
        dlg->SetTitle(L"Selecionar imagem");

        FILEOPENDIALOGOPTIONS opts = 0;
        winrt::check_hresult(dlg->GetOptions(&opts));
        winrt::check_hresult(dlg->SetOptions(
            opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST));

        winrt::check_hresult(dlg->Show(nullptr));

        winrt::com_ptr<IShellItemArray> items;
        winrt::check_hresult(dlg->GetResults(items.put()));

        DWORD count = 0;
        winrt::check_hresult(items->GetCount(&count));

        bool opened_any = false;

        for(DWORD i = 0; i < count; ++i)
        {
            winrt::com_ptr<IShellItem> item;
            winrt::check_hresult(items->GetItemAt(i, item.put()));

            // wil::unique_cotaskmem_string — RAII para CoTaskMemAlloc
            // operator& = wchar_t** para GetDisplayName
            // destrutor → CoTaskMemFree() ✓
            wil::unique_cotaskmem_string raw_path;
            if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)))
                continue;

            std::wstring filepath(raw_path.get());
            // ~unique_cotaskmem_string() → CoTaskMemFree() aqui ✓

            if(OpenFile(std::move(filepath)) != nullptr)
                opened_any = true;
        }

        return opened_any;
    }
    catch(const winrt::hresult_error& e)
    {
        constexpr HRESULT CANCELLED = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if(e.code() != winrt::hresult{CANCELLED})
        {
            fprintf(stderr, "[ImageViewerFactory] Erro: %ls (0x%08X)\n",
                e.message().c_str(),
                static_cast<uint32_t>(static_cast<HRESULT>(e.code())));
        }
        return false;
    }
}

// ============================================================================
// OpenFile
// ============================================================================

/**
 * @brief Cria uma nova ImageViewerWindow para o caminho especificado.
 */
ImageViewerWindow* ImageViewerFactory::OpenFile(std::wstring filepath)
{
    if(filepath.empty())
        return nullptr;

    const int id = m_next_id++;

    auto& ref = m_windows.emplace_back(
        std::make_unique<ImageViewerWindow>(
            id,
            std::move(filepath),
            [this]{ OpenFileDialog(); })); // callback para botão [+ Nova imagem]

    return ref.get();
}

// ============================================================================
// DrawAll — FASE 1: só ImGui, zero Vulkan
// ============================================================================

/**
 * @brief Desenha todas as janelas e move as fechadas para m_pending_destroy.
 *
 * NÃO CHAMA NENHUMA API VULKAN.
 * A destruição dos recursos Vulkan acontece em PostFrameCleanup(),
 * depois de FrameRender() + FramePresent().
 *
 * Janelas fechadas são movidas (std::move do unique_ptr) para
 * m_pending_destroy — os objectos ImageViewerWindow ainda existem
 * em memória até PostFrameCleanup() confirmar que a GPU terminou.
 */
void ImageViewerFactory::DrawAll()
{
    for(const auto& win : m_windows)
        win->Draw(); // ImGui::Begin/Image/End — sem Vulkan directo

    // Move janelas fechadas para a fila de destruição adiada
    // O unique_ptr é movido — m_windows deixa de possuir o objecto
    for(auto& win : m_windows)
    {
        if(!win->IsOpen())
            m_pending_destroy.push_back(std::move(win));
    }

    // Remove os slots nulos deixados pelo move (unique_ptr movido = nullptr)
    std::erase_if(m_windows,
        [](const std::unique_ptr<ImageViewerWindow>& w){ return w == nullptr; });
}

// ============================================================================
// PostFrameCleanup — FASE 2: GPU sync + destruição, após FramePresent
// ============================================================================

/**
 * @brief Destrói recursos Vulkan de janelas fechadas — seguro após submit.
 *
 * QUANDO CHAMAR:
 *   Depois de FrameRender() + FramePresent(), nunca antes.
 *
 *   CORRECTO:
 *     DrawAll()           ← fase ImGui
 *     ImGui::Render()
 *     FrameRender()       ← vkQueueSubmit
 *     FramePresent()      ← vkQueuePresentKHR
 *     PostFrameCleanup()  ← vkDeviceWaitIdle + destruição  ✓
 *
 *   ERRADO (causa VK_ERROR_DEVICE_LOST):
 *     DrawAll()
 *       vkDeviceWaitIdle()  ← mid-frame, antes do submit    ✗
 *     ImGui::Render()
 *     FrameRender()         ← device lost aqui
 *
 * Se não houver janelas pendentes, retorna imediatamente sem stall.
 */
void ImageViewerFactory::PostFrameCleanup()
{
    if(m_pending_destroy.empty())
        return; // nenhuma janela fechou neste frame — sem stall

    // A GPU pode ainda estar a usar os descriptor sets do frame que acabou
    // de ser submetido. vkDeviceWaitIdle garante que terminou TUDO.
    // Seguro aqui porque FrameRender() + FramePresent() já correram.
    vkDeviceWaitIdle(Memory::Get()->GetVulkan()->GetDevice());

    // Destruição segura: GPU confirmadamente idle
    // ~ImageViewerWindow() → ~Image() → Image::Unload() → Vulkan free
    m_pending_destroy.clear();
}

// ============================================================================
// DrawOpenButton
// ============================================================================

/** @brief Botão "Abrir Imagem..." embutível em qualquer janela ImGui activa. */
void ImageViewerFactory::DrawOpenButton()
{
    if(ImGui::Button("Abrir Imagem..."))
        OpenFileDialog();

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Suporta selecção múltipla (Ctrl/Shift + Clique).");

    if(!m_windows.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu aberta%s)",
            m_windows.size(),
            m_windows.size() == 1 ? "" : "s");
    }
}

// ============================================================================
// CloseAll
// ============================================================================

/**
 * @brief Fecha todas as janelas com GPU sync.
 *
 * Só chamar fora do loop de render (ex.: shutdown do app).
 * vkDeviceWaitIdle é seguro nesse contexto.
 */
void ImageViewerFactory::CloseAll()
{
    if(m_windows.empty() && m_pending_destroy.empty())
        return;

    vkDeviceWaitIdle(Memory::Get()->GetVulkan()->GetDevice());

    m_pending_destroy.clear(); // destrói pendentes primeiro
    m_windows.clear();         // depois as vivas
}

// ============================================================================
// GetOpenCount
// ============================================================================

std::size_t ImageViewerFactory::GetOpenCount() const noexcept
{
    return m_windows.size();
}