/*
 * SasEgmStm32.cpp
 *
 *  La parte de SasEgm que conoce el HAL de STM32: el arranque validando la
 *  configuracion de CubeMX, el reloj y los tres callbacks.  Es el UNICO archivo,
 *  junto con Serial9BitStm32, que se cambia al llevar el SAS a otro micro.  En
 *  PC no se compila.
 */

#include "main.h"
#include "SasEgm.h"
#include "Serial9BitStm32.h"

// Este archivo solo tiene sentido con el HAL de ST.  Si el proyecto no define
// USE_HAL_DRIVER, SasEgm.h no declara estos metodos y el error seria confuso.
#ifndef SAS_CON_HAL_ST
#error "SasEgmStm32.cpp necesita USE_HAL_DRIVER (y no SAS_SIN_HAL_ST)."
#endif

// Un solo bus SAS -> una sola serial.  Dos usos de "static", los dos con
// motivo:
//   - la funcion es static (visible solo en este archivo): nadie mas debe
//     crear ni tocar la serial de STM32, la maneja SasEgm.
//   - la variable es estatica DENTRO de la funcion, por el mismo motivo que en
//     SasEgm::getInstance(): que se construya al pedirla y no dependa del
//     orden de construccion de las globales entre archivos.
static Serial9BitStm32* serialStm32() {
    static Serial9BitStm32 unica;
    return &unica;
}

// El reloj del SAS en esta plataforma.  No debe retroceder.
uint32_t SasEgm::ahoraMs() const {
    return HAL_GetTick();
}

// ---------------------------------------------------------------------------
//  INICIALIZACION
//
//  Antes de armar el DMA se revisa la configuracion del UART.  Cada uno de
//  estos errores hace que el SAS "no funcione" sin ningun otro sintoma, asi
//  que se detectan aqui y se devuelven con nombre.  Si init() no devuelve
//  OK, el SAS no arranca y servicio() no hace nada.
// ---------------------------------------------------------------------------
SasInitResultado SasEgm::init(UART_HandleTypeDef* huartSas) {
    iniciado_ = false;

    if (huartSas == nullptr)                              return SasInitResultado::SIN_UART;

    // El host habla a 19200.  CubeMX / main.cpp deben dejarlo asi: la serial ya
    // no reconfigura la velocidad.
    if (huartSas->Init.BaudRate != BAUDIOS_SAS)           return SasInitResultado::NO_ES_19200;

    // El noveno bit es el de wakeup: con 8 bits no se sabe donde empieza
    // una trama.
    if (huartSas->Init.WordLength != UART_WORDLENGTH_9B)  return SasInitResultado::NO_ES_9_BITS;
    if (huartSas->Init.Parity     != UART_PARITY_NONE)    return SasInitResultado::CON_PARIDAD;

    // Sin DMA de RX (o sin el __HAL_LINKDMA en el MspInit) el HAL truena.
    if (huartSas->hdmarx == nullptr)                      return SasInitResultado::SIN_DMA_RX;

    // Normal en vez de circular: el DMA se para al llenarse y el enlace muere
    // en silencio a los pocos segundos.
    if (huartSas->hdmarx->Init.Mode != DMA_CIRCULAR)      return SasInitResultado::DMA_RX_NO_CIRCULAR;

    // Byte en vez de halfword: el noveno bit se pierde.
    if (huartSas->hdmarx->Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD)
                                                          return SasInitResultado::DMA_RX_NO_HALFWORD;

    huart_ = huartSas;
    serialStm32()->init(huartSas);

    return init(serialStm32());
}

// ---------------------------------------------------------------------------
//  INTERRUPCIONES
//
//  Se les pasa el huart tal cual llega al callback del HAL.  Si no es el
//  UART del SAS no hacen nada, asi quien integra no tiene que escribir el if.
// ---------------------------------------------------------------------------
void SasEgm::onTxComplete(UART_HandleTypeDef* huart) {
    if (huart_ != nullptr && huart == huart_) serialStm32()->manejarTxCompleto();
}

void SasEgm::onRxEvent(UART_HandleTypeDef* huart, uint16_t size) {
    if (huart_ != nullptr && huart == huart_) serialStm32()->manejarRxEvent(size);
}

// Sin esto, un solo error de linea -ruido, overrun- detiene el DMA de RX y
// el enlace muere para siempre.  Solo se marca; el re-armado lo hace la serial
// en la siguiente vuelta del lazo.
void SasEgm::onError(UART_HandleTypeDef* huart) {
    if (huart_ != nullptr && huart == huart_) serialStm32()->manejarError();
}
