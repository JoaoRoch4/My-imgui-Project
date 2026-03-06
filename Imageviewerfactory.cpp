/**
 * @file ImageViewerFactory.cpp
 * @brief Factory de janelas de imagem — Implementação moderna com C++/WinRT.
 * * Esta implementação utiliza winrt::com_ptr para gerenciamento de vida de objetos COM
 * e winrt::cotaskmem_ptr para buffers alocados pelo shell (CoTaskMemAlloc).
 */

#include "pch.hpp"
#include "ImageViewerFactory.hpp"



// ============================================================================
// Construtor & Destrutor
// ============================================================================

ImageViewerFactory::ImageViewerFactory() noexcept
    : m_next_id(1)
    , m_com_init(false)
{
    // IFileOpenDialog exige STA (Single Threaded Apartment)
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // S_OK: Inicializamos com sucesso
    // S_FALSE: COM já estava inicializado nesta thread
    m_com_init = (hr == S_OK); 
}

ImageViewerFactory::~ImageViewerFactory() noexcept
{
    CloseAll(); // Garante que janelas (e texturas Vulkan) sejam limpas
    
    if (m_com_init)
    {
        CoUninitialize();
    }
}

// ============================================================================
// Diálogo de Arquivo (Multi-seleção)
// ============================================================================

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

        // Configuração de Filtros
        std::array<COMDLG_FILTERSPEC, FILE_FILTERS.size()> specs{};
        for (std::size_t i = 0; i < FILE_FILTERS.size(); ++i)
        {
            specs[i].pszName = FILE_FILTERS[i].first;
            specs[i].pszSpec = FILE_FILTERS[i].second;
        }

        winrt::check_hresult(dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data()));
        
        FILEOPENDIALOGOPTIONS opts = 0;
        winrt::check_hresult(dlg->GetOptions(&opts));
        winrt::check_hresult(dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST));

        winrt::check_hresult(dlg->Show(nullptr));

        winrt::com_ptr<IShellItemArray> items;
        winrt::check_hresult(dlg->GetResults(items.put()));

        DWORD count = 0;
        winrt::check_hresult(items->GetCount(&count));

        bool opened_any = false;

        for (DWORD i = 0; i < count; ++i)
        {
            winrt::com_ptr<IShellItem> item;
            winrt::check_hresult(items->GetItemAt(i, item.put()));

            // SOLUÇÃO: Usando wil::unique_cotaskmem_string para máxima compatibilidade
            // Ele funciona exatamente como o cotaskmem_ptr, chamando CoTaskMemFree automaticamente.
            wil::unique_cotaskmem_string raw_path;
            
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, raw_path.put())))
            {
                if (OpenFile(raw_path.get()) != nullptr)
                {
                    opened_any = true;
                }
            }
        }

        return opened_any;
    }
    catch (const winrt::hresult_error& e)
    {
        if (e.code() != winrt::hresult(HRESULT_FROM_WIN32(ERROR_CANCELLED)))
        {
            fprintf(stderr, "[ImageViewerFactory] Erro: %ls (0x%08X)\n",
                e.message().c_str(), static_cast<uint32_t>(e.code()));
        }
        return false;
    }
}
// ============================================================================
// Gestão de Janelas
// ============================================================================

ImageViewerWindow* ImageViewerFactory::OpenFile(std::wstring filepath)
{
    if (filepath.empty()) return nullptr;

    const int id = m_next_id++;
    
    // Cria a janela e armazena no container de posse (unique_ptr)
    auto& ref = m_windows.emplace_back(
        std::make_unique<ImageViewerWindow>(id, std::move(filepath))
    );

    return ref.get(); 
}

void ImageViewerFactory::DrawAll()
{
    // Desenha cada janela individualmente
    for (const auto& win : m_windows)
    {
        win->Draw(); 
    }

    // Limpeza: remove janelas que o usuário fechou durante o frame
    RemoveClosed();
}

void ImageViewerFactory::DrawOpenButton()
{
    if (ImGui::Button("Abrir Imagem..."))
    {
        OpenFileDialog();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Suporta seleção múltipla (Ctrl/Shift + Clique).");
    }

    if (!m_windows.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu abertas)", m_windows.size());
    }
}

void ImageViewerFactory::CloseAll()
{
    // Limpar o vetor dispara os destrutores de unique_ptr,
    // que por sua vez limpam os recursos de vídeo (Vulkan/DX12).
    m_windows.clear();
}

std::size_t ImageViewerFactory::GetOpenCount() const noexcept
{
    return m_windows.size();
}

void ImageViewerFactory::RemoveClosed()
{
    // C++20 Erase-if para remover janelas onde IsOpen() retornou false
    std::erase_if(m_windows, [](const std::unique_ptr<ImageViewerWindow>& w) {
        return !w->IsOpen();
    });
}