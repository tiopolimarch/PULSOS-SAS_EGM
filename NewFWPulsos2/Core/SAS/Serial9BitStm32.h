/*
 * Serial9BitStm32.h
 *
 *  Serial9Bit para STM32: UART de 9 bits sin paridad con DMA (RX circular
 *  half-word, TX por DMA o por interrupcion).  Es el unico archivo, junto con
 *  Serial9BitStm32.cpp y SasEgmStm32.cpp, que conoce el HAL.
 *
 *  SE USA ASI (lo hace SasEgm::init(&huart2); aqui solo para referencia)
 *
 *      Serial9BitStm32 serial;
 *      serial.init(&huart2);                    // guarda el UART y arma la recepcion
 *
 *      uint16_t buf[256];
 *      if (serial.available9bit()) {
 *          n = serial.readBuffer9bit((uint8_t*)buf, sizeof(buf));   // palabras leidas
 *          serial.write9bit(buf, n);                                // el 9o bit viaja
 *          serial.flush9bit();
 *      }
 *
 *  Y en los callbacks del HAL: manejarRxEvent(), manejarTxCompleto() y
 *  manejarError().
 *
 *  COMO RECIBE
 *      El DMA llena un buffer circular de palabras de 16 bits (el noveno bit
 *      viaja en el bit 8).  Cada vez que el HAL avisa -IDLE o buffer lleno-
 *      la interrupcion solo ANOTA cuantas palabras llegaron y cuando.  El lazo
 *      principal las vacia de una vez con readBuffer9bit().  Una sola escritora (la
 *      interrupcion) y una sola lectora (el lazo): no hacen falta secciones
 *      criticas.
 *
 *  CONFIGURACION QUE EXIGE  (SasEgm::init(huart) la revisa antes de arrancar)
 *      19200 baudios, 9 bits, sin paridad, 1 stop.
 *      DMA de RX: circular, half-word.  DMA de TX: opcional, half-word.
 */

#ifndef SRC_SERIAL9BITSTM32_H_
#define SRC_SERIAL9BITSTM32_H_

// main.h trae el HAL de la familia que sea (F1, F4...).
#include "main.h"
#include "Serial9Bit.h"

class Serial9BitStm32 : public Serial9Bit
{
public:
    // Guarda el UART y arma la recepcion.  El UART ya lo inicializo CubeMX.
    void init(UART_HandleTypeDef* huart);

    // --- Serial9Bit ---------------------------------------------------------
    size_t write9bit(const uint16_t* palabras, size_t largo) override;
    size_t available9bit() override;
    size_t readBuffer9bit(uint8_t* destino, size_t tamano, uint32_t* marcas = nullptr) override;
    void   flush9bit() override;

    // --- Interrupciones: llamar desde los callbacks del HAL -----------------
    void manejarRxEvent(uint16_t posicionDma);   // HAL_UARTEx_RxEventCallback
    void manejarTxCompleto();               // HAL_UART_TxCpltCallback
    void manejarError();                    // HAL_UART_ErrorCallback

    // --- Diagnostico: solo lectura ------------------------------------------
    uint32_t errores()       const { return statErrores_; }        // errores de linea
    uint32_t desbordes()     const { return statDesbordes_; }      // anillo RX rebasado
    uint32_t txRechazados()  const { return statTxRechazados_; }   // respuestas no enviadas

private:
    // 256 half-words = 512 bytes de buffer DMA = 133 ms de aire a 19200 baudios.
    static constexpr uint16_t RX_PALABRAS     = 256;
    static constexpr uint16_t TX_MAX          = 256;
    static constexpr uint32_t FLUSH_MAX_MS    = 100;     // tope de espera de flush9bit()
    static constexpr uint16_t MASCARA_9_BITS  = 0x01FF;

    void   armarRx();
    void   anotarPalabras(uint16_t palabrasNuevas, uint32_t marca);
    void   rescatarPendientes();
    bool   puedeTransmitir();
    size_t iniciarTx(uint16_t palabras);

    UART_HandleTypeDef* huart_ = nullptr;

    // Recepcion.  dmaRx_ es a la vez el buffer del DMA y el anillo de lectura.
    // La palabra numero k (contando desde que se armo) vive en dmaRx_[k % 256]
    // y su hora de llegada en marcaRx_[k % 256].
    uint16_t          dmaRx_[RX_PALABRAS]   = {};
    uint32_t          marcaRx_[RX_PALABRAS] = {};
    uint16_t          dmaTail_              = 0;      // hasta donde ya se anoto (interrupcion)
    volatile uint32_t escritas_             = 0;      // palabras anotadas (interrupcion)
    volatile uint32_t leidas_               = 0;      // palabras leidas (lazo principal)
    volatile bool     rearmarRx_            = false;
    bool              rescatado_            = false;  // ya se anotaron las palabras previas al error

    // Transmision.
    uint16_t          txBuf_[TX_MAX]        = {};
    volatile bool     txBusy_               = false;

    // Diagnostico: privado, se mira en el depurador.
    volatile uint32_t statTxRechazados_     = 0;
    volatile uint32_t statErrores_          = 0;
    uint32_t          statDesbordes_        = 0;
};

#endif /* SRC_SERIAL9BITSTM32_H_ */
