/*
 * VariablesGlobales.h
 *
 *  Created on: Dec 24, 2025
 *      Author: Saul Luna
 */

#ifndef SRC_VARIABLESGLOBALES_H_
#define SRC_VARIABLESGLOBALES_H_



#include "stm32f1xx_hal.h"
#include "../SM_Class/PuertoSerie.h"


#define Contador1 CashIn
#define	Contador2 Bet
#define Contador3 CashOut
#define Contador4 Refill
#define Contador5 HandPay



enum class EventoTipo : uint8_t {
	None,
	Contador1,
	Contador2,
	Contador3,
	RamClear,
	EdoContadores,
	VersionFW,
	Diagnostico_STM
};


struct VariablesGlobales {
    // --- Contadores ---
    int Total_In_Meter     = 0;
    int Total_BET_meter    = 0;
    int HopperMeter        = 0;
    int Refill             = 0;
    int HandPayMeter       = 0;

    int Total_In_Meter_prg   = 0;
    int Total_BET_meter_prg  = 0;
    int HopperMeter_prg      = 0;
    int Refill_prg           = 0;
    int HandPayMeter_prg     = 0;

    // --- Buffers BCD ---
    uint8_t Total_In_Meter_prg_A[5]   = {};
    uint8_t Total_BET_meter_prg_A[5]  = {};
    uint8_t HopperMeter_prg_A[5]      = {};
    uint8_t Refill_prg_A[5]           = {};
    uint8_t HandPayMeter_prg_A[5]     = {};
    uint8_t Numero_Apagado_EGM_A[5]   = {};

    // --- Flash ---
    uint8_t  flash_data[64]     = {0};
    uint16_t Word_address       = 0x0000;
    uint16_t DIRECCION_I2C_MEM  = 0xA0;

    // --- JSON buffers ---

    char json_progre[256];



    // --- JSON Estructura ---

    // --- Eventos ---
    int Evento_CashOut = 0;
    int Evento_BET     = 0;
    int Evento_CashIn  = 0;
    int Flag_CashOut = 0;

    // --- Contadores generales ---
    uint32_t Contador_datos_enviados = 0;
    int len2 = 0;


    // --- Valores temporales ---
    int Valor_Actual = 0;
    int Valor_Actual1 = 0;
    int Valor_Actual2 = 0;
    int Valor_Actual3 = 0;

    uint32_t timer_val1 = 0;
    uint32_t timer_val2 = 0;
    uint32_t timer_val3 = 0;
    uint32_t timer_val4 = 0;

    int Time_Out_Cash_In;
    int Time_Out_Cash_Out;

    int Evento_Reset = 0;
    uint32_t tick_Cash_Out = 0;


    const char*  Causas_Reset;
   	const char*  Status_memoria;
   	uint16_t temp_mcu_x10 = 0;
   	int  Numero_Apagado_EGM = 0;
   	uint16_t vmcu_mv = 0;
   	uint16_t vsrc_mv = 0;




};

#endif // VARIABLES_GLOBALES_H
