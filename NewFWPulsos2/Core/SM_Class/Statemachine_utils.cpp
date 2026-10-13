/*
 * Statemachine_utils.cpp
 *
 *  Created on: Sep 1, 2026
 *      Author: Saul Luna
 */




/*
 * StateMachine_Utils.cpp
 *
 * Utilidades: conversión BCD, lectura serial de bajo nivel, manejo de
 * timers/LED, temperatura del MCU y causa de reset.
 * Separado de StateMachine.cpp — la declaración completa de la clase sigue
 * en StateMachine.h.
 *
 */

#include "../SM_Class/StateMachine.h"

void StateMachine::intToBCDArray(int num, byte *bcdArray) {
	    //
	    byte Array_hex[10] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
	    int decena = 0;
	    int unidad = 0;
	    for (int i = 4; i >= 0; --i) {
	        bcdArray[i] = num % 100; // Módulo 10 para obtener los dos digitos menos significativos.
	        num /= 100; // Division para obtener los siguientes 2 digitos.
	    }

	    for(int a = 4; a >= 0 ; --a){
	         decena = bcdArray[a]/ 10;
	         unidad = bcdArray[a] %  10;
	         byte unidadbcd = Array_hex[unidad]; //Obtener el equivalente en hexadecimal de la unidad
	         byte originalValue =  Array_hex[decena]; //Obtener el equivalente en hexadecimal de la decena
	         byte shiftedValue = originalValue << 4;
		     bcdArray[a] = unidadbcd + shiftedValue; //Suma de la unidad + decena para obtener el dato a enviar.
		}

	}


/*
	 * Método para convertir BCD a Decimal.
	 * Se pasa por referencia el dato (byte) del tipo bcd.
	 * Retorna el valor del tipo int correspondiente.
	 *
	 */
int StateMachine::BCDToDecimal(byte bcd) {
	        return ((bcd >> 4) * 10) + (bcd & 0x0F);
	}


	/*
	 * Método convertir un array del tipo BCD a Decimal.
	 * Se pasa por referencia el Array y su tamaño.
	 * Retorna el valor del tipo int correspondiente.
	 *
	 */
int  StateMachine::BCDArrayToDecimal(byte bcdArray[], int size) {
	        int result = 0;
	        for (int i = 0; i < size; ++i) {
	            result = result * 100 + BCDToDecimal(bcdArray[i]);
	        }
	        return result;
	}


bool StateMachine::LeerMensajeSerial( uint8_t* buffer, size_t size) {
    if (Serial != nullptr && Serial->available()) {
        Serial->readBuffer(buffer, size);
        Serial->flush();
        return (buffer[0] == '{'); // JSON válido si empieza con '{'
    }
    return false;
}


uint16_t StateMachine::Detiene_Reinica_Timer(int CNT_Number){
	if(CNT_Number == 3){
		TIM3->CNT = 0;
	}else{
		TIM4->CNT = 0;
	}
			HAL_TIM_Base_Stop(_htim4);
		    // Start timer
		    HAL_TIM_Base_Start(_htim4);
		    return  __HAL_TIM_GET_COUNTER(_htim4);

}


// Implementación de métodos auxiliares
void StateMachine::limpiarFlags(volatile Flags &Obj_Flags) {
			Obj_Flags.Flag_Botton_CashOut   = 0;
			Obj_Flags.Flag_Total_In_Meter   = 0;
			Obj_Flags.Flag_Total_BET_meter  = 0;
			Obj_Flags.Flag_HopperMeter 		= 0;
			Obj_Flags.Flag_Refill			= 0;
			Obj_Flags.Flag_HandPayMeter     = 0;
}


void StateMachine::IniciarTimer() {
   if(_htim4 != nullptr){
        HAL_TIM_Base_Start(_htim4);
        timer_val4 = __HAL_TIM_GET_COUNTER(_htim4);
    }
	done = 0;
}


void StateMachine::handleTimers() {
    if (_htim4 == nullptr) return;

    uint32_t currentTimer = __HAL_TIM_GET_COUNTER(_htim4);

    // Timer de 5 segundos
    if (currentTimer - timer_val4 >= TIM_5s) {
        timer_val4 =  Detiene_Reinica_Timer(4);
       // Serial->Println("Reinicio Timer");
        if (bandera_on == 0) {
            enviarEventoOn();
        }

        // Reset del watchdog
        //Serial->Println("Reset del watchdog");
        if (_hiwdg != nullptr)  HAL_IWDG_Refresh(_hiwdg);


    }

    // Timer para LED
    if (currentTimer - timer_valLED >= TIM_150ms) {
        handleLedParpadeo();
    }
}


void StateMachine::handleLedParpadeo() {
    if (Flag_parpadeo_inicial < MAX_PARPADEO_INICIAL) {
        Flag_parpadeo_inicial++;
        timer_valLED = __HAL_TIM_GET_COUNTER(_htim4);
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
}


int16_t StateMachine::Leer_Temperatura_MCU_x10(void)
{
	int32_t temp_c_x10;

    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t adc_temp;
    int32_t Vsense_mv;


    HAL_ADC_Stop(hadc1);
    /* Configurar ADC para sensor de temperatura */
    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(hadc1, &sConfig);

    HAL_ADC_Start(hadc1);
    HAL_ADC_PollForConversion(hadc1, HAL_MAX_DELAY);
    adc_temp = HAL_ADC_GetValue(hadc1);
    HAL_ADC_Stop(hadc1);

    /* Convertir ADC → mV */
   Vsense_mv = (adc_temp * VREF_MV) / 4095;

    /* Convertir a °C ×10 */
    temp_c_x10 = ((Vsense_mv - V25_MV) * 10) / AVG_SLOPE_MV + 250;

    /* Restaurar ADC a canal 0 (voltaje) */
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(hadc1, &sConfig);
    HAL_ADC_Start(hadc1);

    return (int16_t)temp_c_x10;
}


ResetCause_t StateMachine::Obtener_Causa_Reset(void)
{
    uint32_t flags = RCC->CSR;
    ResetCause_t cause = RESET_UNKNOWN;

    if (flags & RCC_CSR_PORRSTF)      cause = RESET_POWERON;
    else if (flags & RCC_CSR_PINRSTF) cause = RESET_PIN;
    else if (flags & RCC_CSR_SFTRSTF) cause = RESET_SOFTWARE;
    else if (flags & RCC_CSR_IWDGRSTF)cause = RESET_IWDG;
    else if (flags & RCC_CSR_WWDGRSTF)cause = RESET_WWDG;
    else if (flags & RCC_CSR_LPWRRSTF)cause = RESET_LOWPOWER;

    /* Limpiar flags para próximo reset */
    __HAL_RCC_CLEAR_RESET_FLAGS();

    return cause;
}


const char* StateMachine::ResetCause_ToString(ResetCause_t cause)
{
    switch (cause)
    {
        case RESET_POWERON:  return "por";
        case RESET_PIN:      return "pin";
        case RESET_SOFTWARE: return "sft";
        case RESET_IWDG:     return "wdg";
        case RESET_WWDG:     return "wwd";
        case RESET_LOWPOWER: return "lpw";
        default:             return "unk";
    }
}


// Envía un pulso general solo se debe indicar el Puerto y el pin.
void StateMachine::PulsoGeneral(GPIO_TypeDef* puerto, uint16_t pin, uint32_t tiempoPulso_ms)
{
    HAL_GPIO_WritePin(puerto, pin, GPIO_PIN_RESET);
  //  osDelay(tiempoPulso_ms);
    HAL_GPIO_WritePin(puerto, pin, GPIO_PIN_SET);
  //  osDelay(40);
}
