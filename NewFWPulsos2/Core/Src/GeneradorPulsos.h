#pragma once
// ============================================================
//  GeneradorPulsos.h — Generador de pulsos GPIO no bloqueante
//
//  Genera N pulsos en un pin GPIO:
//    • Nivel activo : LOW  durante PULSE_LOW_MS  (80 ms)
//    • Nivel reposo : HIGH durante PULSE_HIGH_MS (80 ms)
//    • Idle         : pin en HIGH
//
//  Características:
//    • Singleton (misma instancia desde cualquier módulo)
//    • Completamente no bloqueante — usa HAL_GetTick()
//    • Acumulación: addPulses() mientras se generan pulsos
//      simplemente suma al contador pendiente; no se pierden
//      pulsos entre billetes.
//    • Compatible STM32 HAL (F4 / F4xx)
//
//  Uso típico:
//    // Init (una sola vez)
//    GeneradorPulsos::getInstance()->init(PULSOS_GPIO_Port, PULSOS_Pin);
//
//    // Desde el callback del billetero
//    GeneradorPulsos::getInstance()->addPulses(amount);   // p.ej. 50
//
//    // En el bucle principal (junto a uba->update())
//    GeneradorPulsos::getInstance()->update();
// ============================================================

#include "main.h"   // GPIO_TypeDef, HAL_GetTick, GPIO_PIN_SET / RESET
#include <stdint.h>

class GeneradorPulsos {
public:
    // ================================================================
    //  Tiempos de pulso (ajustar aquí si se necesita cambiar)
    // ================================================================
    static const uint32_t PULSE_LOW_MS  = 80u;  ///< Duración fase LOW  (activo)
    static const uint32_t PULSE_HIGH_MS = 80u;  ///< Duración fase HIGH (reposo)

    // ================================================================
    //  Singleton
    // ================================================================
    static GeneradorPulsos* getInstance();
    static void destroyInstance();

    // ================================================================
    //  Configuración — llamar UNA VEZ antes de usar
    // ================================================================

    /**
     * Asigna el pin GPIO de salida.
     * El pin queda en estado HIGH (reposo) al finalizar.
     * @param port  Puerto GPIO (p.ej. GPIOA)
     * @param pin   Máscara de pin (p.ej. GPIO_PIN_5)
     */
    void init(GPIO_TypeDef* port, uint16_t pin);

    // ================================================================
    //  API principal
    // ================================================================

    /**
     * Encola N pulsos para generar.
     * Si ya se están generando pulsos, los nuevos se suman al total
     * pendiente — NO se interrumpe la secuencia en curso.
     *
     * @param pulsos  Número de pulsos a añadir (p.ej. 50 para $50 MXN)
     */
    void addPulses(uint32_t pulsos);

    /**
     * Motor de la máquina de estados del generador.
     * DEBE llamarse repetidamente desde el bucle principal (main loop).
     * No bloquea: cada llamada tarda microsegundos.
     */
    void update();

    // ================================================================
    //  Consulta de estado
    // ================================================================

    /** true mientras hay pulsos pendientes o en curso. */
    bool     isBusy()          const { return _state != STATE_IDLE; }

    /** Número de pulsos todavía por completar (incluyendo el actual). */
    uint32_t getPendingPulses() const { return _pending; }

private:
    // ---- Constructor/destructor privados ----
    GeneradorPulsos();
    ~GeneradorPulsos() = default;

    // No copiable
    GeneradorPulsos(const GeneradorPulsos&)            = delete;
    GeneradorPulsos& operator=(const GeneradorPulsos&) = delete;

    // ---- Instancia singleton ----
    static GeneradorPulsos* _instance;

    // ---- Hardware ----
    GPIO_TypeDef* _port;
    uint16_t      _pin;
    bool          _initialized;

    // ---- Máquina de estados interna ----
    enum PulseState : uint8_t {
        STATE_IDLE,        ///< Pin HIGH, sin pulsos pendientes
        STATE_PULSE_LOW,   ///< Pin LOW  — esperando PULSE_LOW_MS
        STATE_PULSE_HIGH,  ///< Pin HIGH — esperando PULSE_HIGH_MS (fin de un pulso)
    };

    PulseState _state;
    uint32_t   _pending;     ///< Pulsos todavía por completar
    uint32_t   _stateStart;  ///< HAL_GetTick() al entrar en el estado actual

    // ---- Helpers GPIO ----
    inline void _setHigh() {
        if (_port) HAL_GPIO_WritePin(_port, _pin, GPIO_PIN_SET);
         HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

    }
    inline void _setLow() {
        if (_port) HAL_GPIO_WritePin(_port, _pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }

    /** Inicia el primer pulso (transición IDLE → PULSE_LOW). */
    void _startNextPulse();
};
