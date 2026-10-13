/*
 * Serial9BitStm32.cpp
 *
 * Ver Serial9BitStm32.h.
 */

#include "Serial9BitStm32.h"

// ===========================================================================
//  INICIALIZACION
// ===========================================================================
void Serial9BitStm32::init(UART_HandleTypeDef* huart)
{
    huart_ = huart;
    if (huart_ == nullptr) return;

    statTxRechazados_ = 0;
    statErrores_      = 0;
    statDesbordes_    = 0;
    txBusy_           = false;
    rearmarRx_        = false;
    rescatado_        = false;

    armarRx();
}

void Serial9BitStm32::armarRx()
{
    if (huart_ == nullptr) return;

    HAL_UART_AbortReceive(huart_);

    __HAL_UART_CLEAR_OREFLAG(huart_);
    __HAL_UART_CLEAR_NEFLAG(huart_);
    __HAL_UART_CLEAR_FEFLAG(huart_);
    __HAL_UART_CLEAR_PEFLAG(huart_);
    __HAL_UART_CLEAR_IDLEFLAG(huart_);
    huart_->ErrorCode = HAL_UART_ERROR_NONE;

    // Se empieza de cero: lo que hubiera por leer se pierde con el re-armado.
    dmaTail_  = 0;
    escritas_ = 0;
    leidas_   = 0;

    // El buffer es de 16 bits porque el UART esta en 9 bits sin paridad:
    // HAL escribe la palabra completa (9 bits) en cada half-word.
    HAL_UARTEx_ReceiveToIdle_DMA(huart_,
                                 reinterpret_cast<uint8_t*>(dmaRx_),
                                 RX_PALABRAS);

    // Apagamos el Half Transfer: solo agrega callbacks que no usamos.
    if (huart_->hdmarx != nullptr) {
        __HAL_DMA_DISABLE_IT(huart_->hdmarx, DMA_IT_HT);
    }
}

// ===========================================================================
//  RECEPCION
// ===========================================================================

// Contexto de interrupcion: solo anota las palabras nuevas y cuando llegaron.
void Serial9BitStm32::manejarRxEvent(uint16_t posicionDma)
{
    // Normaliza la posicion: RX_PALABRAS equivale a la posicion 0.
    // Sin esto, el while nunca terminaria cuando el DMA da la vuelta.
    const uint16_t posicionEscritura = static_cast<uint16_t>(posicionDma % RX_PALABRAS);
    const uint32_t marca             = HAL_GetTick();

    // Cuantas palabras nuevas hay.  Si posicionEscritura == dmaTail_ pero el HAL
    // aviso con posicionDma == RX_PALABRAS, el DMA dio una vuelta completa
    // (llegaron justo 256): sin esto esas 256 palabras nunca se anotan.
    uint16_t palabrasNuevas =
        static_cast<uint16_t>((posicionEscritura + RX_PALABRAS - dmaTail_) % RX_PALABRAS);
    if (palabrasNuevas == 0 && posicionDma >= RX_PALABRAS) palabrasNuevas = RX_PALABRAS;

    anotarPalabras(palabrasNuevas, marca);
}

// Anota 'palabrasNuevas' palabras que el DMA ya dejo en dmaRx_, empezando en dmaTail_.
void Serial9BitStm32::anotarPalabras(uint16_t palabrasNuevas, uint32_t marca)
{
    for (uint16_t palabraAnotada = 0; palabraAnotada < palabrasNuevas; palabraAnotada++) {
        marcaRx_[dmaTail_] = marca;
        dmaTail_ = static_cast<uint16_t>((dmaTail_ + 1) % RX_PALABRAS);
        escritas_ = escritas_ + 1;
    }
}

// Tras un error el HAL ya paro el DMA, pero las palabras que alcanzo a escribir
// antes siguen en dmaRx_ sin anotar (el aviso de IDLE no llego).  Se anotan aqui
// para que el lazo las lea ANTES de re-armar y no se pierdan.  El contador del
// DMA (palabras que le faltaban) dice hasta donde escribio.
void Serial9BitStm32::rescatarPendientes()
{
    if (huart_ == nullptr || huart_->hdmarx == nullptr) return;

    const uint32_t palabrasFaltantes = __HAL_DMA_GET_COUNTER(huart_->hdmarx);
    const uint16_t posicionEscritura =
        static_cast<uint16_t>((RX_PALABRAS - palabrasFaltantes) % RX_PALABRAS);

    // Sin interrupciones: manejarRxEvent() toca los mismos contadores.
    const uint32_t estadoInterrupciones = __get_PRIMASK();
    __disable_irq();
    const uint16_t palabrasNuevas =
        static_cast<uint16_t>((posicionEscritura + RX_PALABRAS - dmaTail_) % RX_PALABRAS);
    anotarPalabras(palabrasNuevas, HAL_GetTick());
    __set_PRIMASK(estadoInterrupciones);
}

