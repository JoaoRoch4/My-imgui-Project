#include "pch.hpp"
#include "OnlineClock.hpp"


OnlineClock::OnlineClock() {
    WSADATA w;
    WSAStartup(std::bit_cast<WORD>(MAKEWORD(2, 2)), &w);
}

OnlineClock::~OnlineClock() {
    WSACleanup();
}

void OnlineClock::sincronizar() {
    std::cout << "[NTP DEBUG] Iniciando tentativa de sincronizacao..." << std::endl;
    
    try {
        UniqueSocket sPtr = SocketFactory::criarUdpSocket();
        if (!sPtr) {
            std::cerr << "[NTP ERROR] Falha ao criar o socket UDP." << std::endl;
            return;
        }

        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        std::cout << "[NTP DEBUG] Resolvendo host: pool.ntp.org..." << std::endl;
        if (getaddrinfo("pool.ntp.org", "123", &hints, &res) != 0) {
            std::cerr << "[NTP ERROR] Falha ao resolver DNS (sem internet?)." << std::endl;
            return;
        }

        // Converte o endereço IP resolvido para string para logar
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ipStr, INET_ADDRSTRLEN);
        std::cout << "[NTP DEBUG] Servidor encontrado: " << ipStr << " na porta 123." << std::endl;

        unsigned char packet[48] = { 0x1B, 0 };
        if (sendto(*sPtr, (char*)packet, sizeof(packet), 0, res->ai_addr, (int)res->ai_addrlen) < 0) {
            std::cerr << "[NTP ERROR] Erro ao enviar pacote UDP." << std::endl;
            freeaddrinfo(res);
            return;
        }
        std::cout << "[NTP DEBUG] Pacote de requisicao enviado. Aguardando resposta..." << std::endl;
        freeaddrinfo(res);

        DWORD timeout = 3000;
        setsockopt(*sPtr, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        int bytesReceived = recv(*sPtr, (char*)packet, sizeof(packet), 0);
        if (bytesReceived > 0) {
            std::cout << "[NTP DEBUG] Resposta recebida (" << bytesReceived << " bytes)." << std::endl;

            // Extração do timestamp
            uint32_t secs1900 = ntohl(*reinterpret_cast<uint32_t*>(&packet[40]));
            auto ntp_tp = std::chrono::system_clock::from_time_t(secs1900 - 2208988800U);

            std::lock_guard<std::mutex> lock(m_mutexData);
            m_offset = ntp_tp - std::chrono::system_clock::now();
            m_sincronizado = true;

            // Log de sucesso com o offset calculado
            auto offsetSecs = std::chrono::duration_cast<std::chrono::seconds>(m_offset).count();
            std::cout << "[NTP SUCCESS] Sincronizado. Offset local: " << offsetSecs << " segundos." << std::endl;
        } else {
            std::cerr << "[NTP ERROR] Timeout ou conexao encerrada pelo servidor." << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[NTP EXCEPTION] " << e.what() << std::endl;
        m_sincronizado = false;
    } catch (...) {
        std::cerr << "[NTP EXCEPTION] Erro desconhecido durante a sincronizacao." << std::endl;
        m_sincronizado = false;
    }
}
std::string OnlineClock::obterDataHoraFormatada() {
    using namespace std::chrono;
    
    nanoseconds current_offset;
    {
        std::lock_guard<std::mutex> lock(m_mutexData);
        current_offset = m_offset;
    }

    // Aplica o offset online ao relógio do sistema atual
    auto agora_online = system_clock::now() + current_offset;
    auto tp_secs = floor<seconds>(agora_online);
    
    // Converte para fuso horário local (Brasil/etc)
    auto zt = zoned_time{ current_zone(), tp_secs };

    // Lógica do piscar dos dois pontos (:)
    bool mostrarSep = (duration_cast<seconds>(tp_secs.time_since_epoch()).count() % 2 == 0);
    char sep = mostrarSep ? ':' : ' ';

    // Formatação: %d/%m/%Y (Data) e %H:%M:%S (Hora)
    return std::format("{:%d/%m/%Y} | {:%H}{}{:%M}{}{:%S}", zt, zt, sep, zt, sep, zt);
}