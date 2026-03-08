/**
 * @file GifDecoder.cpp
 * @brief Decoder GIF89a com LZW paralelo por frame.
 *
 * ============================================================
 *  PORQUÊ 3 PASSES EM VEZ DE 1
 * ============================================================
 *
 *  GIF é um formato de stream sequencial:
 *    - Não tens índice de frames — tens de ler bloco a bloco
 *    - Cada frame pode ter LCT diferente (lida antes do LZW)
 *    - O compositing depende do canvas do frame anterior
 *      → não pode ser paralelizado
 *
 *  Mas o LZW de cada frame é completamente autónomo:
 *    - lzw_data[0] não depende de lzw_data[1]
 *    - Cada frame tem os seus próprios lzw_data + color table
 *    → pode ser decomprimido em paralelo
 *
 *  PASS 1 — Parse (sequencial, I/O-bound):
 *    Lê o ficheiro e recolhe ParsedFrame[] com lzw_data copiados.
 *    Rápido — apenas I/O + metadata + memcpy dos sub-blocos.
 *
 *  PASS 2 — LZW Decode (paralelo, CPU-bound):
 *    std::jthread por grupo de frames (thread pool fixo).
 *    Cada task: LzwDecode(lzw_data[i]) → indices[i]
 *    Futures coleccionadas → todas esperadas antes do Pass 3.
 *    Escala com o número de núcleos disponíveis.
 *
 *  PASS 3 — Compositing (sequencial, memory-bound):
 *    Para cada frame i (em ordem):
 *      1. Aplica disposal do frame i-1
 *      2. Composita pixels de indices[i] sobre o canvas
 *      3. Copia canvas → raw_pixels[i]
 *    Não pode ser paralelizado — cada frame depende do anterior.
 *    Mas é rápido: apenas memcpy + loops de pixel simples.
 *
 * ============================================================
 *  ESTRUTURAS INTERNAS
 * ============================================================
 *
 *  ColorTable  — 256 entradas RGB (3 bytes cada)
 *  GCE         — Graphic Control Extension (delay, disposal, transparent)
 *  ParsedFrame — tudo o que é necessário para LZW decode + compositing:
 *                  lzw_data, min_code_size, gce, posição, interlace,
 *                  cópia da color table activa
 *
 *  A color table é copiada para cada ParsedFrame para que os threads
 *  do Pass 2 não precisem de sincronização (read-only por construção,
 *  mas a cópia elimina qualquer questão de lifetime da global CT).
 */

#include "pch.hpp"
#include "GifDecoder.hpp"

// ============================================================================
// Implementação interna — anonymous namespace
// ============================================================================

