/**
 * @file    PulseGenerator.cpp
 * @brief   Implementación del generador de pulsos no bloqueante
 *          80 ms HIGH / 80 ms LOW — STM32F103C8T6 (Blue Pill)
 *
 *          Timer     : TIM4
 *          PSC       : 7200 − 1   →  tick   = 10 kHz  (TIM4 clk = 72 MHz)
 *          ARR       : 800  − 1   →  período = 80 ms
 *
 * @version 1.1  — Adaptado a TIM4 con SystemClock HSE 8 MHz × PLL9 = 72 MHz
 */

#include "PulseGenerator.h"
#include "main.h"

/* ====================================================================== */
/*  Constructor privado                                                    */
/* ====================================================================== */
PulseGenerator::PulseGenerator()
    : Flag_Pulse(false),
      _htim(nullptr),
      _port(nullptr),
      _pin(0),
      _initialized(false)
{}


/* ====================================================================== */
/*  Singleton                                                              */
/* ====================================================================== */
PulseGenerator& PulseGenerator::getInstance()
{
    /* Instancia estática — creada una sola vez, destruida al final       */
    static PulseGenerator instance;
    return instance;
}


/* ====================================================================== */
/*  init                                                                   */
/* ====================================================================== */
void PulseGenerator::init(TIM_HandleTypeDef* htim, GPIO_TypeDef* port, uint16_t pin)
{
    /* Guardar referencias de hardware */
    _htim        = htim;
    _port        = port;
    _pin         = pin;
    _initialized = true;

    /* Estado inicial: pin en LOW, pulsos desactivados */
    Flag_Pulse = false;
    HAL_GPIO_WritePin(_port, _pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin( LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

    /*
     * Arrancar TIM4 con interrupciones.
     * El período ya debe ser 799 (80 ms a 10 kHz) — ver MX_TIM4_Init().
     * Los pulsos no se generan aún porque Flag_Pulse == false;
     * el timer queda corriendo en espera de start().
     */
    HAL_TIM_Base_Start_IT(_htim);
}


/* ====================================================================== */
/*  start — activa pulsos                                                  */
/* ====================================================================== */
void PulseGenerator::start()
{
    if (!_initialized) return;

    /*
     * Levantar la bandera.
     * En el próximo tick del timer (máx. 80 ms) comienza el toggle.
     */
    Flag_Pulse = true;
}


/* ====================================================================== */
/*  stop — detiene pulsos                                                  */
/* ====================================================================== */
void PulseGenerator::stop()
{
    if (!_initialized) return;

    /* Bajar la bandera primero para evitar race condition con la ISR */
    Flag_Pulse = false;

    /* Apagar el pin inmediatamente (no esperar al próximo tick) */
    HAL_GPIO_WritePin(_port, _pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin( LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}


/* ====================================================================== */
/*  onTimerTick — conectar a HAL_TIM_PeriodElapsedCallback                */
/* ====================================================================== */
void PulseGenerator::onTimerTick(TIM_HandleTypeDef* htim)
{
    /* Descartar si la librería no está lista */
    if (!_initialized) return;

    /* Descartar interrupciones de otros timers */
    if (htim->Instance != _htim->Instance) return;

    if (Flag_Pulse)
    {
        /*
         * Toggle cada 80 ms  →  80 ms HIGH + 80 ms LOW = señal cuadrada
         * HAL_GPIO_TogglePin es seguro desde ISR: accede al registro BSRR
         * con operaciones atómicas de 32 bits.
         */
        HAL_GPIO_TogglePin(_port, _pin);
        HAL_GPIO_TogglePin( LED_GPIO_Port, LED_Pin);
    }
    /*
     * Si Flag_Pulse == false, no hacemos nada:
     * el pin ya quedó en LOW desde stop().
     */
}