// Es lo primero que se llama en cada vuelta del lazo, asi que aqui se re-arma
// la recepcion si hubo un error de linea (ruido, overrun): el re-armado no se
// puede hacer dentro de la interrupcion.
//
// NO se re-arma mientras haya palabras sin leer: primero se rescatan las que el
// DMA alcanzo a escribir antes del error y se deja que el lazo las lea; el
// re-armado ocurre en la llamada siguiente, ya con el anillo vacio.  Asi un
// error no se lleva los poleos que ya habian llegado enteros.  El mensaje que
// estaba a medias lo descarta el SAS por su timeout o con el siguiente mensaje.
size_t Serial9BitStm32::available9bit()
{
    if (rearmarRx_) {
        if (!rescatado_) {
            rescatarPendientes();
            rescatado_ = true;
        }
        if (escritas_ == leidas_) {
            rearmarRx_ = false;
            rescatado_ = false;
            armarRx();
        }
    }

    const uint32_t pendientes = escritas_ - leidas_;
    return (pendientes > RX_PALABRAS) ? (size_t)RX_PALABRAS : (size_t)pendientes;
}

// Todo lo pendiente (hasta lo que quepa en 'tamano' bytes) en una sola pasada
// por el anillo.  Cada palabra se escribe en 'destino' como 16 bits (byte bajo
// primero, igual que un uint16_t), sin suponer que 'destino' este alineado.
// Devuelve cuantas PALABRAS copio.
size_t Serial9BitStm32::readBuffer9bit(uint8_t* destino, size_t tamano, uint32_t* marcas)
{
    const size_t maximoPalabras = tamano >> 1;   // palabras de 16 bits que caben
    if (destino == nullptr || maximoPalabras == 0) return 0;

    uint32_t pendientes = escritas_ - leidas_;
    if (pendientes == 0) return 0;

    // Si el lazo se atraso tanto que el DMA dio la vuelta sobre lo no leido, lo
    // mas viejo ya se perdio: se queda con lo que alcanza.
    if (pendientes > RX_PALABRAS) {
        leidas_    = escritas_ - RX_PALABRAS;
        pendientes = RX_PALABRAS;
        statDesbordes_++;
    }
////////////mask
    const size_t palabrasACopiar = (pendientes < maximoPalabras) ? (size_t)pendientes : maximoPalabras;

    uint32_t contadorLectura = leidas_;
    uint16_t indiceAnillo    = static_cast<uint16_t>(contadorLectura % RX_PALABRAS);
    for (size_t palabraCopiada = 0; palabraCopiada < palabrasACopiar; palabraCopiada++) {
        const uint16_t palabra = static_cast<uint16_t>(dmaRx_[indiceAnillo] & MASCARA_9_BITS);
        destino[palabraCopiada << 1]       = static_cast<uint8_t>(palabra & 0xFF);
        destino[(palabraCopiada << 1) + 1] = static_cast<uint8_t>(palabra >> 8);
        if (marcas != nullptr) marcas[palabraCopiada] = marcaRx_[indiceAnillo];
        indiceAnillo = static_cast<uint16_t>((indiceAnillo + 1) % RX_PALABRAS);
        contadorLectura++;
    }
    leidas_ = contadorLectura;
    return palabrasACopiar;
}

// ===========================================================================
//  TRANSMISION
// ===========================================================================
bool Serial9BitStm32::puedeTransmitir()
{
    // Si el TX anterior sigue en curso NO pisamos el buffer: preferimos
    // perder la respuesta (y contarla) antes que mandar una trama corrupta.
    if (txBusy_ || huart_->gState != HAL_UART_STATE_READY) {
        statTxRechazados_++;
        return false;
    }
    return true;
}

size_t Serial9BitStm32::iniciarTx(uint16_t palabras)
{
    txBusy_ = true;

    // Con DMA de TX se usa; sin el, por interrupcion.  Llamar a
    // HAL_UART_Transmit_DMA sin DMA configurado es un hard fault.  Los dos
    // caminos terminan en HAL_UART_TxCpltCallback.
    const HAL_StatusTypeDef st = (huart_->hdmatx != nullptr)
        ? HAL_UART_Transmit_DMA(huart_, reinterpret_cast<uint8_t*>(txBuf_), palabras)
        : HAL_UART_Transmit_IT (huart_, reinterpret_cast<uint8_t*>(txBuf_), palabras);

    if (st != HAL_OK) {
        txBusy_ = false;
        statTxRechazados_++;
        return 0;
    }
    return palabras;
}

// Palabras de 9 bits: el noveno bit va en el bit 8.  Devuelve cuantas acepto
// (0 si el transmisor esta ocupado).
size_t Serial9BitStm32::write9bit(const uint16_t* palabras, size_t largo)
{
    if (huart_ == nullptr || palabras == nullptr) return 0;
    if (largo == 0 || largo > TX_MAX)             return 0;
    if (!puedeTransmitir())                       return 0;

    for (size_t indicePalabra = 0; indicePalabra < largo; indicePalabra++) {
        txBuf_[indicePalabra] = static_cast<uint16_t>(palabras[indicePalabra] & MASCARA_9_BITS);
    }
    return iniciarTx(static_cast<uint16_t>(largo));
}

// Espera a que termine la transmision, con tope: nunca cuelga el lazo.
void Serial9BitStm32::flush9bit()
{
    const uint32_t inicio = HAL_GetTick();
    while (txBusy_ && (HAL_GetTick() - inicio) < FLUSH_MAX_MS) {
    }
}

// ===========================================================================
//  INTERRUPCIONES
// ===========================================================================
void Serial9BitStm32::manejarTxCompleto()
{
    txBusy_ = false;
}

// Solo marcamos.  El re-armado real se hace en available9bit(), fuera de la ISR.
void Serial9BitStm32::manejarError()
{
    statErrores_++;
    rearmarRx_ = true;
}