namespace {

// ============================================================================
// Helpers de leitura binária
// ============================================================================

/**
 * @brief Lê 1 byte sem sinal do stream.
 * @return Byte lido, ou 0 em falha de I/O.
 */
[[nodiscard]] uint8_t ReadU8(std::istream& s)
{
    uint8_t v = 0;
    s.read(reinterpret_cast<char*>(&v), 1);
    return v;
}

/**
 * @brief Lê uint16_t little-endian do stream (2 bytes, LSB primeiro).
 * @return Valor de 16 bits, ou 0 em falha de I/O.
 */
[[nodiscard]] uint16_t ReadU16LE(std::istream& s)
{
    uint8_t lo = 0, hi = 0;
    s.read(reinterpret_cast<char*>(&lo), 1);
    s.read(reinterpret_cast<char*>(&hi), 1);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

/**
 * @brief Lê todos os sub-blocos GIF para um vector plano.
 *
 * Sub-blocos GIF: [length_byte][data × length] repetido até length == 0.
 * Concatena os dados de todos os sub-blocos numa sequência contígua.
 * Usado para ler lzw_data completo de um Image Descriptor.
 *
 * @return Vector contíguo com todos os bytes de dados (sem length bytes).
 */
[[nodiscard]] std::vector<uint8_t> ReadSubBlocks(std::istream& s)
{
    std::vector<uint8_t> data;
    data.reserve(256); // capacidade inicial para evitar reallocs em GIFs pequenos

    for(;;)
    {
        const uint8_t len = ReadU8(s);
        if(len == 0) break; // bloco terminador — fim dos sub-blocos

        const std::size_t off = data.size();
        data.resize(off + len);
        s.read(reinterpret_cast<char*>(data.data() + off), len);

        if(!s) break; // falha de leitura — termina graciosamente
    }
    return data;
}

/**
 * @brief Descarta sub-blocos sem ler para memória.
 * Usado para extensões que não nos interessam (comment, application, etc.).
 */
void SkipSubBlocks(std::istream& s)
{
    for(;;)
    {
        const uint8_t len = ReadU8(s);
        if(len == 0) break;
        s.seekg(len, std::ios::cur); // avança sem copiar dados
    }
}

// ============================================================================
// Color Table
// ============================================================================

/**
 * @brief Tabela de cores GIF — até 256 entradas RGB.
 *
 * Armazena as cores como RGB compactado (3 bytes por entrada).
 * Convertemos para RGBA (alpha = 255) durante o compositing no Pass 3.
 * Copiada para cada ParsedFrame para evitar dependências de lifetime.
 */
struct ColorTable
{
    std::array<uint8_t, 256 * 3> rgb{}; ///< RGB compactado — entrada i em rgb[i*3..i*3+2]
    int count = 0;                        ///< Número de entradas válidas (potência de 2)
};

/**
 * @brief Lê uma color table do stream.
 * @param count  Número de entradas RGB a ler.
 */
[[nodiscard]] ColorTable ReadColorTable(std::istream& s, int count)
{
    ColorTable ct;
    ct.count = count;
    // Cada entrada: R, G, B — 3 bytes
    s.read(reinterpret_cast<char*>(ct.rgb.data()),
           static_cast<std::streamsize>(count * 3));
    return ct;
}

// ============================================================================
// Disposal Method
// ============================================================================

/**
 * @brief Método de disposal do frame após ser exibido.
 *
 * Controlado pelo Graphic Control Extension.
 * Determina o que acontece ao canvas antes de renderizar o próximo frame.
 */
enum class Disposal : uint8_t
{
    Unspecified       = 0, ///< Não especificado — comportamento de DoNotDispose
    DoNotDispose      = 1, ///< Deixa o canvas intacto
    RestoreBackground = 2, ///< Preenche a área do frame com transparente
    RestorePrevious   = 3  ///< Restaura o canvas ao estado antes deste frame
};

// ============================================================================
// Graphic Control Extension
// ============================================================================

/**
 * @brief Metadados do Graphic Control Extension (0x21 0xF9).
 *
 * Precede cada Image Descriptor e define:
 *   - delay:       tempo de exibição em ms
 *   - disposal:    o que fazer ao canvas após este frame
 *   - transparent: índice de cor a tratar como totalmente transparente
 */
struct GCE
{
    Disposal disposal        = Disposal::DoNotDispose; ///< Método de disposal
    bool     has_transparent = false;                  ///< Tem cor transparente
    uint8_t  transparent_idx = 0;                      ///< Índice da cor transparente
    int      delay_ms        = 100;                    ///< Delay em ms (default 100ms)
};

// ============================================================================
// ParsedFrame — resultado do Pass 1, input para Pass 2 e Pass 3
// ============================================================================

/**
 * @brief Frame parseado — tudo o que é necessário para LZW decode e compositing.
 *
 * Produzido em Pass 1 (sequencial, parse do stream).
 * Consumido em Pass 2 (paralelo, LZW decode) e Pass 3 (sequencial, compositing).
 *
 * A color_table é uma CÓPIA da tabela activa (local ou global).
 * Isto garante que os threads do Pass 2 podem aceder sem sincronização —
 * cada ParsedFrame é completamente autónomo.
 */
struct ParsedFrame
{
    std::vector<uint8_t> lzw_data;     ///< Sub-blocos LZW concatenados — input do LzwDecode
    int      min_code_size = 2;        ///< LZW minimum code size (2-8)
    GCE      gce;                      ///< Graphic Control Extension deste frame
    int      left      = 0;            ///< Posição X no canvas lógico
    int      top       = 0;            ///< Posição Y no canvas lógico
    int      frame_w   = 0;            ///< Largura do frame (pode ser < canvas)
    int      frame_h   = 0;            ///< Altura do frame (pode ser < canvas)
    bool     interlaced = false;       ///< Frame usa interlacing GIF (4 passes)
    ColorTable color_table;            ///< Cópia da color table activa (local ou global)
};

// ============================================================================
// LZW Bit Reader
// ============================================================================

/**
 * @brief Leitor de bits little-endian para LZW GIF.
 *
 * GIF LZW é packed little-endian: o primeiro código começa no bit 0
 * do primeiro byte. O buffer de 32 bits acumula bytes do span e
 * permite extrair n bits de uma vez.
 *
 * Não aloca memória — opera directamente sobre um std::span.
 * Pode ser copiado sem custo (span + 2 inteiros).
 */
class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> data) noexcept
        : m_data(data) {}

