// ============================================================
//  GeneradorPulsos.cpp — Generador de pulsos GPIO no bloqueante
//  para STM32 con HAL.
//
//  Diagrama de una secuencia de 3 pulsos:
//
//  HIGH ──┐        ┌──────┐        ┌──────┐        ┌──────────
//         │        │      │        │      │        │
//  LOW    └────────┘      └────────┘      └────────┘
//         |←80ms→| |←80ms→|←80ms→| |←80ms→|←80ms→|
//         pulso 1          pulso 2          pulso 3
//
//  El pin permanece en HIGH mientras no hay pulsos pendientes.
//  addPulses() puede llamarse en cualquier momento; los pulsos
//  se acumulan y se generan de forma continua.
// ============================================================

#include "GeneradorPulsos.h"

// ====================================================================
//  Singleton
// ====================================================================

GeneradorPulsos* GeneradorPulsos::_instance = nullptr;

GeneradorPulsos* GeneradorPulsos::getInstance() {
    if (_instance == nullptr) {
        static GeneradorPulsos inst;   // almacenamiento estático, sin heap
        _instance = &inst;
    }
    return _instance;
}

void GeneradorPulsos::destroyInstance() {
    // La instancia es estática; solo invalidamos el puntero.
    _instance = nullptr;
}

// ====================================================================
//  Constructor
// ====================================================================

GeneradorPulsos::GeneradorPulsos()
    : _port(nullptr)
    , _pin(0)
    , _initialized(false)
    , _state(STATE_IDLE)
    , _pending(0)
    , _stateStart(0)
{}

// ====================================================================
//  init()  —  configurar GPIO
// ====================================================================

void GeneradorPulsos::init(GPIO_TypeDef* port, uint16_t pin) {
    _port        = port;
    _pin         = pin;
    _initialized = true;
    _state       = STATE_IDLE;
    _pending     = 0;
    _stateStart  = 0;

    // Estado de reposo = pin HIGH
    _setHigh();
}

// ====================================================================
//  addPulses()  —  encolar N pulsos (no bloqueante)
// ====================================================================

void GeneradorPulsos::addPulses(uint32_t pulsos) {
    if (!_initialized || pulsos == 0) return;

    _pending += pulsos;

    // Si estábamos en reposo, arrancar inmediatamente el primer pulso
    if (_state == STATE_IDLE) {
        _startNextPulse();
    }
    // Si ya hay pulsos en curso, el update() los consumirá de forma
    // transparente — no se necesita ninguna acción adicional aquí.
}

// ====================================================================
//  update()  —  llamar desde el main loop
//  Tiempo de ejecución por llamada: < 1 µs (solo comparaciones)
// ====================================================================

void GeneradorPulsos::update() {
    if (!_initialized || _state == STATE_IDLE) return;

    const uint32_t now     = HAL_GetTick();
    const uint32_t elapsed = now - _stateStart;

    switch (_state) {

        // ---- Fase LOW: esperar PULSE_LOW_MS y luego subir el pin ----
        case STATE_PULSE_LOW:
            if (elapsed >= PULSE_LOW_MS) {
                _setHigh();
                _state      = STATE_PULSE_HIGH;
                _stateStart = now;
            }
            break;

        // ---- Fase HIGH: esperar PULSE_HIGH_MS; fin de un pulso ----
        case STATE_PULSE_HIGH:
            if (elapsed >= PULSE_HIGH_MS) {
                // El pulso actual ha terminado
                if (_pending > 0) {
                    _pending--;
                }

                if (_pending > 0) {
                    // Quedan pulsos → iniciar el siguiente inmediatamente
                    _startNextPulse();
                } else {
                    // Sin más pulsos → reposo
                    _state = STATE_IDLE;
                    // El pin ya está en HIGH desde el inicio de STATE_PULSE_HIGH
                }
            }
            break;

        default:
            // STATE_IDLE: no debe llegar aquí (guard al inicio)
            break;
    }
}

// ====================================================================
//  _startNextPulse()  —  helper interno
// ====================================================================

void GeneradorPulsos::_startNextPulse() {
    _setLow();
    _state      = STATE_PULSE_LOW;
    _stateStart = HAL_GetTick();
}
