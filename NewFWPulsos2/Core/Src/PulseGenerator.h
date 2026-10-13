/**
 * @file    PulseGenerator.hpp
 * @brief   Generador de pulsos no bloqueante — 80 ms HIGH / 80 ms LOW
 *          Plataforma : STM32F103C8T6 (Blue Pill)
 *          Timer      : TIM4  (APB1, timer-clock = 72 MHz)
 *
 * @details Genera una señal cuadrada de 80 ms en alto y 80 ms en bajo sobre
 *          cualquier pin GPIO, usando únicamente interrupciones de timer.
 *          No emplea HAL_Delay ni ningún mecanismo bloqueante.
 *
 *          La generación se controla con la bandera pública Flag_Pulse:
 *            Flag_Pulse = true  →  pulsos activos   (equivale a llamar start())
 *            Flag_Pulse = false →  pulsos detenidos (equivale a llamar stop())
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *  CADENA DE RELOJ (SystemClock_Config vigente)
 * ─────────────────────────────────────────────────────────────────────────────
 *   HSE  =  8 MHz  →  PLL × 9  =  72 MHz  (SYSCLK / HCLK)
 *   APB1 divider   =  /2        →  APB1 periph = 36 MHz
 *   Regla STM32    : si APBx divider ≠ 1  →  timer-clock = APBx × 2
 *                    ∴  TIM4 clock = 36 MHz × 2 = 72 MHz  ✓
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *  CONFIGURACIÓN DE TIM4 EN CubeMX (valores a usar)
 * ─────────────────────────────────────────────────────────────────────────────
 *   Clock Source     : Internal Clock
 *   Prescaler (PSC)  : 7200 − 1  →  tick = 72 MHz / 7200 = 10 kHz
 *   Counter Period   : 800  − 1  →  overflow cada 800 ticks = 80 ms  ✓
 *   Counter Mode     : Up
 *   Auto-Reload      : Enable  (PRELOAD puede quedar Disable, no afecta)
 *   NVIC             : TIM4 global interrupt → Enable
 *
 *  ⚠  El CubeMX generado trae Period = 65535 (≈ 6.5 s).
 *     Cambiar a Period = 799  para obtener exactamente 80 ms.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *  INTEGRACIÓN MÍNIMA
 * ─────────────────────────────────────────────────────────────────────────────
 *  // 1. Inicializar (una sola vez en main)
 *  PulseGenerator::getInstance().init(&htim4, GPIOA, GPIO_PIN_0);
 *
 *  // 2. Conectar al callback del timer
 *  extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim) {
 *      PulseGenerator::getInstance().onTimerTick(htim);
 *  }
 *
 *  // 3. Controlar
 *  PulseGenerator::getInstance().start();   // Flag_Pulse = true
 *  PulseGenerator::getInstance().stop();    // Flag_Pulse = false
 *
 * @version 1.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
    #include "stm32f1xx_hal.h"
#ifdef __cplusplus
}
#endif


class PulseGenerator
{
public:
    /* ------------------------------------------------------------------ */
    /*  Singleton — acceso global único                                    */
    /* ------------------------------------------------------------------ */
    static PulseGenerator& getInstance();

    /* ------------------------------------------------------------------ */
    /*  Inicialización                                                     */
    /* ------------------------------------------------------------------ */
    /**
     * @brief  Configura la librería y arranca el timer con interrupciones.
     *         Los pulsos NO empiezan hasta llamar start() o poner
     *         Flag_Pulse = true.
     *
     * @param  htim  Handle del TIMx configurado con periodo de 80 ms.
     * @param  port  Puerto GPIO de la señal de salida (ej. GPIOA).
     * @param  pin   Pin  GPIO de la señal de salida  (ej. GPIO_PIN_0).
     */
    void init(TIM_HandleTypeDef* htim, GPIO_TypeDef* port, uint16_t pin);

    /* ------------------------------------------------------------------ */
    /*  Control de pulsos                                                  */
    /* ------------------------------------------------------------------ */
    /**
     * @brief  Activa la generación de pulsos.
     *         Equivale a  Flag_Pulse = true.
     */
    void start();

    /**
     * @brief  Detiene la generación de pulsos y deja el pin en LOW.
     *         Equivale a  Flag_Pulse = false.
     */
    void stop();

    /* ------------------------------------------------------------------ */
    /*  Callback del timer — OBLIGATORIO conectar                         */
    /* ------------------------------------------------------------------ */
    /**
     * @brief  Llamar desde HAL_TIM_PeriodElapsedCallback().
     *         Realiza el toggle del pin cada 80 ms si Flag_Pulse == true.
     *         Si el htim no corresponde al timer inicializado, retorna
     *         inmediatamente sin efecto.
     *
     * @param  htim  Puntero recibido en el callback (para filtrar el timer).
     */
    void onTimerTick(TIM_HandleTypeDef* htim);

    /* ------------------------------------------------------------------ */
    /*  Bandera de control (pública)                                      */
    /* ------------------------------------------------------------------ */
    /**
     * @brief  true  → pulsos habilitados
     *         false → pulsos detenidos
     *
     * @note   Puede leerse y escribirse directamente desde cualquier
     *         parte del firmware (ISR, callbacks, main loop).
     *         start() / stop() son equivalentes pero además controlan
     *         el estado del pin GPIO.
     */
    volatile bool Flag_Pulse;

    /* ------------------------------------------------------------------ */
    /*  Regla de los 5 — singleton no es copiable ni movible              */
    /* ------------------------------------------------------------------ */
    PulseGenerator(const PulseGenerator&)            = delete;
    PulseGenerator& operator=(const PulseGenerator&) = delete;
    PulseGenerator(PulseGenerator&&)                 = delete;
    PulseGenerator& operator=(PulseGenerator&&)      = delete;

private:
    PulseGenerator();   /* Constructor privado — solo getInstance() lo llama */

    TIM_HandleTypeDef* _htim;
    GPIO_TypeDef*      _port;
    uint16_t           _pin;
    bool               _initialized;
};