    /**
     * @brief Lê n bits do stream (1-16), little-endian.
     * @return Valor extraído, ou 0 se não há dados suficientes.
     */
    [[nodiscard]] uint32_t Read(int n) noexcept
    {
        // Carrega bytes no buffer até ter n bits disponíveis
        while(m_bits_avail < n)
        {
            if(m_pos >= m_data.size()) return 0; // fim do span
            // Empacota byte — LSB primeiro (little-endian GIF)
            m_buf |= static_cast<uint32_t>(m_data[m_pos++]) << m_bits_avail;
            m_bits_avail += 8;
        }

        // Extrai n bits com máscara
        const uint32_t mask = (1u << n) - 1u;
        const uint32_t val  = m_buf & mask;

        // Remove os n bits lidos do buffer
        m_buf >>= n;
        m_bits_avail -= n;

        return val;
    }

private:
    std::span<const uint8_t> m_data;          ///< Dados de entrada (não possuídos)
    std::size_t              m_pos       = 0;  ///< Posição actual no span
    uint32_t                 m_buf       = 0;  ///< Buffer de 32 bits
    int                      m_bits_avail = 0; ///< Bits válidos no buffer
};

// ============================================================================
// LZW Decode
// ============================================================================

/**
 * @brief Descomprime dados LZW GIF para índices de pixel.
 *
 * Esta função é chamada em threads paralelas no Pass 2.
 * É PURA — sem estado global, sem escrita partilhada.
 * Cada thread opera sobre os seus próprios lzw_data e produz
 * o seu próprio vector de indices — sem mutex necessário.
 *
 * ALGORITMO
 * ----------
 *  A tabela LZW é representada por arrays paralelos prefix/suffix de 4096 entradas:
 *    prefix[i] = índice da entrada anterior na cadeia (-1 = raiz)
 *    suffix[i] = byte de pixel deste nó
 *
 *  Para decodificar o código i:
 *    percorre: i → prefix[i] → prefix[prefix[i]] → ... → nó com prefix==-1
 *    recolhe suffix[] em cada passo → inverte → sequência de pixels
 *
 *  Caso especial (code == next_code):
 *    A entrada ainda não foi adicionada (encoder e decoder em sync).
 *    Valor = sequência_anterior + primeiro_byte_da_sequência_anterior.
 *
 * @param data           Sub-blocos LZW concatenados.
 * @param min_code_size  Minimum code size lido do Image Descriptor.
 * @param pixel_count    Total de pixels esperados (frame_w × frame_h).
 * @return               Vector de índices de pixel (não RGBA — apenas índices CT).
 */
