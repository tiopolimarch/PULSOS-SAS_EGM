/*
 * PuertoSerie.cpp
 *
 * Implementación de la clase PuertoSerie para comunicación UART con DMA y buffer circular.
 * Incluye recepción con interrupción IDLE, transmisión no bloqueante, y funciones Print/Println.
 *
 * Autor: aalam
 * Fecha: abril 15, 2025
 */

#include "PuertoSerie.h"


//#define	DEPURACION

// Constructor por defecto. Inicializa punteros.
PuertoSerie::PuertoSerie() : huart_(nullptr) {}

// Inicializa el UART con DMA y activa la interrupción IDLE.
// Parámetro: puntero al handler UART
void PuertoSerie::init(UART_HandleTypeDef* huart) {
    huart_ = huart;

	flush();
	txBusy_ = false;
	txHead_ = txTail_ = 0;

	//huart_->Init.WordLength = UART_WORDLENGTH_8B;
	HAL_UARTEx_ReceiveToIdle_DMA(huart_, rxBuffer_, rxBufferSize_);
	       // __HAL_UART_ENABLE_IT(huart_, UART_IT_IDLE);
	        __HAL_DMA_DISABLE_IT(huart_->hdmarx, DMA_IT_HT);
}

// Envía un bloque de datos usando buffer circular + DMA (no bloqueante).
// Retorna true si se añadió correctamente al buffer, false si está lleno.
bool PuertoSerie::write(uint8_t* data, uint16_t size) {
	for (uint16_t i = 0; i < size; ++i) {
        uint16_t nextHead = (txHead_ + 1) % txBufferSize_;
        if (nextHead == txTail_) return false; // buffer lleno
        txBuffer_[txHead_] = data[i];
        txHead_ = nextHead;
    }

    if (!txBusy_) {
        txBusy_ = true;
        uint16_t len = 0;
        uint16_t start = txTail_;
        while (start != txHead_ && len < txBufferSize_) {
            len++;
            start = (start + 1) % txBufferSize_;
        }
        if (txTail_ + len <= txBufferSize_) {
            HAL_UART_Transmit_DMA(huart_, (uint8_t *) &txBuffer_[txTail_], len);
            txTail_ = txHead_;
        } else {
            uint16_t firstChunk = txBufferSize_ - txTail_;
            HAL_UART_Transmit_DMA(huart_,(uint8_t *) &txBuffer_[txTail_], firstChunk);
            txTail_ = (txTail_ + firstChunk) % txBufferSize_;  // ✅ avanzar solo lo que se transmitió
        }
    }
    return true;
}


// Envía un string formateado sin salto de línea.
// Retorna true si la transmisión fue aceptada.
bool PuertoSerie::Print(const char *format, ...) {

	char temp[128];
	va_list args;
	va_start(args, format);
	int len = vsnprintf(temp, sizeof(temp), format, args);
	va_end(args);
	return write(reinterpret_cast<uint8_t*>(temp), len);
}
// Envía un string formateado con salto de línea CRLF.
// Retorna true si la transmisión fue aceptada.
bool PuertoSerie::Println(const char *format, ...) {

	char temp[128];
	va_list args;
	va_start(args, format);
	int len = vsnprintf(temp, sizeof(temp) - 2, format, args);
	va_end(args);
	temp[len++] = '\r';
	temp[len++] = '\n';
	temp[len] = '\0';
	return write(reinterpret_cast<uint8_t*>(temp), len);
}

// Lee hasta 'size' bytes disponibles en el buffer de recepción.
// Retorna el número de bytes leídos.
uint16_t PuertoSerie::readBuffer(uint8_t* buffer, uint16_t size) {
	uint16_t count = 0;
	#ifdef	DEPURACION
		char temp[32];
		sprintf(temp,"Header: %d\r\n", rxHead_);
		write((uint8_t*)temp, strlen(temp));
		while(txBusy_);
	#endif
	while (rxTail_ != rxHead_ && count < size) {
		buffer[count++] = rxBuffer_[rxTail_];
		rxTail_ = (rxTail_ + 1) % rxBufferSize_;
	}

	return count;
}

// Lee un solo byte del buffer de recepción.
// Retorna el byte si disponible, o 0 si está vacío.
uint8_t PuertoSerie::read(){
	if (rxTail_ != rxHead_) {
		uint8_t byte = rxBuffer_[rxTail_];
		rxTail_ = (rxTail_ + 1) % rxBufferSize_;
		return byte;
	}
	return 0;
}

