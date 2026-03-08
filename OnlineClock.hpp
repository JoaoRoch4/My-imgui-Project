#pragma once

#include "pch.hpp"

struct SocketDeleter {
    // O operador recebe o ponteiro para o SOCKET que o unique_ptr gerencia
    void operator()(SOCKET* s) const {
        if (s) {
            if (*s != INVALID_SOCKET) {
                closesocket(*s);
            }
            delete s; // Libera a memória do heap onde o SOCKET estava guardado
        }
    }
};
using UniqueSocket = std::unique_ptr<SOCKET, SocketDeleter>;

class SocketFactory {
public:
    static UniqueSocket criarUdpSocket() {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        
        if (s == INVALID_SOCKET) {
            return nullptr; 
        }

        // Alocamos o SOCKET no heap para que o unique_ptr possa gerenciá-lo
        return UniqueSocket(new SOCKET(s));
    }
};
class OnlineClock {
public:
    OnlineClock();
    ~OnlineClock();

    // Sincroniza com o servidor NTP
    void sincronizar();
    
    // Retorna a string formatada: "DD/MM/YYYY | HH:MM:SS"
    std::string obterDataHoraFormatada();
    
    bool estaPronto() const;
        std::atomic<bool> m_sincronizado{ false };

private:
  

    std::chrono::nanoseconds m_offset{ 0 };
    mutable std::mutex m_mutexData;
};
   