[[nodiscard]] std::vector<uint8_t> LzwDecode(
    std::span<const uint8_t> data,
    int min_code_size,
    int pixel_count)
{
    constexpr int MAX_CODES = 4096; // tamanho máximo da tabela LZW GIF

    const int clear_code = 1 << min_code_size; // código de reset da tabela
    const int eoi_code   = clear_code + 1;     // código de fim de stream

    // Tabela LZW — arrays estáticos de 4096 entradas (no stack — sem heap)
    // Arrays separados (prefix, suffix) são mais cache-friendly que array de structs
    // porque o walk da cadeia acede prefix[] sequencialmente antes de suffix[]
    std::array<int16_t, MAX_CODES> prefix{};
    std::array<uint8_t, MAX_CODES> suffix{};

    // Inicializa as entradas raiz (cores simples 0..clear_code-1)
    for(int i = 0; i < clear_code; ++i)
    {
        prefix[static_cast<std::size_t>(i)] = -1;             // sem predecessora
        suffix[static_cast<std::size_t>(i)] = static_cast<uint8_t>(i); // próprio valor
    }

    BitReader bits(data);

    std::vector<uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(pixel_count)); // alloc exacta → sem realloc

    // Buffer reutilizável para decodificar uma sequência de pixel
    // Reutilizado entre codes para evitar allocs dentro do loop principal
    std::vector<uint8_t> seq;
    seq.reserve(64); // sequência típica — raramente excede 64

    int code_size = min_code_size + 1; // tamanho actual do código em bits
    int next_code = eoi_code + 1;      // próximo slot livre na tabela
    int prev_code = -1;                // código anterior (para construir nova entrada)

    // Lambda: percorre a cadeia prefix/suffix e preenche seq com os pixels (invertido)
    // Retorna referência a seq (reutilizado — não aloca por chamada)
    auto decode_seq = [&](int code) -> const std::vector<uint8_t>&
    {
        seq.clear();
        // Walk: começa no nó 'code', sobe pela cadeia de prefix até -1 (raiz)
        int c = code;
        while(c >= 0 && c < MAX_CODES)
        {
            seq.push_back(suffix[static_cast<std::size_t>(c)]);
            c = prefix[static_cast<std::size_t>(c)];
        }
        std::reverse(seq.begin(), seq.end()); // raiz → folha = ordem correcta
        return seq;
    };

    for(;;)
    {
        const int code = static_cast<int>(bits.Read(code_size));

        // Clear code: reset da tabela e do code_size
        if(code == clear_code)
        {
            code_size = min_code_size + 1;
            next_code = eoi_code + 1;
            prev_code = -1;
            continue;
        }

        // EOI: fim do stream LZW
        if(code == eoi_code)
            break;

        // Primeiro código após um clear — emite directamente, sem nova entrada na tabela
        if(prev_code < 0)
        {
            pixels.push_back(suffix[static_cast<std::size_t>(code)]);
            prev_code = code;
            continue;
        }

        if(code < next_code)
        {
            // Caso normal: código existe na tabela
            const auto& s = decode_seq(code);
            for(uint8_t b : s) pixels.push_back(b);

            // Adiciona nova entrada: cadeia_anterior + primeiro_byte_actual
            if(next_code < MAX_CODES)
            {
                prefix[static_cast<std::size_t>(next_code)] =
                    static_cast<int16_t>(prev_code);
                suffix[static_cast<std::size_t>(next_code)] = s.front();
                ++next_code;
            }
        }
        else
        {
            // Caso especial: code == next_code
            // A entrada ainda não existe — valor = decode(prev) + primeiro_byte(decode(prev))
            const auto& prev_seq = decode_seq(prev_code);
            const uint8_t first  = prev_seq.front();

            for(uint8_t b : prev_seq) pixels.push_back(b);
            pixels.push_back(first); // byte extra que completa a nova entrada

            if(next_code < MAX_CODES)
            {
                prefix[static_cast<std::size_t>(next_code)] =
                    static_cast<int16_t>(prev_code);
                suffix[static_cast<std::size_t>(next_code)] = first;
                ++next_code;
            }
        }

        // Aumenta code_size quando a tabela cresce para 2^code_size (máx 12)
        if(next_code == (1 << code_size) && code_size < 12)
            ++code_size;

        prev_code = code;

        // Para quando temos pixels suficientes — evita over-read de dados corrompidos
        if(static_cast<int>(pixels.size()) >= pixel_count)
            break;
    }

    // Garante que o vector tem exactamente pixel_count entradas
    // (alguns encoders GIF sub-codificam o último frame)
    pixels.resize(static_cast<std::size_t>(pixel_count), 0);

    return pixels;
}

// ============================================================================
// Deinterlace map
// ============================================================================