// Retorna la cantidad de bytes disponibles para lectura.
uint16_t PuertoSerie::available() const {
    if (rxHead_ >= rxTail_) {
        return rxHead_ - rxTail_;
    } else {
        return rxBufferSize_ - rxTail_ + rxHead_;
    }
}
// Limpia los índices de recepción.
void PuertoSerie::flush() {
    rxHead_ = rxTail_ = 0;
}
// Cambia el baudrate de la UART de forma dinámica.
// Aplica reconfiguración del periférico y reinicia DMA.
void PuertoSerie::setBaudRate(uint32_t baud) {
    if (huart_) {
        huart_->Init.BaudRate = baud;
        HAL_UART_DeInit(huart_);
        HAL_UART_Init(huart_);
        HAL_UARTEx_ReceiveToIdle_DMA(huart_, rxBuffer_, rxBufferSize_);
               // __HAL_UART_ENABLE_IT(huart_, UART_IT_IDLE);
                __HAL_DMA_DISABLE_IT(huart_->hdmarx, DMA_IT_HT);
    }
}
// Rutina a llamar desde el handler UART cuando se detecta IDLE.
// Actualiza rxHead_ en función del contador DMA restante.
void PuertoSerie::handleIdleIRQ(uint16_t Size) {
 //   if (__HAL_UART_GET_FLAG(huart_, UART_FLAG_IDLE)) {
      //  __HAL_UART_CLEAR_IDLEFLAG(huart_);

	for (uint16_t i = 0; i < Size; ++i) {
					uint16_t next = (rxHead_ + 1) % rxBufferSize_;
					if (next != rxTail_) { // Previene sobreescritura
						rxBufferInternal_[rxHead_] = rxBuffer_[i];
						rxHead_ = next;
					}
		        }

        // Calcula nueva posición de escritura (DMA)
     //   uint16_t remaining = __HAL_DMA_GET_COUNTER(huart_->hdmarx);
	//	if (remaining > rxBufferSize_) remaining = 0;
	//		rxHead_ = rxBufferSize_ - remaining;
			
		// Notificación a la tarea registrada
       // if (tareaNotificada_) {
         //   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
          //  vTaskNotifyGiveFromISR(tareaNotificada_, &xHigherPriorityTaskWoken);
           // portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        //}

		#ifdef DEPURACION
			const char* msg = "[IDLE IRQ]\r\n";
			write((uint8_t*)msg, strlen(msg));
			//while(txBusy_);
		#endif		
  //  }

     HAL_UARTEx_ReceiveToIdle_DMA(huart_, rxBuffer_, rxBufferSize_);
       // __HAL_UART_ENABLE_IT(huart_, UART_IT_IDLE);
        __HAL_DMA_DISABLE_IT(huart_->hdmarx, DMA_IT_HT);
}
// Rutina a llamar desde el callback HAL_UART_TxCpltCallback.
// Maneja la continuación de transmisión por DMA usando buffer circular.
void PuertoSerie::handleTxComplete() {
	if (txTail_ == txHead_) {
	        txBusy_ = false;
	        return;
	    }

	    txBusy_ = true;

	    uint16_t len = 0;
	    uint16_t start = txTail_;
	    while (start != txHead_ && len < txBufferSize_) {
	        len++;
	        start = (start + 1) % txBufferSize_;
	    }

	    if (len == 0) {
	        txBusy_ = false;
	        return;
	    }

	    if (txTail_ + len <= txBufferSize_) {
	        // Fragmento continuo
	        HAL_UART_Transmit_DMA(huart_, (uint8_t *)&txBuffer_[txTail_], len);
	        txTail_ = (txTail_ + len) % txBufferSize_;
	    } else {
	        // Fragmento dividido: solo enviamos la primera parte
	        uint16_t firstChunk = txBufferSize_ - txTail_;
	        HAL_UART_Transmit_DMA(huart_, (uint8_t *)&txBuffer_[txTail_], firstChunk);
	        txTail_ = 0;  // avanzamos solo el fragmento transmitido
	    }

}



//void PuertoSerie::registrarTarea(TaskHandle_t tarea) {
	//tareaNotificada_ = tarea;
//}
