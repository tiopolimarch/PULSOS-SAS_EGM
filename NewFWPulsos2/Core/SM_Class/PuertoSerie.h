/*
 * PuertoSerie.h
 *
 *  Created on: Apr 15, 2025
 *      Author: aalam
 */

#ifndef INC_PUERTOSERIE_H_
#define INC_PUERTOSERIE_H_

#pragma once

//#include "stm32f4xx_hal.h"
#include "main.h"
#include "stdint.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>  // Para manejar argumentos variables
//#include "cmsis_os.h"  // necesario para TaskHandle_t
//#include "queue.h"


class PuertoSerie {
public:
    PuertoSerie();
    void init(UART_HandleTypeDef* huart);
    bool write(uint8_t* data, uint16_t size);

    // Retorna true si la transmisión fue exitosa.
    //bool write9bit(uint16_t data);

	bool Print(const char *format, ...);
    bool Println(const char *format, ...);
    uint8_t read();
    uint16_t readBuffer(uint8_t* buffer, uint16_t size);
    uint16_t available() const;
    void flush();
    void setBaudRate(uint32_t baud);

    void handleIdleIRQ(uint16_t Size);
    void handleTxComplete();
	
	//void registrarTarea(TaskHandle_t tarea);

private:
    UART_HandleTypeDef* huart_;

    static constexpr uint16_t rxBufferSize_ = 512;
    static constexpr uint16_t txBufferSize_ = 512;
    uint8_t rxBuffer_[rxBufferSize_];
    uint8_t rxBufferInternal_[rxBufferSize_];
	char txBuffer_[txBufferSize_];
    volatile uint16_t rxHead_ = 0;
    volatile uint16_t rxTail_ = 0;
	volatile uint16_t txHead_, txTail_;
    volatile bool txBusy_ = false;
	
	//TaskHandle_t tareaNotificada_ = nullptr;
};


#endif /* INC_PUERTOSERIE_H_ */