/**
 * @brief Constrói o mapa scan-line → linha real para frames interlaced.
 *
 * GIF interlacing entrega os pixels em 4 passes fora de ordem:
 *   Pass 1: linhas  0,  8, 16, 24, ...  (offset=0, step=8)
 *   Pass 2: linhas  4, 12, 20, 28, ...  (offset=4, step=8)
 *   Pass 3: linhas  2,  6, 10, 14, ...  (offset=2, step=4)
 *   Pass 4: linhas  1,  3,  5,  7, ...  (offset=1, step=2)
 *
 * LzwDecode produz pixels em ordem de scan-line (0, 1, 2, ...).
 * map[scan_y] = real_y converte para a posição correcta no canvas.
 *
 * @param height  Altura do frame.
 * @return        map[scan_y] = real_y, tamanho == height.
 */
[[nodiscard]] std::vector<int> BuildDeinterlaceMap(int height)
{
    std::vector<int> map;
    map.reserve(static_cast<std::size_t>(height));

    // Constantes dos 4 passes GIF89a
    constexpr std::array<int, 4> offsets{ 0, 4, 2, 1 };
    constexpr std::array<int, 4> steps  { 8, 8, 4, 2 };

    for(int pass = 0; pass < 4; ++pass)
        for(int y = offsets[pass]; y < height; y += steps[pass])
            map.push_back(y); // a scan-line map.size()-1 corresponde à linha real y

    return map;
}

// ============================================================================
// Canvas helper
// ============================================================================

/**
 * @brief Preenche um rectângulo do canvas RGBA com transparente (0,0,0,0).
 *
 * Usado em Pass 3 para Disposal::RestoreBackground:
 * a área do frame anterior é restaurada para transparente antes de
 * renderizar o próximo frame.
 *
 * O rectângulo é clampado aos limites do canvas — não há out-of-bounds.
 */
void ClearRect(std::vector<uint8_t>& canvas,
               int canvas_w, int canvas_h,
               int x, int y, int w, int h)
{
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(canvas_w, x + w);
    const int y1 = std::min(canvas_h, y + h);

    for(int cy = y0; cy < y1; ++cy)
    {
        for(int cx = x0; cx < x1; ++cx)
        {
            const std::size_t ci =
                (static_cast<std::size_t>(cy) * canvas_w + cx) * 4;
            canvas[ci + 0] = 0;
            canvas[ci + 1] = 0;
            canvas[ci + 2] = 0;
            canvas[ci + 3] = 0; // transparente
        }
    }
}

} // namespace (anonymous)


// ============================================================================
// GifDecoder::Decode — streaming com sliding window
// ============================================================================

/**
 * @brief Decodifica um ficheiro GIF e entrega os frames via callbacks.
 *
 * PASS 1 — Parse sequencial (obrigatório — GIF é um stream)
 * ----------------------------------------------------------
 *  Lê todos os blocos em ordem e recolhe ParsedFrame[] com lzw_data.
 *  Chama on_header() com os metadados do GIF para que o chamador
 *  possa pré-alocar buffers antes de qualquer frame chegar.
 *
 * PASS 2 + PASS 3 — Sliding window (pipelined)
 * ---------------------------------------------
 *  Em vez de esperar que TODOS os frames estejam decodificados antes de
 *  compositar, usamos uma janela deslizante de hw_threads tasks:
 *
 *  1. Lança as primeiras hw_threads futures de LZW decode em paralelo.
 *  2. Itera em ordem i = 0..N-1:
 *       future[i].get()            → aguarda LZW do frame i (já pode estar pronto)
 *       lança future[i+hw_threads] → mantém sempre hw_threads tasks em voo
 *       composite(i, indices)      → aplica disposal + pixels no canvas
 *       on_frame(i, canvas, delay) → callback imediato para o chamador
 *
 *  RESULTADO: o chamador recebe o frame 0 mal o seu LZW + composite termine,
 *  sem esperar pelos frames 1..N. Estes continuam a decodificar em paralelo.
 *
 *  SLIDING WINDOW vs SLICE-BASED vs ASYNC-POR-FRAME
 *  --------------------------------------------------
 *  Async-por-frame: cria N threads simultâneas → saturação do scheduler
 *  Slice-based:     divide N frames em hw grupos → frame 0 só sai no fim
 *  Sliding window:  mantém hw tasks em voo, entrega frames em ordem → óptimo
 *
 * @param path       Caminho para o ficheiro .gif.
 * @param on_header  Chamado uma vez após Parse com width/height/total/frame_bytes.
 * @param on_frame   Chamado por frame (em ordem) assim que composto.
 * @return           true se pelo menos 1 frame foi entregue.
 */
