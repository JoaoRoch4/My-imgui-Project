#pragma once
#include "pch.hpp"



/**
 * @file GifDecoder.hpp
 * @brief Decoder GIF89a puro C++ com LZW paralelo e streaming por frame.
 *
 * ============================================================
 *  PIPELINE (3 passes, parcialmente pipelined)
 * ============================================================
 *
 *  PASS 1 — Parse (1 thread, sequencial)
 *    ifstream → lê todos os blocos em ordem e recolhe ParsedFrame[].
 *    No final chama on_header(width, height, total_frames, frame_bytes).
 *    Rápido — apenas I/O + metadata.
 *
 *  PASS 2 + PASS 3 — Pipelined (sliding window)
 *    Lança os primeiros hw_threads LZW decodes em paralelo.
 *    Itera em ordem 0..N:
 *      future[i].get()  → aguarda LZW do frame i
 *      lança future[i + hw_threads]  → mantém hw_threads tasks em voo
 *      composite(i)     → aplica disposal + pixels ao canvas
 *      on_frame(i, canvas_span, delay_ms)  → callback imediato
 *
 *    O chamador recebe o frame 0 mal termine o seu LZW + composite,
 *    enquanto os frames seguintes continuam a decodificar em paralelo.
 *
 * ============================================================
 *  GANHO PARA O UTILIZADOR
 * ============================================================
 *
 *  ANTES (batch):
 *    [Spinner ............ todos os frames prontos] → frame 0 visível
 *
 *  AGORA (streaming):
 *    [Spinner ...] → frame 0 visível
 *                   [frames 1..N carregam em background]
 *
 *  O tempo até o GIF aparecer cai de T_total para T_frame_0.
 */

/**
 * @brief Callback chamado uma vez após o Parse (Pass 1), antes de qualquer frame.
 *
 * Permite ao chamador pré-alocar buffers com as dimensões e contagem exactas.
 *
 * @param width        Largura do canvas lógico.
 * @param height       Altura do canvas lógico.
 * @param total_frames Número total de frames no GIF.
 * @param frame_bytes  Bytes por frame = width × height × 4 (RGBA).
 */
using GifHeaderCallback = std::function<
    void(int width, int height, int total_frames, std::size_t frame_bytes)>;

/**
 * @brief Callback chamado uma vez por frame, em ordem (0, 1, 2, ...).
 *
 * Chamado assim que o frame está composto no canvas — sem esperar pelos frames
 * seguintes. O span é válido apenas durante a chamada (canvas interno).
 *
 * @param frame_idx  Índice do frame (0-based).
 * @param rgba       Vista do canvas RGBA: width × height × 4 bytes.
 * @param delay_ms   Delay em ms para este frame.
 */
using GifFrameCallback = std::function<
    void(int frame_idx, std::span<const uint8_t> rgba, int delay_ms)>;

/// Decoder GIF89a com LZW paralelo e streaming por frame — sem dependências externas.
class GifDecoder
{
public:
    /**
     * @brief Decodifica um ficheiro GIF e entrega os frames via callbacks.
     *
     * Bloqueia até todos os frames estarem decodificados (corre numa background
     * thread — não bloqueia o render loop).
     *
     * Ordem de chamadas garantida:
     *   on_header → on_frame(0) → on_frame(1) → ... → on_frame(N-1) → retorna
     *
     * Thread-safe: pode ser chamado de qualquer thread.
     * Sem estado estático — cada chamada é completamente independente.
     *
     * @param path       Caminho para o ficheiro .gif.
     * @param on_header  Chamado após Parse com metadata do GIF.
     * @param on_frame   Chamado por frame assim que composto.
     * @return           true se pelo menos 1 frame foi entregue, false em erro.
     */
    [[nodiscard]] static bool Decode(
        const std::filesystem::path& path,
        GifHeaderCallback            on_header,
        GifFrameCallback             on_frame);
};