bool GifDecoder::Decode(
    const std::filesystem::path& path,
    GifHeaderCallback            on_header,
    GifFrameCallback             on_frame)
{
    // ===========================================================================
    // PASS 1 — Parse sequencial do stream GIF
    // ===========================================================================

    std::ifstream file(path, std::ios::binary);
    if(!file.is_open()) return false;

    // ---- Valida header --------------------------------------------------------
    std::array<char, 6> header{};
    file.read(header.data(), 6);
    if(!file) return false;

    const bool is_gif87 = (header == std::array<char,6>{'G','I','F','8','7','a'});
    const bool is_gif89 = (header == std::array<char,6>{'G','I','F','8','9','a'});
    if(!is_gif87 && !is_gif89) return false;

    // ---- Logical Screen Descriptor (7 bytes) ----------------------------------
    const int canvas_w = static_cast<int>(ReadU16LE(file));
    const int canvas_h = static_cast<int>(ReadU16LE(file));
    if(canvas_w <= 0 || canvas_h <= 0) return false;

    const uint8_t packed_lsd = ReadU8(file);
    ReadU8(file); // background color index — tratamos canvas como transparente
    ReadU8(file); // pixel aspect ratio (ignorado)

    const bool global_ct_flag = (packed_lsd & 0x80) != 0;
    const int  gct_count      = 1 << ((packed_lsd & 0x07) + 1);

    ColorTable global_ct;
    if(global_ct_flag)
        global_ct = ReadColorTable(file, gct_count);

    // ---- Loop de blocos até Trailer ------------------------------------------
    std::vector<ParsedFrame> parsed_frames;
    parsed_frames.reserve(64);

    GCE current_gce;

    for(;;)
    {
        if(!file) break;
        const uint8_t block_type = ReadU8(file);

        if(block_type == 0x3B) break; // Trailer

        if(block_type == 0x21)
        {
            const uint8_t label = ReadU8(file);

            if(label == 0xF9) // Graphic Control Extension
            {
                ReadU8(file); // block_size

                const uint8_t packed_gce = ReadU8(file);

                switch((packed_gce >> 3) & 0x07)
                {
                    case 1:  current_gce.disposal = Disposal::DoNotDispose;      break;
                    case 2:  current_gce.disposal = Disposal::RestoreBackground;  break;
                    case 3:  current_gce.disposal = Disposal::RestorePrevious;    break;
                    default: current_gce.disposal = Disposal::Unspecified;        break;
                }

                current_gce.has_transparent = (packed_gce & 0x01) != 0;

                const uint16_t delay_cs = ReadU16LE(file);
                const int      delay_ms = static_cast<int>(delay_cs) * 10;
                current_gce.delay_ms = (delay_ms <= 0) ? 10 : delay_ms;

                current_gce.transparent_idx = ReadU8(file);
                ReadU8(file); // block terminator
            }
            else
            {
                SkipSubBlocks(file);
            }
            continue;
        }

        if(block_type == 0x2C) // Image Descriptor
        {
            ParsedFrame pf;

            pf.left    = static_cast<int>(ReadU16LE(file));
            pf.top     = static_cast<int>(ReadU16LE(file));
            pf.frame_w = static_cast<int>(ReadU16LE(file));
            pf.frame_h = static_cast<int>(ReadU16LE(file));

            if(pf.frame_w <= 0 || pf.frame_h <= 0)
            {
                SkipSubBlocks(file);
                continue;
            }

            const uint8_t packed_id  = ReadU8(file);
            const bool local_ct_flag = (packed_id & 0x80) != 0;
            pf.interlaced            = (packed_id & 0x40) != 0;
            const int lct_count      = 1 << ((packed_id & 0x07) + 1);

            pf.color_table = local_ct_flag
                ? ReadColorTable(file, lct_count)
                : global_ct; // cópia — thread do LZW é autónomo

            pf.min_code_size = static_cast<int>(ReadU8(file));
            if(pf.min_code_size < 2 || pf.min_code_size > 8)
            {
                SkipSubBlocks(file);
                continue;
            }

            pf.lzw_data = ReadSubBlocks(file);
            if(pf.lzw_data.empty()) continue;

            pf.gce = current_gce;

            parsed_frames.push_back(std::move(pf));
            current_gce = GCE{};
            continue;
        }

        SkipSubBlocks(file); // bloco desconhecido
    }

    if(parsed_frames.empty()) return false;

    // ---- Chama on_header com metadados exactos --------------------------------
    // Chamado ANTES de qualquer on_frame — permite pré-alocar buffers
    const std::size_t frame_count  = parsed_frames.size();
    const std::size_t canvas_pixels =
        static_cast<std::size_t>(canvas_w) *
        static_cast<std::size_t>(canvas_h);
    const std::size_t frame_bytes  = canvas_pixels * 4;

    on_header(
        canvas_w,
        canvas_h,
        static_cast<int>(frame_count),
        frame_bytes);

    // ===========================================================================
    // PASS 2 + PASS 3 — Sliding window: LZW paralelo + composite em ordem
    // ===========================================================================
    //
    // JANELA DESLIZANTE
    // ------------------
    // Mantém sempre hw_threads futures em voo.
    // Quando consumimos (get) a future do frame i, lançamos imediatamente
    // a future do frame i+hw_threads — a janela avança 1 slot.
    //
    // Isto garante que:
    //   - Nunca há mais de hw_threads threads simultâneas (sem saturação)
    //   - A GPU começa a receber frame 0 logo que o seu LZW termine
    //   - Os frames seguintes decodificam enquanto o composite + callback corre
    //
    // EXEMPLO (hw=4, frames=10):
    //   Lança: [0,1,2,3]
    //   get(0) → composite(0) → on_frame(0) → lança [4]   → janela: [1,2,3,4]
    //   get(1) → composite(1) → on_frame(1) → lança [5]   → janela: [2,3,4,5]
    //   ...
    //   get(9) → composite(9) → on_frame(9) → nada a lançar → fim

    const std::size_t hw = static_cast<std::size_t>(
        std::thread::hardware_concurrency());

    // Nunca mais tasks em voo do que frames totais nem do que núcleos
    const std::size_t window_size = std::max(std::size_t{1},
                                              std::min(hw, frame_count));

    // Reserva vector de futures — index i = future do LZW do frame i
    // Começamos com futures default-constructed (não válidas)
    std::vector<std::future<std::vector<uint8_t>>> futures(frame_count);

    // Lambda que lança a task LZW para o frame next_launch
    // Captura parsed_frames por referência — vive até ao fim desta função
    std::size_t next_launch = 0;

    auto launch_lzw = [&](std::size_t frame_idx)
    {
        const ParsedFrame& pf = parsed_frames[frame_idx];
        const int pixel_count = pf.frame_w * pf.frame_h;
        const int min_cs      = pf.min_code_size;

        // lzw_data é capturado por referência — ParsedFrame vive até ao fim do Decode
        // LzwDecode só lê pf.lzw_data (const) → sem race com outros frames
        futures[frame_idx] = std::async(
            std::launch::async,
            [&pf_lzw = pf.lzw_data, min_cs, pixel_count]
                -> std::vector<uint8_t>
            {
                // Função pura — sem estado partilhado
                return LzwDecode(
                    std::span<const uint8_t>(pf_lzw),
                    min_cs,
                    pixel_count);
            }
        );

        ++next_launch;
    };

    // ---- Lança a janela inicial (primeiras window_size tasks) -----------------
    for(std::size_t t = 0; t < window_size; ++t)
        launch_lzw(t);

    // ---- Estado do canvas para Pass 3 (composite) ----------------------------
    std::vector<uint8_t> canvas(canvas_pixels * 4, 0); // RGBA transparente
    std::vector<uint8_t> saved_canvas;                  // para RestorePrevious

    // Estado do frame anterior (para disposal)
    struct PrevState {
        int      left     = 0;
        int      top      = 0;
        int      width    = 0;
        int      height   = 0;
        Disposal disposal = Disposal::DoNotDispose;
        bool     valid    = false;
    } prev;

    int frames_delivered = 0; // quantos on_frame foram chamados com sucesso

    // ---- Loop principal: composite em ordem, lança próxima task ao consumir ---
    for(std::size_t i = 0; i < frame_count; ++i)
    {
        // Aguarda o LZW deste frame específico
        // Se a task ainda está a correr, bloqueia apenas o necessário.
        // Se já terminou (frames seguintes mais lentos), retorna imediatamente.
        std::vector<uint8_t> indices = futures[i].get();

        // Lança a próxima task — mantém a janela cheia
        if(next_launch < frame_count)
            launch_lzw(next_launch);

        // ------- Composite do frame i no canvas (Pass 3, sequencial) ----------

        const ParsedFrame& pf = parsed_frames[i];

        // ---- 1. Aplica disposal do frame anterior ----------------------------
        if(prev.valid)
        {
            switch(prev.disposal)
            {
            case Disposal::RestoreBackground:
                // Preenche área do frame anterior com transparente
                ClearRect(canvas, canvas_w, canvas_h,
                          prev.left, prev.top, prev.width, prev.height);
                break;

            case Disposal::RestorePrevious:
                // Restaura o canvas ao estado antes do frame anterior
                if(!saved_canvas.empty())
                    canvas = saved_canvas;
                break;

            case Disposal::DoNotDispose:
            case Disposal::Unspecified:
            default:
                break; // canvas inalterado
            }
        }

        // ---- 2. Guarda canvas se disposal actual é RestorePrevious -----------
        // Guardamos ANTES de renderizar — restaura estado antes deste frame
        if(pf.gce.disposal == Disposal::RestorePrevious)
            saved_canvas = canvas;

        // ---- 3. Mapa de deinterlace (apenas se necessário) ------------------
        std::vector<int> deinterlace_map;
        if(pf.interlaced)
            deinterlace_map = BuildDeinterlaceMap(pf.frame_h);

        // ---- 4. Composita pixels sobre o canvas ------------------------------
        for(int fy = 0; fy < pf.frame_h; ++fy)
        {
            const int actual_fy =
                pf.interlaced
                    ? deinterlace_map[static_cast<std::size_t>(fy)]
                    : fy;

            const int canvas_y = pf.top + actual_fy;
            if(canvas_y < 0 || canvas_y >= canvas_h) continue;

            for(int fx = 0; fx < pf.frame_w; ++fx)
            {
                const int canvas_x = pf.left + fx;
                if(canvas_x < 0 || canvas_x >= canvas_w) continue;

                const uint8_t idx =
                    indices[static_cast<std::size_t>(fy) * pf.frame_w + fx];

                // Pixel transparente → skip (compositing alpha)
                if(pf.gce.has_transparent && idx == pf.gce.transparent_idx)
                    continue;

                // Escreve pixel RGBA no canvas
                const std::size_t ci  =
                    (static_cast<std::size_t>(canvas_y) * canvas_w + canvas_x) * 4;
                const std::size_t rgb =
                    static_cast<std::size_t>(idx) * 3;

                canvas[ci + 0] = pf.color_table.rgb[rgb + 0]; // R
                canvas[ci + 1] = pf.color_table.rgb[rgb + 1]; // G
                canvas[ci + 2] = pf.color_table.rgb[rgb + 2]; // B
                canvas[ci + 3] = 255;                           // A — opaco
            }
        }

        // ---- 5. Entrega frame ao chamador via callback ------------------------
        // canvas é uma std::span válida apenas durante esta chamada.
        // GifAnimation copia os pixels para m_pending.raw_pixels sob mutex.
        on_frame(
            static_cast<int>(i),
            std::span<const uint8_t>(canvas),
            pf.gce.delay_ms);

        ++frames_delivered;

        // Actualiza estado para o disposal do próximo frame
        prev.left     = pf.left;
        prev.top      = pf.top;
        prev.width    = pf.frame_w;
        prev.height   = pf.frame_h;
        prev.disposal = pf.gce.disposal;
        prev.valid    = true;
    }

    return frames_delivered > 0;
}