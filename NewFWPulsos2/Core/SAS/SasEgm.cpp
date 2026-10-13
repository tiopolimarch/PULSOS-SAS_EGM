/*
 * SasEgm.cpp
 *
 *  MOTOR SAS COMPLETO.  Todo lo que habla el protocolo vive aqui.
 *
 *  Que hay adentro:
 *     - el armado de tramas: junta las palabras de 9 bits que entrega la
 *       serial en mensajes SAS completos (la tabla de largos de los comandos
 *       es cosa del protocolo, por eso vive aqui y no en la serial)
 *     - el CRC y el codificador BCD
 *     - los armadores de trama y el despachador de long polls
 *     - la atencion del bus y la cola de excepciones
 *     - la contabilidad: el saldo, los ajustes y el puente a los medidores
 *     - la vigilancia del enlace
 *
 *  Lo unico del SAS que NO esta aqui es el AFT, que tiene su propio archivo
 *  -SasEgm_aft.cpp- porque es una pieza que se entiende sola.
 *
 *  Las constantes, los codigos con nombre y las variables de estado estan
 *  todos en SasEgm.h.
 */

#include <string.h>
#include "SasEgm.h"

// =========================================================================
//  LA UNICA LLAMADA DEL LAZO
//
//  El orden importa:
//
//    1. recibirPalabras()   arma los mensajes con lo que llego y limpia los
//                           que quedaron a medias
//    2. atenderSas()        saca un mensaje de la cola y contesta
//    3. el saldo sigue a los pulsos fisicos (si se vincularon contadores)
//    4. reintentos, timeouts y vigilancia del enlace
// =========================================================================
void SasEgm::servicio() {
    if (!iniciado_) return;   // init() no corrio o fallo

    recibirPalabras();
    atenderSas();

    actualizarCreditoPorPulsos();
    reemitirExcepcionHandpay();
    vigilarBloqueoAft();
    vigilarEnlace();
}

// ---------------------------------------------------------------------------
//  LA INSTANCIA UNICA
//
//  Estatica de funcion y no global: asi se construye la primera vez que
//  alguien la pide y no antes.  En un arranque de STM32 eso importa, porque
//  el orden en que se construyen las globales entre archivos no esta definido
//  y una que dependa de otra puede encontrarsela a medio hacer.
// ---------------------------------------------------------------------------
SasEgm* SasEgm::getInstance() {
    static SasEgm unica;
    return &unica;
}

// ---------------------------------------------------------------------------
//  INICIALIZACION
//
//  init(huart), la version para STM32, esta en SasEgmStm32.cpp: antes de armar
//  el DMA revisa la configuracion del UART (cada error de CubeMX hace que el
//  SAS "no funcione" sin ningun otro sintoma, asi que se detectan alli y se
//  devuelven con nombre), prepara la serial y luego llama a esta.
//
//  Si esta funcion no devuelve OK, el SAS no arranca y servicio() no hace nada.
//  A partir de que devuelve OK el SAS contesta poleos.
// ---------------------------------------------------------------------------
SasInitResultado SasEgm::init(Serial9Bit* serial) {
    iniciado_ = false;
    if (serial == nullptr) return SasInitResultado::SIN_UART;

    serial_ = serial;
    reiniciarEnsamblado();

    iniciado_ = true;
    return SasInitResultado::OK;
}

void SasEgm::vincularContadores(const int* billIn, const int* apostado,
                                const int* cashOut, const int* handpay) {
    pBillIn_   = billIn;
    pApostado_ = apostado;
    pCashOut_  = cashOut;
    pHandpay_  = handpay;
}

// Los callbacks del HAL (onRxEvent, onTxComplete, onError), init(huart) y
// ahoraMs() estan en SasEgmStm32.cpp.

// =========================================================================
// FUNCIONES DE APOYO PARA SAS
// =========================================================================

uint16_t SasEgm::calcularCrc16Kermit(const uint8_t* datos, uint16_t longitud) {
	uint16_t crc = 0x0000;
	for (uint16_t i = 0; i < longitud; i++) {
		crc ^= datos[i];
		for (uint8_t bit = 0; bit < 8; bit++) {
			crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
		}
	}
	return crc;
}


bool SasEgm::enviarTramaSas(const uint8_t* payload, uint16_t longitud) {
	if (longitud > 250) return false;

	// palabras: cada byte de 'payload' pasado a palabra de 9 bits (bit 8 en
	// 0, ver el comentario de arriba).  Solo se usan los primeros 'longitud'.
	uint16_t palabras[250] = {};
	for (uint16_t i = 0; i < longitud; i++) {
		palabras[i] = payload[i];
	}
	const bool enviada = (serial_->write9bit(palabras, longitud) == longitud);

	// Depuracion: se avisa DESPUES de mandar, para no retrasar la respuesta.
	if (hookTramas_ != nullptr) hookTramas_(true, payload, longitud);
	return enviada;
}

// Respuesta corta: direccion + comando + 4 bytes BCD + CRC = 8 bytes.
// Es el formato que contesta la maquina real:  01 1A 96 94 06 99 16 EE
//
// El codificador BCD es el mismo que usa el 0x2F: bcdEncodeSas.
void SasEgm::responderContadorBCD(ComandoSas comando, uint64_t valor) {
	// payload: la trama de 8 bytes que se manda.
	uint8_t payload[8] = {};
	payload[0] = direccionSas_;
	payload[1] = static_cast<uint8_t>(comando);
	bcdEncodeSas(&payload[2], valor, 4);

	uint16_t crc = calcularCrc16Kermit(payload, 6);
	payload[6] = (uint8_t)(crc & 0xFF);
	payload[7] = (uint8_t)((crc >> 8) & 0xFF);
	enviarTramaSas(payload, 8);
}

// ---------------------------------------------------------------------------
//  RESPUESTA DE VARIOS MEDIDORES EN UNA SOLA TRAMA
//
//  direccion + comando + N medidores de 4 BCD + CRC.  No lleva byte de
//  largo: el host ya sabe cuantos medidores trae cada comando, y por eso el
//  orden y la cantidad NO se pueden cambiar.
//
//    0x0F -> 6 medidores (24 bytes de datos), trama de 28
//    0x19 -> 5 medidores (20 bytes de datos), trama de 24
//
//  Si algun dia se manda un medidor de mas o de menos, el host lee todo
//  corrido y le asigna el valor equivocado a cada campo.  Eso no se ve como
//  un error en el sniffer: se ve como una maquina con numeros raros.
// ---------------------------------------------------------------------------
void SasEgm::responderBloqueMedidores(ComandoSas comando,
                                            const uint64_t* valores,
                                            uint8_t nValores) {
	if (valores == nullptr) return;

	// respuesta: la trama que se arma y se manda al final.
	// len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
	uint8_t  respuesta[40] = {}; // Antes r
	uint16_t len = 0;            // Antes n

	// 2 de cabecera + N x 4 + 2 de CRC tiene que caber.
	if ((uint16_t)(2 + nValores * 4 + 2) > sizeof(respuesta)) return;

	respuesta[len++] = direccionSas_;
	respuesta[len++] = static_cast<uint8_t>(comando);

	for (uint8_t i = 0; i < nValores; i++) {
		bcdEncodeSas(&respuesta[len], valores[i], 4);
		len = (uint16_t)(len + 4);
	}

	const uint16_t crc = calcularCrc16Kermit(respuesta, len);
	respuesta[len++] = (uint8_t)(crc & 0xFF);
	respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);
	enviarTramaSas(respuesta, len);
}

// ---------------------------------------------------------------------------
//  ACK DE UN BYTE
//
//  Los long polls que no devuelven datos se contestan con la propia
//  direccion.  El NACK seria esa misma direccion con el bit 7 puesto, pero
//  no se usa: si la trama no cuadra, no se contesta nada, que en SAS es
//  valido y no le promete nada al host.
// ---------------------------------------------------------------------------
void SasEgm::responderAckSimple() {
	const uint8_t ack = direccionSas_;
	enviarTramaSas(&ack, 1);
}

// ---------------------------------------------------------------------------
//  BCD: cada nibble es un digito decimal.  1234 se guarda 0x12 0x34.
//  Se recorta al maximo que cabe para que un desbordamiento no escriba
//  nibbles con valores A-F, que el host leeria como basura.
// ---------------------------------------------------------------------------
void SasEgm::bcdEncodeSas(uint8_t* destino, uint64_t valor, uint8_t nBytes) {
    // -----------------------------------------------------------------
    //  SE SATURA, NO SE DA LA VUELTA.
    //
    //  Si el valor no cabe, el medidor se queda en todo nueves (como un
    //  contador mecanico).  Dar la vuelta con un modulo lo pondria en cero:
    //
    //      99,999,999 creditos  ->  99 99 99 99
    //     100,000,000 creditos  ->  se queda en 99 99 99 99 (no 00 00 00 00)
    //
    //  SIN DIVISIONES NI MODULOS: cada digito se saca restando la potencia de
    //  10 que le toca, de la mas grande a la mas chica (maximo 9 restas por
    //  digito).  El Cortex-M3 no divide uint64_t por hardware.
    // -----------------------------------------------------------------
    static const uint64_t POT10[17] = {
        1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL,
        10000000ULL, 100000000ULL, 1000000000ULL, 10000000000ULL,
        100000000000ULL, 1000000000000ULL, 10000000000000ULL,
        100000000000000ULL, 1000000000000000ULL, 10000000000000000ULL
    };

    // Cada byte BCD lleva dos digitos: el de arriba (nibble alto) y el de abajo.
    const uint8_t  totalDigitos = (uint8_t)(nBytes << 1);
    const uint64_t valorMaximo  = POT10[(totalDigitos < 16) ? totalDigitos : 16] - 1;
    if (valor > valorMaximo) valor = valorMaximo;

    for (uint8_t indiceByte = 0; indiceByte < nBytes; indiceByte++) {
        uint8_t byteBcd = 0;
        for (uint8_t mitadDelByte = 0; mitadDelByte < 2; mitadDelByte++) {   // 0 = nibble alto, 1 = bajo
            // Posicion decimal de este digito: 0 = unidades, 1 = decenas...
            const uint8_t posicionDecimal =
                (uint8_t)(totalDigitos - 1 - ((indiceByte << 1) + mitadDelByte));
            uint8_t digito = 0;
            if (posicionDecimal < 16) {
                while (valor >= POT10[posicionDecimal]) {
                    valor -= POT10[posicionDecimal];
                    digito++;
                }
            }
            byteBcd = (uint8_t)((byteBcd << 4) | digito);
        }
        destino[indiceByte] = byteBcd;
    }
}

// ---------------------------------------------------------------------------
//  Lee un contador vinculado.  El que no se vinculo cuenta como cero, y un
//  valor negativo -contador corrupto- tambien.
// ---------------------------------------------------------------------------
uint64_t SasEgm::leerContador(const int* contador) {
    if (contador == nullptr || *contador < 0) return 0;
    return (uint64_t)(*contador);
}


// ===========================================================================
//  ACCESORES  -  la unica puerta para tocar la contabilidad
//
//  Todo junto a proposito: aqui esta TODO lo que mueve un medidor.  Si un
//  numero cambio y no se sabe por que, esta en este bloque.
//
//  Los ajustes son PROGRESIVOS: se suman al contador de pulsos y nunca se
//  restan.  Un medidor SAS es un acumulado de por vida, y un host que lo ve
//  bajar da por perdida la contabilidad de la maquina.
// ===========================================================================

// --- lectura ---------------------------------------------------------------

uint32_t SasEgm::creditoActual() const { return creditoActual_; }

// El manual prohibe bloquear para AFT si hay un hand pay esperando: seria
// pagar dos veces.  Lo consulta SasEgm_aft.cpp.
bool SasEgm::hayHandpayPendiente() const { return handpayPendiente_; }

// Ordenes del host (long polls 01 a 09).  El firmware las lee en su lazo.
bool     SasEgm::juegoBloqueadoPorHost() const          { return apagadoPorHost_ || juegoDeshabilitado_; }
bool     SasEgm::sonidoApagadoPorHost() const           { return sonidoApagado_; }
bool     SasEgm::billeteroDeshabilitadoPorHost() const  { return billeteroDeshabilitado_; }
uint32_t SasEgm::billeteroDenominacionesPorHost() const { return billeteroMascara_; }

// --- escritura -------------------------------------------------------------

// Fija el saldo (0x1A) y registra la subida como entrada de billetero.
// Sube el 0x0B porque el credito tiene que venir de algun lado: si el 0x1A
// dice 10 y el 0x0B no se movio, el host lo ve aparecido de la nada.
//
// Con el contador del billetero vinculado, el contador ya cuenta esos mismos
// creditos: no se suman al 0x0B otra vez, y se anotan como "pendientes" para que
// el saldo tampoco los reciba dos veces cuando el contador suba (en el 0x1A, 20
// creditos salian 40).  Da igual cual llegue primero, el contador o este metodo.
void SasEgm::set_CurrentCredit(uint32_t creditos) {
    if (creditos > creditoActual_) {
        const uint32_t diferencia = creditos - creditoActual_;
        if (pBillIn_ == nullptr) {
            ajusteBillIn_ += diferencia;
        } else {
            pendienteSetBill_   += diferencia;
            msPendienteSetBill_  = ahoraMs();
        }
    }
    creditoActual_ = creditos;
}

// Pago a mano: sube el 0x23 y el 0x04, baja el saldo con piso en cero.
//
// Con el contador de cash out vinculado, el contador ya cuenta esos mismos
// creditos (medidor y saldo): aqui solo se avisa el hand pay (0x51 y 0x1B) y
// no se suman otra vez, o el 0x04 y el 0x23 salen al doble.
void SasEgm::set_Handpay(uint32_t creditos) {
    if (pCashOut_ == nullptr) {
        ajusteHandpay_ += creditos;

        if (creditos >= creditoActual_) creditoActual_ = 0;
        else                             creditoActual_ -= creditos;
    }

    // Cada hand pay se reporta UNA vez (7.8.1).  Si el anterior ya se leyo
    // el monto empieza de cero; si no, se suma.
    if (handpayPendiente_) handpayMonto_ += creditos;
    else                    handpayMonto_  = creditos;

    handpayPendiente_ = true;

    encolarExcepcionSas(ExcepcionSas::HANDPAY_PENDIENTE);
}

// Apuesta -> medidor 0x00.  Progresivo, solo sube.
void SasEgm::set_Bet(uint32_t creditos) {
    ajusteBet_ += creditos;
}

// Retiro por AFT: sube el 0x18 y baja el saldo.  PRIVADO: solo lo llama el AFT.
//
// Deliberadamente NO toca el 0x04 ni el 0x23 y NO levanta hand pay pendiente.
// Un retiro electronico y un pago a mano son eventos distintos y el casino los
// tiene que poder separar.  Y si levantara hand pay, el manual prohibiria
// bloquear para el SIGUIENTE AFT: dos retiros seguidos no funcionaban.
void SasEgm::set_AftOut(uint32_t creditos) {
    ajusteAftOut_ += creditos;

    if (creditos >= creditoActual_) creditoActual_ = 0;
    else                             creditoActual_ -= creditos;
}

// Cash out propio de la placa, ya pagado.  La bandera se mira AQUI ADENTRO:
// si los pulsos del hopper regresan por su entrada ya suben el medidor solos,
// y eso es una propiedad del cableado, no de quien llama.
void SasEgm::registrarCashOutLocal(uint32_t creditos) {
    if (!CASHOUT_REFLEJA_EN_SAS) return;
    set_Handpay(creditos);
}


// ===========================================================================
//  COLA DE EXCEPCIONES
// ===========================================================================

// El 0x51, 0x52, 0x6F y 0x69 estan marcadas con P -prioritaria- en el manual,
// y por la misma razon: las cuatro llevan dinero detras.
bool SasEgm::excEsPrioritaria(ExcepcionSas codigo) {
    switch (codigo) {
        case ExcepcionSas::HANDPAY_PENDIENTE:
        case ExcepcionSas::HANDPAY_RESETEADO:
        case ExcepcionSas::AFT_COMPLETA:
        case ExcepcionSas::JUEGO_BLOQUEADO:
            return true;
        default:
            return false;
    }
}

void SasEgm::encolarExcepcionSas(ExcepcionSas codigo) {
    if (!EXCEPCIONES_SAS_ACTIVO) return;

    const bool nuevaEsPrio = excEsPrioritaria(codigo);

    // Prioridad, encontrada en el banco y no supuesta: la placa quedaba
    // reenviando un aviso normal -que es lo correcto ante un NACK- y el 0x51
    // del hand pay NUNCA salia.  El aviso menos importante tapaba al unico
    // que de verdad importa.
    //
    //   - una prioritaria pisa lo que sea, pendiente o en vuelo.
    //   - una normal NO pisa a una prioritaria.
    //
    // Perder un 0x4F no cuesta: el 0x0B ya lleva el dinero y el host lo ve en
    // el siguiente 0x2F.  Perder un 0x51 si cuesta.
    if (!nuevaEsPrio) {
        if (excEsPrioritaria(excPendiente_)) { excPisadas_++; return; }
        if (excEnVuelo_ && excEsPrioritaria(excEnVueloCodigo_)) { excPisadas_++; return; }
    }
    else if (excEnVuelo_ && !excEsPrioritaria(excEnVueloCodigo_)) {
        // Se abandona el reenvio de la normal: sin esto el bucle de NACK no
        // suelta nunca a la prioritaria.
        excEnVuelo_ = false;
        excPisadas_++;
    }

    if (excPendiente_ != ExcepcionSas::NINGUNA && excPendiente_ != codigo) excPisadas_++;

    excPendiente_ = codigo;

    // El reloj de la reemision arranca aqui, no en el arranque de la placa.
    if (codigo == ExcepcionSas::HANDPAY_PENDIENTE) msUltimaExc51_ = ahoraMs();
}


// ---------------------------------------------------------------------------
//  EL SALDO SIGUE A LOS PULSOS REALES
//
//  Esto corre en cada vuelta del lazo.  Mira cuanto se movieron los
//  contadores de pulsos desde la vuelta anterior y aplica esa diferencia al
//  saldo: lo que entra por billetero sube, lo que sale por cash out baja.
//
//  Trabajo por DIFERENCIAS y no restando los dos acumulados, porque esos dos
//  numeros se empezaron a contar en momentos distintos y se han borrado en
//  momentos distintos: su resta no es un saldo, no significa nada.
//
//  Un delta negativo significa que alguien borro los contadores (RAM clear).
//  En ese caso no resto nada, solo vuelvo a tomar la referencia: restarlo le
//  quitaria al jugador credito que si tenia.
// ---------------------------------------------------------------------------
void SasEgm::actualizarCreditoPorPulsos() {
    // Sin contadores vinculados el saldo lo llevan solo los set_*.
    if (pBillIn_ == nullptr && pCashOut_ == nullptr) return;

    const int32_t billIn  = (pBillIn_  != nullptr) ? (int32_t)*pBillIn_  : 0;
    const int32_t cashOut = (pCashOut_ != nullptr) ? (int32_t)*pCashOut_ : 0;

    if (primeraLectura_) {
        ultBillInPulsos_  = billIn;
        ultCashOutPulsos_ = cashOut;
        primeraLectura_   = false;
        return;
    }

    int32_t       dIn  = billIn  - ultBillInPulsos_;
    const int32_t dOut = cashOut - ultCashOutPulsos_;
    ultBillInPulsos_  = billIn;
    ultCashOutPulsos_ = cashOut;

    if (dIn < 0 || dOut < 0) return;      // hubo RAM clear, solo re-referencio

    // Los creditos que set_CurrentCredit() ya puso en el saldo no se suman otra
    // vez cuando el contador del billetero sube.  Si el contador no los alcanza en
    // VENTANA_SET_BILLETE_MS, esa cuenta se descarta.
    if (pendienteSetBill_ > 0) {
        if ((ahoraMs() - msPendienteSetBill_) > VENTANA_SET_BILLETE_MS) {
            pendienteSetBill_ = 0;
        } else if (dIn > 0) {
            const uint32_t absorbidos = ((uint32_t)dIn < pendienteSetBill_) ? (uint32_t)dIn : pendienteSetBill_;
            pendienteSetBill_ -= absorbidos;
            dIn -= (int32_t)absorbidos;
        }
    }

    const int32_t saldo = (int32_t)creditoActual_ + dIn - dOut;
    creditoActual_ = (saldo > 0) ? (uint32_t)saldo : 0;
}


// ---------------------------------------------------------------------------
//  DE CODIGO DE MEDIDOR SAS A VARIABLE DE LA PLACA
//
//  Esta funcion es la tabla de medidores: aqui se decide que numero contesta
//  la placa para cada codigo que pida el host.
//
//  Cada medidor sale del contador vinculado en vincularContadores() -si hay
//  uno- mas el ajuste que hayan metido los setters.  Los contadores que se
//  vinculen tienen que ser PROGRESIVOS (nunca se reinician), porque un
//  medidor SAS no puede ir para atras.
//
//  MAPEO ACORDADO EN LA REUNION TECNICA:
//
//     0x02  BET  - apostado          <- pulso BET      + ajuste
//     0x04  CC   - creditos cancel.  <- pulso CASH OUT + ajuste hand pay
//     0x0B  BI   - billetero         <- pulso CASH IN  + ajuste
//     0x23  HP   - hand paid         <- va a la par del 0x04
//
//  El 0x23 sale del mismo numero que el 0x04 a proposito: en una maquina de
//  ruta el cash out no lo paga una tolva, lo paga una persona, asi que ese
//  mismo evento es a la vez un credito cancelado y un credito pagado a mano.
//  Como salen de la misma fuente, no se pueden desfasar.
//
//  Los medidores que esta placa no tiene de donde sacar se contestan en CERO
//  y no se omiten: una maquina de ruta sin contador de jackpots tiene, de
//  verdad, cero jackpots, y asi el host la ve completa.
//
//  Devuelve true si el medidor tiene una fuente real detras.  El que
//  devuelve false igual se contesta, pero en cero.
// ---------------------------------------------------------------------------
bool SasEgm::obtenerValorMedidorRuta(uint8_t codigo, uint64_t* salida) {
    if (salida == nullptr) return false;
    *salida = 0;

    // creditosPorPulso: creditos que vale cada pulso (multiplica los contadores).
    const uint64_t creditosPorPulso = (uint64_t)SAS_CREDITOS_POR_PULSO;

    switch (static_cast<MedidorSas>(codigo)) {
        // ------------------------------------------------------------------
        // 0x00 Total coin in credits.  ESTE ES EL BET.
        //
        // En SAS no existe un medidor que se llame "bet": lo apostado se
        // reporta como "coin in", que es el nombre que quedo de cuando las
        // maquinas tragaban monedas.  Tabla C-7 del manual 6.02.
        //
        // El 0x02 NO es la apuesta: es "Total jackpot credits" (tabla C-7).
        // ------------------------------------------------------------------
        case MedidorSas::COIN_IN_APOSTADO:
            *salida = (leerContador(pApostado_) + ajusteBet_) * creditosPorPulso;
            return true;

        // 0x0C Current credits.  Es el mismo saldo del long poll 0x1A.  Un
        // host que lo pide por 0x2F recibe el saldo real, no un cero.
        case MedidorSas::CREDITOS_ACTUALES:
            *salida = (uint64_t)creditoActual_ * creditosPorPulso;
            return true;

        // 0x0B credits from bill accepted.  El cash in de esta placa entra
        // por billetero.
        case MedidorSas::CREDITOS_BILLETE:
            *salida = (leerContador(pBillIn_) + ajusteBillIn_) * creditosPorPulso;
            return true;

        // 0x04 Total cancelled credits y 0x23 Total hand paid credits.
        //
        // Los dos contestan el mismo numero porque asi lo pedi: lo que sale
        // por el CC-04 se tiene que ver a la par en el 23.
        //
        // PENDIENTE DE REVISAR CON EL MANUAL.  La tabla C-7 define el 0x23
        // como "hand paid cancelled credits + jackpot", o sea 0x03 + 0x02, y
        // no como una copia del 0x04.  Con el jackpot en cero, el 0x23
        // deberia seguir al 0x03 -el pulso de HAND PAY- y no al hopper.
        // Se deja asi porque asi se acordo y se prueba; antes de campo hay
        // que decidirlo.
        case MedidorSas::CREDITOS_CANCELADOS:
        case MedidorSas::HANDPAY_PAGADOS:
            *salida = (leerContador(pCashOut_) + ajusteHandpay_) * creditosPorPulso;
            return true;

        // 0x18 Total electronic transfers to host: los retiros por AFT.  Es
        // la casilla que el host pinta como AFT_OUT.
        case MedidorSas::TRANSFERENCIAS_AFT_HOST:
            *salida = (uint64_t)ajusteAftOut_ * creditosPorPulso;
            return true;

        // 0x03 hand paid cancelled credits.  Sale del pulso de HAND PAY
        // propio de la placa, que es otra entrada distinta al cash out.
        case MedidorSas::HANDPAY_CANCELADOS:
            *salida = leerContador(pHandpay_) * creditosPorPulso;
            return true;

        // Sin fuente en esta placa: se contesta cero.  Aqui caen, entre
        // otros, el 0x02 -jackpot: esta maquina no paga premios-, el 0x01
        // -coin out-, el 0x17 y el 0x18 -transferencias AFT-, el 0x24
        // -drop- y el 0x25/0x26 -partidas-.  Todos son cero de verdad o
        // sencillamente no los podemos medir.
        //
        // El pulso de REFILL todavia no tiene medidor asignado.
        default:
            return false;
    }
}

// Atajo del de arriba.  Devuelve el valor y ya; el medidor sin fuente sale
// en cero.  Esta para que los long polls de medidor suelto -0x10, 0x11,
// 0x46- saquen el dato del MISMO lugar que el 0x2F y no puedan contradecirlo.
uint64_t SasEgm::medidorSas(MedidorSas medidor) {
    uint64_t valor = 0;
    obtenerValorMedidorRuta(static_cast<uint8_t>(medidor), &valor);
    return valor;
}

// ---------------------------------------------------------------------------
//  CUANTOS BYTES BCD OCUPA CADA MEDIDOR EN EL 0x2F
//
//  No todos son de 4 bytes.  La tabla C-7 dice que el 0x2F reporta cada
//  medidor con el "Min Size" de la tabla, y hay medidores de 5:
//
//     0x0D..0x10   tickets SAS en centavos
//     0x80..0xBE   tickets y transferencias AFT en centavos
//
//  Todo lo demas es de 4.
//
//  POR QUE IMPORTA AUNQUE ESTA PLACA NO TENGA TICKETS NI AFT:
//  el 0x2F no lleva separadores.  Si el host pide un medidor de 5 y la
//  placa responde 4, todo lo que sigue se corre un byte y el host lee basura.
// ---------------------------------------------------------------------------
uint8_t SasEgm::tamanoMedidorSas(uint8_t codigo) {
    if (codigo >= MEDIDOR_TICKETS_MIN && codigo <= MEDIDOR_TICKETS_MAX) return BCD_TAMANO_LARGO;
    if (codigo >= MEDIDOR_AFT_MIN     && codigo <= MEDIDOR_AFT_MAX)     return BCD_TAMANO_LARGO;
    return BCD_TAMANO_CORTO;
}

// ---------------------------------------------------------------------------
//  LONG POLL 0x2F  -  medidores selectos del juego N
//
//  Peticion:  01 2F <len> <juegoH> <juegoL> <cod>...<cod> <CRClo> <CRChi>
//  Respuesta: 01 2F <len> <juegoH> <juegoL> <cod><4 BCD>... <CRClo> <CRChi>
//
//  <len> cuenta lo que va DESPUES del propio byte de largo y ANTES del CRC.
// ---------------------------------------------------------------------------
bool SasEgm::procesarComandoContadoresDetallados(const uint8_t* trama,
                                                       uint16_t largoTrama) {
    if (trama == nullptr || largoTrama < 7) return false;

    const uint8_t lenPeticion = trama[2];

    // Minimo 2 (solo el numero de juego).  El tope lo pone nuestro buffer de
    // salida, no un numero magico.
    if (lenPeticion < 3) return false;
    const uint8_t nMedidores = (uint8_t)(lenPeticion - 2);
    if (nMedidores > SAS_MAX_MEDIDORES) return false;

    const uint16_t largoTotal = (uint16_t)(3 + lenPeticion + 2);
    if (largoTrama < largoTotal) return false;

    // CRC sobre todo menos los dos bytes del propio CRC.  Byte bajo primero.
    const uint16_t crcCalculado = calcularCrc16Kermit(trama, (uint16_t)(largoTotal - 2));
    const uint16_t crcTrama     = (uint16_t)(trama[largoTotal - 2] |
                                            (trama[largoTotal - 1] << 8));
    if (crcCalculado != crcTrama) { sasCrcMalos_++; return false; }

    temporizadorUltimoSas_ = ahoraMs();

    const uint8_t  gameNumH = trama[3];
    const uint8_t  gameNumL = trama[4];
    const uint8_t* codigos  = &trama[5];

    // Lo mas grande que puede salir de aqui: 3 de cabecera + 0x3E de datos
    // + 2 de CRC = 69 bytes.  Con 128 sobra.
    // respuesta: la trama que se arma y se manda al final.
    // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
    uint8_t  respuesta[128] = {};
    uint16_t len = 0;

    respuesta[len++] = direccionSas_;
    respuesta[len++] = static_cast<uint8_t>(ComandoSas::MEDIDORES_SELECTOS);
    const uint16_t posLen = len;
    respuesta[len++] = 0x00;            // se rellena al final
    respuesta[len++] = gameNumH;
    respuesta[len++] = gameNumL;

    // SE CONTESTAN TODOS LOS QUE PIDAN, sin excepcion.
    //
    // La respuesta espeja la peticion: mismo numero de medidores, en el mismo
    // orden.  Un host que indexa por posicion se desincroniza si falta alguno.
    //
    // El medidor que la placa no tiene va en cero, que es el dato correcto
    // (una maquina de ruta sin jackpots tiene cero jackpots).
    for (uint8_t i = 0; i < nMedidores; i++) {
        const uint8_t codigo = codigos[i];

        uint64_t valor = 0;
        obtenerValorMedidorRuta(codigo, &valor);   // sin fuente -> queda en 0

        // Cada medidor con SU tamano, el que dice la tabla C-7.
        const uint8_t tamanoBcd = tamanoMedidorSas(codigo);

        // Dos topes, y los dos tienen que cumplirse:
        //
        //   1. el byte de largo del 0x2F no puede pasar de 0x3E (tabla 7.3b).
        //      Ese byte cuenta el numero de juego mas todos los pares, sin
        //      el CRC.  Si me paso, el byte se desborda y el host lee una
        //      trama de otro tamano del que de verdad mande.
        //   2. el buffer de salida.
        //
        // Si un host pide mas de lo que cabe, contesto lo que alcance en vez
        // de no contestar nada.  El manual lo permite y una respuesta corta
        // le sirve mas al host que el silencio.
        const uint16_t largoSiLoAgrego = (uint16_t)((len - 3) + 1 + tamanoBcd);
        if (largoSiLoAgrego > SAS_2F_LARGO_MAX) break;
        if ((uint16_t)(len + 1 + tamanoBcd + 2) > sizeof(respuesta)) break;

        respuesta[len++] = codigo;
        bcdEncodeSas(&respuesta[len], valor, tamanoBcd);
        len = (uint16_t)(len + tamanoBcd);
    }

    respuesta[posLen] = (uint8_t)(len - 3);

    const uint16_t crcRespuesta = calcularCrc16Kermit(respuesta, len);
    respuesta[len++] = (uint8_t)(crcRespuesta & 0xFF);
    respuesta[len++] = (uint8_t)((crcRespuesta >> 8) & 0xFF);

    enviarTramaSas(respuesta, len);
    return true;
}

// ---------------------------------------------------------------------------
//  Copia una cadena ASCII con LARGO FIJO.
//
//  Si es mas corta rellena con '0', si es mas larga la recorta.  Los campos
//  del 0x1F son de largo fijo y la trama no lleva byte de longitud: un
//  caracter de mas o de menos le corre TODO al host y le cambia el paytable
//  y el porcentaje de pago por basura.
//
//  Se rellena con '0' y no con espacio porque asi lo pide la tabla 7.10 para
//  el ID adicional, y prefiero un solo criterio para los cuatro campos.
// ---------------------------------------------------------------------------
void SasEgm::copiarAsciiFijo(uint8_t* destino, const char* texto, uint8_t largo) {
    uint8_t i = 0;
    if (texto != nullptr) {
        for (; i < largo && texto[i] != '\0'; i++) destino[i] = (uint8_t)texto[i];
    }
    for (; i < largo; i++) destino[i] = (uint8_t)'0';
}

// ---------------------------------------------------------------------------
//  LONG POLL 0x1F  -  identidad de la maquina
//
//  Respuesta de 24 bytes, tabla 7.10 del SAS 6.02:
//
//    01 1F <ID 2 ASCII> <ID extra 3 ASCII> <denom 1> <apuesta max 1>
//          <grupo progresivo 1> <opciones 2> <paytable 6 ASCII>
//          <porcentaje 4 ASCII> <CRC 2>
//
//  NO lleva byte de longitud: es de largo fijo y el host cuenta posiciones.
//
//  Tres de estos campos -denominacion, apuesta maxima y porcentaje- los
//  define el casino o la jurisdiccion, no yo.  Van en cero hasta que me
//  pasen los buenos: prefiero que se note que faltan a inventar un numero
//  que el host se crea y que despues no cuadre con el alta de la maquina.
// ---------------------------------------------------------------------------
void SasEgm::responderIdentidadMaquina() {
    // respuesta: la trama que se arma y se manda al final.
    // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
    uint8_t  respuesta[24] = {};
    uint16_t len = 0;

    respuesta[len++] = direccionSas_;
    respuesta[len++] = static_cast<uint8_t>(ComandoSas::IDENTIDAD_MAQUINA);

    copiarAsciiFijo(&respuesta[len], ID_JUEGO_ASCII,       2);  len += 2;
    copiarAsciiFijo(&respuesta[len], ID_JUEGO_EXTRA_ASCII, 3);  len += 3;

    respuesta[len++] = CODIGO_DENOMINACION;
    respuesta[len++] = APUESTA_MAXIMA;
    respuesta[len++] = GRUPO_PROGRESIVO;
    respuesta[len++] = OPCIONES_JUEGO_HI;
    respuesta[len++] = OPCIONES_JUEGO_LO;

    copiarAsciiFijo(&respuesta[len], ID_PAYTABLE_ASCII,     6);  len += 6;
    copiarAsciiFijo(&respuesta[len], PORCENTAJE_PAGO_ASCII, 4);  len += 4;

    const uint16_t crc = calcularCrc16Kermit(respuesta, len);
    respuesta[len++] = (uint8_t)(crc & 0xFF);
    respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);
    enviarTramaSas(respuesta, len);
}

// ---------------------------------------------------------------------------
//  LONG POLL 0x1B  -  informacion del hand pay
//
//  Respuesta de 24 bytes, tabla 7.8 del SAS 6.02:
//
//    01 1B <grupo 1> <nivel 1> <monto 5 BCD> <pago parcial 2 BCD>
//          <reset ID 1> <10 bytes sin usar> <CRC 2>
//
//  Nivel = 0x80, que en la tabla es "cancelled credits amount".  Es lo
//  correcto para esta placa: aqui no hay premios ni progresivos, el unico
//  hand pay que existe es el cash out del saldo, o sea creditos cancelados.
//
//  El monto va en unidades de la denominacion SAS -creditos-, NO en centavos.
//  Eso lo dice la tabla: solo cuando el hand pay trae parte de un progresivo
//  el monto se manda en centavos, y aqui nunca es el caso.
//
//  Si no hay hand pay trabado sale todo en ceros.  Asi lo pide la seccion
//  7.8.2: "long poll 1B returns all zeros whenever the gaming machine is not
//  currently in a handpay lockup".
// ---------------------------------------------------------------------------
void SasEgm::responderHandpayInfo() {
    // respuesta: la trama que se arma y se manda al final.  De largo fijo
    // (24 bytes), por eso aqui no hace falta un indice 'len': se escribe por
    // posicion directa.
    uint8_t  respuesta[24] = {};

    respuesta[0] = direccionSas_;
    respuesta[1] = static_cast<uint8_t>(ComandoSas::INFO_HANDPAY);

    if (handpayPendiente_) {
        respuesta[2] = 0x00;   // grupo progresivo: ninguno
        respuesta[3] = 0x80;   // nivel: creditos cancelados

        // Monto en 5 BCD.  respuesta[9] y respuesta[10] -pago parcial-
        // quedan en cero: aqui no se paga nada por adelantado, el cash out
        // sale completo.  respuesta[11] -reset ID- en cero: solo hay reset
        // estandar, esta placa no sabe devolver el hand pay al medidor de
        // creditos.
        //
        // La unidad la decide HANDPAY_1B_EN_CENTAVOS.  Ver la nota larga en
        // el .h: el manual pide creditos, pero mi host divide este campo
        // entre 100 y por eso diez dolares salian como $0,10.
        uint64_t monto = (uint64_t)handpayMonto_ * (uint64_t)SAS_CREDITOS_POR_PULSO;
        if (HANDPAY_1B_EN_CENTAVOS) monto *= (uint64_t)CENTAVOS_POR_CREDITO;

        bcdEncodeSas(&respuesta[4], monto, 5);
    }

    const uint16_t crc = calcularCrc16Kermit(respuesta, 22);
    respuesta[22] = (uint8_t)(crc & 0xFF);
    respuesta[23] = (uint8_t)((crc >> 8) & 0xFF);
    enviarTramaSas(respuesta, 24);

    // -------------------------------------------------------------------
    //  REPORTADO = FUERA DE LA COLA
    //
    //  Esta placa NO se traba esperando al tecnico: pulsa el hopper, paga y
    //  sigue.  Asi que en cuanto el host pregunta por el pago, el pago ya
    //  esta hecho y la ficha se cierra aqui mismo.
    //
    //  Sin esto el monto se quedaba pendiente para siempre, el siguiente
    //  cash out le sumaba encima, y el 0x1B iba reportando 10, 20, 30, 40.
    //
    //  Y se avisa con la excepcion 0x52 -hand pay reseteado-, que es lo que
    //  pide la 7.8.1 una vez que el 0x1B se contesto y el pago se resolvio.
    //  Asi el host cierra su lado en vez de quedarse esperando un 0x94 que
    //  en esta placa nunca va a hacer falta.
    //
    //  Riesgo asumido: si la respuesta se pierde en el aire, el host vuelve
    //  a pedir el 0x1B y recibe ceros.  Prefiero eso a un monto que crece
    //  solo, que es dinero mal reportado.
    // -------------------------------------------------------------------
    if (handpayPendiente_) {
        handpayPendiente_ = false;
        handpayMonto_     = 0;
        encolarExcepcionSas(ExcepcionSas::HANDPAY_RESETEADO);
    }
}

// ---------------------------------------------------------------------------
//  DESPACHADOR DE LONG POLLS
// ---------------------------------------------------------------------------
void SasEgm::ProcesarComandoSAS(const uint8_t* trama, uint16_t largoTrama) {
    if (largoTrama < 2)              return;
    if (trama[0] != direccionSas_)   return;   // es para otra maquina del bus

    sasTramasAtendidas_++;

    const uint8_t comando = trama[1];
    // creditosPorPulso: creditos que vale cada pulso (multiplica los contadores).
    const uint64_t creditosPorPulso = (uint64_t)SAS_CREDITOS_POR_PULSO;

    switch (static_cast<ComandoSas>(comando)) {
        case ComandoSas::MEDIDORES_SELECTOS:
            procesarComandoContadoresDetallados(trama, largoTrama);
            break;

        // 0x1A = creditos actuales.  Lo que el jugador tiene en pantalla
        // ahora mismo, no un acumulado.  Lo lleva actualizarCreditoPorPulsos()
        // y lo mueven los setters set_CurrentCredit() y set_Handpay().
        case ComandoSas::CREDITOS_ACTUALES:
            responderContadorBCD(ComandoSas::CREDITOS_ACTUALES, (uint64_t)creditoActual_ * creditosPorPulso);
            break;

        // ------------------------------------------------------------------
        //  0x54 = que version de SAS hablo y cual es mi numero de serie.
        //
        //  Respuesta:  01 54 <largo> <3 ASCII version> <serie ASCII> <CRC16>
        //  El <largo> cuenta version + serie, sin contar el CRC.
        //
        //  Los dos campos van en ASCII, NO en BCD.  Es de los pocos long
        //  polls que mandan texto, asi que no se puede usar el codificador
        //  BCD de los medidores.
        // ------------------------------------------------------------------
        case ComandoSas::VERSION_Y_SERIE: {
            // respuesta: la trama que se arma y se manda al final.
            // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
            uint8_t  respuesta[64] = {};
            uint16_t len = 0;

            respuesta[len++] = direccionSas_;
            respuesta[len++] = static_cast<uint8_t>(ComandoSas::VERSION_Y_SERIE);
            const uint16_t posLen = len;
            respuesta[len++] = 0x00;                       // se rellena al final

            for (const char* caracter = SAS_VERSION_ASCII; *caracter && len < 60; ++caracter) respuesta[len++] = (uint8_t)*caracter;
            for (const char* caracter = SERIE_MAQUINA;     *caracter && len < 60; ++caracter) respuesta[len++] = (uint8_t)*caracter;

            respuesta[posLen] = (uint8_t)(len - 3);

            const uint16_t crc = calcularCrc16Kermit(respuesta, len);
            respuesta[len++] = (uint8_t)(crc & 0xFF);
            respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);
            enviarTramaSas(respuesta, len);
            break;
        }

        // ------------------------------------------------------------------
        //  0xA0 = que funciones tengo habilitadas.
        //
        //  Peticion:   01 A0 <juegoH> <juegoL> <CRC16>
        //  Respuesta:  01 A0 <juegoH> <juegoL> <3 de features> <3 reservados> <CRC16>
        //
        //  Esta trama NO lleva byte de largo: es de tamano fijo, 12 bytes.
        //
        //  Los tres bytes de features van en CERO: son banderas de cosas que
        //  esta placa no hace -AFT, bonos, torneos, validacion de tickets-.
        //  Prender un bit aqui es prometerle al host algo que despues va a
        //  pedir y no vamos a saber contestar.
        // ------------------------------------------------------------------
        case ComandoSas::FUNCIONES_HABILITADAS: {
            // respuesta: la trama que se arma y se manda al final.
            // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
            uint8_t  respuesta[16] = {};
            uint16_t len = 0;

            respuesta[len++] = direccionSas_;
            respuesta[len++] = static_cast<uint8_t>(ComandoSas::FUNCIONES_HABILITADAS);

            // Devuelve el mismo numero de juego que pide el host.  Si la
            // trama es corta, contesta el juego 0000 (la maquina completa).
            respuesta[len++] = (largoTrama >= 4) ? trama[2] : 0x00;
            respuesta[len++] = (largoTrama >= 4) ? trama[3] : 0x00;

            respuesta[len++] = 0x00; respuesta[len++] = 0x00; respuesta[len++] = 0x00;   // features
            respuesta[len++] = 0x00; respuesta[len++] = 0x00; respuesta[len++] = 0x00;   // reservados

            const uint16_t crc = calcularCrc16Kermit(respuesta, len);
            respuesta[len++] = (uint8_t)(crc & 0xFF);
            respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);
            enviarTramaSas(respuesta, len);
            break;
        }

        // ------------------------------------------------------------------
        //  MEDIDORES SUELTOS  (long polls 0x10 a 0x18)
        //
        //  Todos salen de obtenerValorMedidorRuta(), el MISMO puente que usa
        //  el 0x2F.  Un solo camino para cada dato: el host recibe el mismo
        //  numero sin importar por donde pregunte.
        //
        //  Ojo con los codigos: el numero del LONG POLL y el numero del
        //  MEDIDOR son dos numeraciones distintas.  El long poll 0x11 pide el
        //  medidor 0x00, no el 0x11.
        // ------------------------------------------------------------------

        // 0x10 total cancelled credits -> medidor 0x04.  Es el cash out.
        case ComandoSas::TOTAL_CANCELADOS: responderContadorBCD(ComandoSas::TOTAL_CANCELADOS, medidorSas(MedidorSas::CREDITOS_CANCELADOS)); break;

        // 0x11 total coin in -> medidor 0x00.  En SAS "coin in" es el total
        // apostado, o sea el BET.
        case ComandoSas::TOTAL_COIN_IN: responderContadorBCD(ComandoSas::TOTAL_COIN_IN, medidorSas(MedidorSas::COIN_IN_APOSTADO)); break;

        // 0x12 total coin out: CERO a proposito.
        //
        // "Coin out" es lo que la maquina paga por premios.  El cash out ya
        // se reporta como creditos cancelados (0x10); repetirlo aqui lo
        // contaria dos veces.  La placa no tiene premios pagados, asi que va
        // en cero, igual que en el 0x2F.
        case ComandoSas::TOTAL_COIN_OUT: responderContadorBCD(ComandoSas::TOTAL_COIN_OUT, 0); break;

        // 0x13 total drop: cero.  No hay caja de caida en esta placa.
        // PENDIENTE: si la jurisdiccion cuenta los billetes del stacker como
        // drop, esto hay que revisarlo, porque entonces no es cero.
        case ComandoSas::TOTAL_DROP: responderContadorBCD(ComandoSas::TOTAL_DROP, 0); break;

        // 0x14 total jackpot: cero de verdad.  Aqui no hay premios ni
        // progresivos; el unico dinero que sale es el cash out, y ese ya va
        // por el 0x10 y el 0x23.
        case ComandoSas::TOTAL_JACKPOT: responderContadorBCD(ComandoSas::TOTAL_JACKPOT, 0); break;

        // Partidas jugadas / ganadas / perdidas.  La placa no tiene pulso de
        // los que salgan, asi que van en cero.  Verificado en la maquina
        // real: 0x15 = jugadas, 0x16 = ganadas, 0x17 = perdidas, y ademas
        // 0x17 = 0x15 - 0x16 clavado (3594 - 776 = 2818).
        case ComandoSas::PARTIDAS_JUGADAS: responderContadorBCD(ComandoSas::PARTIDAS_JUGADAS, 0); break;
        case ComandoSas::PARTIDAS_GANADAS: responderContadorBCD(ComandoSas::PARTIDAS_GANADAS, 0); break;
        case ComandoSas::PARTIDAS_PERDIDAS: responderContadorBCD(ComandoSas::PARTIDAS_PERDIDAS, 0); break;

        // 0x18 partidas desde el ultimo encendido y desde que se cerro la
        // puerta.  Son DOS medidores de 2 BCD, no uno de 4, pero como los dos
        // van en cero la trama sale igual de 8 bytes y el host la lee bien.
        case ComandoSas::PARTIDAS_DESDE_ENCENDIDO: responderContadorBCD(ComandoSas::PARTIDAS_DESDE_ENCENDIDO, 0); break;

        // ------------------------------------------------------------------
        //  BLOQUES DE VARIOS MEDIDORES  (0x0F y 0x19)
        //
        //  Son los mismos medidores de arriba pero en una sola trama.  El
        //  orden lo fija la tabla 7.2a/7.2b y NO se puede cambiar: el host
        //  cuenta posiciones, no lee codigos.
        //
        //  Salen del mismo puente que los sueltos, asi que no hay manera de
        //  que el 0x0F y el 0x11 se contradigan.
        // ------------------------------------------------------------------

        // 0x0F: cancelados, coin in, coin out, drop, jackpot, jugadas.
        case ComandoSas::BLOQUE_DESDE_CANCELADOS: {
            const uint64_t medidores[6] = {
                medidorSas(MedidorSas::CREDITOS_CANCELADOS),   // total cancelled credits
                medidorSas(MedidorSas::COIN_IN_APOSTADO),   // total coin in  (= BET)
                0,                  // total coin out  - sin fuente
                0,                  // total drop      - no hay
                0,                  // total jackpot   - no hay
                0                   // games played    - sin fuente
            };
            responderBloqueMedidores(ComandoSas::BLOQUE_DESDE_CANCELADOS, medidores, 6);
            break;
        }

        // 0x19: lo mismo sin los cancelados.
        case ComandoSas::BLOQUE_DESDE_COIN_IN: {
            const uint64_t medidores[5] = {
                medidorSas(MedidorSas::COIN_IN_APOSTADO),   // total coin in  (= BET)
                0,                  // total coin out
                0,                  // total drop
                0,                  // total jackpot
                0                   // games played
            };
            responderBloqueMedidores(ComandoSas::BLOQUE_DESDE_COIN_IN, medidores, 5);
            break;
        }

        // ------------------------------------------------------------------
        //  0x46 = creditos de todos los billetes aceptados
        //
        //  Este SI lo tenemos de verdad y con la unidad correcta: la tabla
        //  B-1 lo pide "in SAS accounting denom units", o sea en creditos,
        //  que es exactamente lo que cuenta el billetero de esta placa.
        //  Es el mismo dato del medidor 0x0B.
        // ------------------------------------------------------------------
        case ComandoSas::CREDITOS_BILLETES: responderContadorBCD(ComandoSas::CREDITOS_BILLETES, medidorSas(MedidorSas::CREDITOS_BILLETE)); break;

        // ------------------------------------------------------------------
        //  0x20 = valor en DOLARES de todos los billetes aceptados
        //
        //  Este lo tuve callado mientras no sabia la denominacion, porque
        //  sin ella no hay forma de convertir creditos a dinero.  Ya la se:
        //  un credito es un dolar, asi que el valor en dolares es el MISMO
        //  numero que el 0x46 y no hay nada que multiplicar.
        //
        //  Ojo si algun dia la placa cambia de denominacion: aqui hay que
        //  multiplicar por los dolares que valga un credito.  Hoy es x1 y
        //  por eso no se ve la multiplicacion.
        // ------------------------------------------------------------------
        case ComandoSas::DOLARES_BILLETES: responderContadorBCD(ComandoSas::DOLARES_BILLETES, medidorSas(MedidorSas::CREDITOS_BILLETE)); break;

        // 0x1B: ficha del hand pay trabado.  Si no hay ninguno sale en ceros.
        case ComandoSas::INFO_HANDPAY:
            responderHandpayInfo();
            break;

        // 0x1F: identidad de la maquina.
        case ComandoSas::IDENTIDAD_MAQUINA:
            responderIdentidadMaquina();
            break;

        // ------------------------------------------------------------------
        //  0x01 a 0x09 = habilitar / deshabilitar (SAS 6.02 seccion 7.4)
        //
        //  Tipo S: llegan con CRC (ya validado arriba).  Se guarda la orden y se
        //  contesta ACK (la propia direccion).  El firmware lee las banderas con
        //  juegoBloqueadoPorHost() y demas.  El 0x09 contesta NACK -direccion con
        //  el bit 7- si el juego pedido no existe.
        // ------------------------------------------------------------------
        case ComandoSas::APAGAR_JUEGO:
            if (COMANDOS_HABILITAR_ACTIVO) { apagadoPorHost_ = true;  responderAckSimple(); }
            break;
        case ComandoSas::ENCENDER_JUEGO:
            if (COMANDOS_HABILITAR_ACTIVO) { apagadoPorHost_ = false; responderAckSimple(); }
            break;
        case ComandoSas::SONIDO_APAGAR:
        case ComandoSas::SONIDO_JUEGO_APAGAR:
            if (COMANDOS_HABILITAR_ACTIVO) { sonidoApagado_ = true;  responderAckSimple(); }
            break;
        case ComandoSas::SONIDO_ENCENDER:
            if (COMANDOS_HABILITAR_ACTIVO) { sonidoApagado_ = false; responderAckSimple(); }
            break;
        case ComandoSas::BILLETERO_HABILITAR:
            if (COMANDOS_HABILITAR_ACTIVO) { billeteroDeshabilitado_ = false; responderAckSimple(); }
            break;
        case ComandoSas::BILLETERO_DESHABILITAR:
            if (COMANDOS_HABILITAR_ACTIVO) { billeteroDeshabilitado_ = true;  responderAckSimple(); }
            break;

        // 0x08: cuatro bytes con una denominacion por bit, el menos significativo
        // primero (tabla 7.5), y una bandera.
        case ComandoSas::BILLETERO_CONFIGURAR:
            if (COMANDOS_HABILITAR_ACTIVO && largoTrama >= 9) {
                billeteroMascara_ = (uint32_t)trama[2] | ((uint32_t)trama[3] << 8) |
                                    ((uint32_t)trama[4] << 16) | ((uint32_t)trama[5] << 24);
                responderAckSimple();
            }
            break;

        // 0x09: juego N (2 bytes BCD) y 00 deshabilitar / 01 habilitar.  Esta placa
        // tiene un solo juego (NUMERO_ACTIVO); el 0000 vale para todos.
        case ComandoSas::JUEGO_N_HABILITAR:
            if (COMANDOS_HABILITAR_ACTIVO && largoTrama >= 7) {
                const uint32_t juego = (uint32_t)(((trama[2] >> 4) * 10 + (trama[2] & 0x0F)) * 100 +
                                                  ((trama[3] >> 4) * 10 + (trama[3] & 0x0F)));
                if (juego == 0 || juego == NUMERO_ACTIVO) {
                    juegoDeshabilitado_ = (trama[4] == 0x00);
                    responderAckSimple();
                } else {
                    const uint8_t nack = (uint8_t)(direccionSas_ | BIT_POLEO_GENERAL);
                    enviarTramaSas(&nack, 1);
                }
            }
            break;

        // ------------------------------------------------------------------
        //  0x0A / 0x0B = entrar y salir de modo mantenimiento
        //
        //  Se contestan con un ACK de un byte.  Es honesto: el modo
        //  mantenimiento solo sirve para que no suene la alarma de puerta
        //  abierta mientras el tecnico trabaja, y esta placa no tiene alarma
        //  de puerta, o sea que no hay nada que hacer y nada que prometer.
        //  La bandera se guarda solo para verla en el debugger.
        // ------------------------------------------------------------------
        case ComandoSas::MANTENIMIENTO_ENTRAR:
            modoMantenimiento_ = true;
            responderAckSimple();
            break;
        case ComandoSas::MANTENIMIENTO_SALIR:
            modoMantenimiento_ = false;
            responderAckSimple();
            break;

        // ------------------------------------------------------------------
        //  0x94 = reset remoto del hand pay
        //
        //  El host nos dice que ya pago y que soltemos el bloqueo.  Se borra
        //  la ficha del 0x1B y se avisa con la excepcion 0x52.
        //
        //  LIMITACION CONOCIDA: la trama trae un byte de codigo -00 reset
        //  normal, 01 devolver el hand pay al medidor de creditos- que aqui
        //  no llega a leerse, porque el ensamblador cierra estos long polls
        //  en direccion + comando.  Cualquier 0x94 se trata como reset
        //  normal.  Para esta placa da igual: no sabemos devolver creditos.
        // ------------------------------------------------------------------
        case ComandoSas::RESET_HANDPAY:
            handpayPendiente_ = false;
            handpayMonto_     = 0;
            encolarExcepcionSas(ExcepcionSas::HANDPAY_RESETEADO);
            responderAckSimple();
            break;

        // ------------------------------------------------------------------
        //  LO QUE NO SE CONTESTA A PROPOSITO
        //
        //  En SAS callar es una respuesta valida; inventar una trama no.  La
        //  tabla 7.1b lo dice claro: un medidor que uno SABE que vale cero se
        //  contesta con cero, pero un medidor que uno simplemente no puede
        //  medir se tiene que IGNORAR.  Contestar cero ahi es afirmar algo
        //  que no nos consta.
        //
        //    0x1E  billetes por denominacion.  Este sigue callado aunque ya
        //          sepa la denominacion de la MAQUINA, porque son dos cosas
        //          distintas: yo se que un credito vale un dolar, pero no se
        //          si los veinte creditos que acaban de entrar fueron un
        //          billete de veinte o veinte billetes de uno.  La placa
        //          cuenta pulsos, no billetes.
        //
        //          Contestar seis ceros mientras el 0x0B trae miles de
        //          creditos es contradecirse solo, y en una auditoria eso se
        //          lee como maquina manipulada.
        //
        //    0x2D  cancelados por numero de juego.  Habria que leer el juego
        //          que viene en la peticion, y el ensamblador cierra este
        //          long poll antes de esos bytes.
        //
        //    0x01..0x09  apagar/encender el juego, el sonido y el billetero.
        //          Estos SI se contestan (mas abajo), pero solo si
        //          COMANDOS_HABILITAR_ACTIVO esta en true.  Esta placa LEE
        //          pulsos: el ACK le dice al host "ya lo hice", asi que el
        //          firmware tiene que leer juegoBloqueadoPorHost() y demas y
        //          apagar de verdad lo que corresponda.  Si no puede, esa
        //          bandera va en false y se callan, que es peor que mentir.
        //
        //    0x73  registro de AFT.  El host no lo usa: manda la llave de
        //          registro en ceros dentro del 0x72, que significa "no hace
        //          falta registrarse".
        // ------------------------------------------------------------------

        // ------------------------------------------------------------------
        //  AFT.  Aqui empieza el cash out remoto de verdad.
        //
        //  El host hace el cash out remoto por AFT, por eso estos dos
        //  comandos se contestan.
        // ------------------------------------------------------------------
        case ComandoSas::AFT_BLOQUEO_Y_ESTADO:
            responderAftLockStatus(trama, largoTrama);
            break;

        case ComandoSas::AFT_TRANSFERENCIA:
            responderAftTransfer(trama, largoTrama);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
//  ARMADO DE TRAMAS
//
//  La serial entrega palabras de 9 bits.  Aqui se juntan en mensajes SAS
//  completos.  Esta parte SI es del protocolo -saber cuanto mide cada mensaje
//  depende del comando-, por eso no vive en la serial.
//
//  Corre en el lazo principal: la interrupcion solo llena la serial, y aqui se
//  vacia.  Cada palabra trae la marca de cuando LLEGO, asi que el tiempo de un
//  mensaje se cuenta desde su llegada aunque el lazo se haya atrasado en
//  leerlo.
// ---------------------------------------------------------------------------

void SasEgm::reiniciarEnsamblado() {
    ensambleLen_  = 0;
    ensambleTick_ = 0;
    colaHead_     = 0;
    colaTail_     = 0;

    statTramas_      = 0;
    statColaLlena_   = 0;
    statIncompletas_ = 0;
    statWakeup1_     = 0;
    statWakeup0_     = 0;
}

// Saca todo lo que haya en la serial y lo va armando.
void SasEgm::recibirPalabras() {
    // La serial entrega lo pendiente por lotes, cada palabra con su hora de
    // llegada (la que importa para la regla de respuesta fresca).
    // palabras: hasta LOTE_RX palabras de 9 bits que llegaron del host.
    // marcas:   la hora (ahoraMs()) en la que llego cada una de esas palabras,
    //           misma posicion que 'palabras'.
    uint16_t palabras[LOTE_RX] = {};
    uint32_t marcas[LOTE_RX]   = {};

    while (serial_->available9bit() > 0) {
        // palabrasLeidas: cuantas palabras entrego la serial en esta vuelta.
        const size_t palabrasLeidas = serial_->readBuffer9bit(reinterpret_cast<uint8_t*>(palabras),
                                             sizeof(palabras), marcas);
        if (palabrasLeidas == 0) break;

        for (size_t i = 0; i < palabrasLeidas; i++) {
            procesarPalabraRx(palabras[i], marcas[i]);
        }
    }

    // Mensaje a medias que no se completa (byte perdido, ruido): se tira
    // para que no se pegue con el siguiente.
    if (ensambleLen_ > 0 && (ahoraMs() - ensambleTick_) > TIMEOUT_TRAMA_MS) {
        ensambleLen_ = 0;
        statIncompletas_++;
    }
}

// ---------------------------------------------------------------------------
//  LARGO DEL MENSAJE SEGUN EL PROTOCOLO
//  Devuelve 0 mientras todavia no se pueda saber.  Si se pasa conCrc, dice si
//  ese comando termina con CRC de 2 bytes.
// ---------------------------------------------------------------------------
uint16_t SasEgm::largoEsperado(bool* conCrc) const {
    if (conCrc != nullptr) *conCrc = false;

    if (ensambleLen_ == 0) return 0;

    const uint8_t direccion = ensamble_[0];

    // General poll: la direccion trae el bit 7 puesto (0x80 | addr).
    // Es un mensaje de un solo byte, se puede despachar de inmediato.
    if (direccion & BIT_POLEO_GENERAL) return 1;

    // Long poll dirigido a otro EGM del bus: no nos interesa, pero hay que
    // cerrarlo igual para no pegarlo con el siguiente mensaje.
    // Aun asi necesitamos el comando para saber cuanto ocupa.
    if (ensambleLen_ < 2) return 0;

    switch (static_cast<ComandoSas>(ensamble_[1])) {
        // Comandos que traen byte de longitud:
        //   direccion + comando + len + (len datos) + 2 de CRC
        case ComandoSas::VALIDACION_NUMERO:
        case ComandoSas::MEDIDORES_SELECTOS:
        case ComandoSas::FECHA_HORA_FIJAR:
        case ComandoSas::FECHA_HORA_RECIBIR:
        case ComandoSas::AFT_TRANSFERENCIA:
        case ComandoSas::VALIDACION_ESTADO_EXTENDIDO:
        case ComandoSas::MEDIDORES_EXTENDIDOS:
        case ComandoSas::MULTIDENOM_PREAMBULO:
            if (ensambleLen_ < 3) return 0;
            if (conCrc != nullptr) *conCrc = true;
            return (uint16_t)(3 + ensamble_[2] + 2);

        // Comandos con datos de largo FIJO: no traen byte de longitud, asi que
        // hay que saberselos de memoria.
        //
        // Sin esta rama el 0xA0 se cerraba a los dos bytes y los cuatro que
        // siguen -numero de juego y CRC- se quedaban sueltos, y el armador
        // los tomaba como el arranque de otra trama.  No rompia nada porque la
        // direccion no cuadraba y se descartaban, pero la trama buena llegaba
        // mutilada y era imposible validarle el CRC.
        //
        // send enabled features: direccion + comando + numero de juego (2) + CRC (2)
        case ComandoSas::FUNCIONES_HABILITADAS:
            if (conCrc != nullptr) *conCrc = true;
            return 6;

        // AFT lock and status request, tabla 8.2a:
        //   direccion + comando + codigo de bloqueo + condicion +
        //   timeout (2 BCD) + CRC (2) = 8 bytes.
        //
        // SIN ESTO NO HAY AFT.  El armador cerraba el 0x74 a los dos bytes y
        // los seis que siguen se quedaban sueltos, asi que nunca llegaba a
        // leerse el codigo de bloqueo y era imposible saber si el host pedia
        // bloquear, desbloquear o solo preguntar.
        case ComandoSas::AFT_BLOQUEO_Y_ESTADO:
            if (conCrc != nullptr) *conCrc = true;
            return 8;

        // Habilitar / deshabilitar (tipo S, tablas 7.4a, 7.5 y 7.6.1):
        //   01 a 07   direccion + comando + CRC (2)                        = 4
        //   08        direccion + comando + 4 denominaciones + bandera + CRC = 9
        //   09        direccion + comando + juego (2 BCD) + 00/01 + CRC     = 7
        case ComandoSas::APAGAR_JUEGO:
        case ComandoSas::ENCENDER_JUEGO:
        case ComandoSas::SONIDO_APAGAR:
        case ComandoSas::SONIDO_ENCENDER:
        case ComandoSas::SONIDO_JUEGO_APAGAR:
        case ComandoSas::BILLETERO_HABILITAR:
        case ComandoSas::BILLETERO_DESHABILITAR:
            if (conCrc != nullptr) *conCrc = true;
            return 4;
        case ComandoSas::BILLETERO_CONFIGURAR:
            if (conCrc != nullptr) *conCrc = true;
            return 9;
        case ComandoSas::JUEGO_N_HABILITAR:
            if (conCrc != nullptr) *conCrc = true;
            return 7;

        default:
            break;
    }

    // El resto de los long polls que usamos son direccion + comando.
    return 2;
}

// ---------------------------------------------------------------------------
//  UNA PALABRA RECIBIDA
//
//  marcaMs = el instante en que llego.
// ---------------------------------------------------------------------------
void SasEgm::procesarPalabraRx(uint16_t palabra, uint32_t marcaMs) {
    if (palabra & BIT_WAKEUP) { statWakeup1_++; msUltimoWakeup_ = marcaMs; } else statWakeup0_++;

    // Sin 9no bit no hay inicio de mensaje.  Si tampoco hay un mensaje abierto,
    // es un byte suelto (ruido, o el sobrante de una trama que se midio mal):
    // se ignora.  Sin esto, un 0x81 suelto se tomaba por poleo general y la
    // placa contestaba sin que nadie le preguntara.
    if (!(palabra & BIT_WAKEUP) && ensambleLen_ == 0) return;

    // El 9no bit marca inicio de mensaje. Si habia uno a medias, se perdio
    // algun byte: lo publicamos igual (el despachador lo descartara por
    // largo o por CRC) y arrancamos limpio.
    if (palabra & BIT_WAKEUP) {
        if (ensambleLen_ > 0) {
            statIncompletas_++;
            publicarTrama(false, marcaMs);      // a medias: no vale como acuse
        }
        ensambleLen_  = 0;
        ensambleTick_ = marcaMs;
    }

    if (ensambleLen_ < MAX_TRAMA) {
        ensamble_[ensambleLen_++] = static_cast<uint8_t>(palabra & MASCARA_DATO);
    }

    // Cierre estructural: en cuanto el mensaje esta completo segun SAS,
    // se publica al instante. No se espera silencio ni timeout.
    const uint16_t esperado = largoEsperado(nullptr);
    if (esperado > 0 && ensambleLen_ >= esperado) {
        publicarTrama(true, marcaMs);
    }
}

void SasEgm::publicarTrama(bool completa, uint32_t marcaMs) {
    if (ensambleLen_ == 0) return;

    // Se anota ahora, con el mensaje todavia en el ensamblador.
    bool lleva = false;
    largoEsperado(&lleva);

    const uint8_t siguiente = static_cast<uint8_t>((colaHead_ + 1) % COLA_TRAMAS);

    if (siguiente == colaTail_) {
        // Cola llena: el lazo principal no esta drenando a tiempo.
        // Se pierde la trama, pero no corrompemos las que ya estan en cola.
        statColaLlena_++;
    } else {
        memcpy(cola_[colaHead_], ensamble_, ensambleLen_);
        colaLen_[colaHead_]      = ensambleLen_;
        colaTick_[colaHead_]     = marcaMs;
        colaCompleta_[colaHead_] = completa;
        colaConCrc_[colaHead_]   = lleva;
        colaHead_ = siguiente;
        statTramas_++;
    }

    ensambleLen_ = 0;
}

// Saca la trama mas vieja de la cola.  Ademas del largo devuelve, si se piden,
// cuanto lleva esperando, si llego ENTERA segun el protocolo y si su comando
// lleva CRC.  Con eso atenderSas() decide si la trama vale como acuse de una
// excepcion.
uint16_t SasEgm::leerTramaDeCola(uint8_t* destino, uint16_t maxLen, uint32_t* edadMs,
                                 bool* completa, bool* conCrc) {
    if (destino == nullptr)     return 0;
    if (colaTail_ == colaHead_) return 0;   // no hay trama completa

    // largoRx: cuantos bytes tiene esta trama recibida (NO es un indice de
    // escritura como el 'len' de los armadores de respuesta).
    uint16_t largoRx = colaLen_[colaTail_];
    if (largoRx > maxLen) largoRx = maxLen;

    if (edadMs   != nullptr) *edadMs   = ahoraMs() - colaTick_[colaTail_];
    if (completa != nullptr) *completa = colaCompleta_[colaTail_];
    if (conCrc   != nullptr) *conCrc   = colaConCrc_[colaTail_];

    memcpy(destino, cola_[colaTail_], largoRx);
    colaTail_ = static_cast<uint8_t>((colaTail_ + 1) % COLA_TRAMAS);

    return largoRx;
}

// ---------------------------------------------------------------------------
//  ATENCION DEL BUS  -  se llama en cada vuelta del lazo
// ---------------------------------------------------------------------------
void SasEgm::atenderSas() {
    uint32_t edadMs        = 0;
    // largoRx: cuantos bytes tiene la trama que se va a atender.
    uint16_t largoRx       = 0;
    bool     tramaCompleta = false;
    bool     tramaConCrc   = false;

    // Se vacia la cola hasta dar con una trama FRESCA.  Si nos atrasamos, las
    // viejas se tiran: contestar tarde es peor que no contestar, porque la
    // respuesta se le cruza al host con su siguiente poleo y le rompe la
    // trama buena.
    while ((largoRx = leerTramaDeCola(bufferRecepcionSas_,
                                sizeof(bufferRecepcionSas_), &edadMs,
                                &tramaCompleta, &tramaConCrc)) > 0) {
        if (edadMs <= EDAD_MAXIMA_MS) break;
    }
    if (largoRx == 0) return;

    // Depuracion: lo que el host acaba de mandar y que se va a atender.
    if (hookTramas_ != nullptr) hookTramas_(false, bufferRecepcionSas_, largoRx);

    // Llego algo: el enlace esta vivo.  Si venia caido, el host acaba de
    // volver y no hay nada que hacer para retomar: la placa contesta lo que
    // le pregunten, como siempre.  Solo se anota para el diagnostico.
    msUltimaTrama_ = ahoraMs();
    enlaceArriba_  = true;

    // --- POLEO GENERAL: un solo byte ---------------------------------------
    if (largoRx == 1) {
        const uint8_t direccion = bufferRecepcionSas_[0];

        if (direccion == (uint8_t)(direccionSas_ | BIT_POLEO_GENERAL)) {
            // Es para nosotros.
            uint8_t excepcion = static_cast<uint8_t>(ExcepcionSas::NINGUNA);   // sin novedad

            if (EXCEPCIONES_SAS_ACTIVO) {
                if (excEnVuelo_) {
                    // El host REPITIO el mismo poleo general sin haber
                    // acusado la excepcion anterior.  Eso es un NACK
                    // implicito -SAS 6.02 seccion 3.2-: no la recibio bien.
                    // Se manda LA MISMA otra vez.
                    //
                    // Si en vez de esto se borrara al transmitir, el
                    // reintento del host contestaria 0x00 y el evento se
                    // perderia para siempre.  Un hand pay perdido es dinero
                    // que el sistema nunca se entero que salio.
                    excepcion = static_cast<uint8_t>(excEnVueloCodigo_);
                    sasNackImplicitos_++;
                }
                else if (excPendiente_ != ExcepcionSas::NINGUNA) {
                    excepcion = static_cast<uint8_t>(excPendiente_);
                    excEnVuelo_       = true;
                    excEnVueloCodigo_ = excPendiente_;
                    excPendiente_ = ExcepcionSas::NINGUNA;
                }
            }

            enviarTramaSas(&excepcion, 1);
            sasPoleosContestados_++;
            return;
        }

        // Poleo general dirigido a OTRA maquina del bus.  No se contesta,
        // pero segun la tabla 3.1 cuenta como acuse de recibo de nuestra
        // excepcion: el host siguio adelante con su ronda, o sea que la
        // recibio bien.
        if ((direccion & BIT_POLEO_GENERAL) && EXCEPCIONES_SAS_ACTIVO) {
            excEnVuelo_ = false;
        }
        return;
    }

    // --- LONG POLL ---------------------------------------------------------
    //
    // Un long poll tambien acusa recibo de la excepcion -tabla 3.1-.  Se
    // marca ANTES de contestar, porque la respuesta al long poll puede
    // levantar una excepcion nueva y esa no debe darse por entregada de una
    // vez.
    //
    // PERO SOLO SI EL MENSAJE ES VALIDO: entero y, si su comando lleva CRC,
    // con el CRC bueno.  Un mensaje roto es ruido: no existe para el EGM y no
    // prueba que el host haya recibido nada.  Si no se valida, un 2F corrupto
    // despues de avisar un hand pay daria por entregado el 0x51.
    // Con el mensaje roto el host sigue sin acusar, repite su poleo general y
    // el NACK implicito de arriba reenvia la MISMA excepcion.
    bool acuseValido = tramaCompleta;
    if (acuseValido && tramaConCrc) {
        acuseValido = false;
        if (largoRx >= 4) {
            const uint16_t crcCalculado = calcularCrc16Kermit(bufferRecepcionSas_,
                                                              (uint16_t)(largoRx - 2));
            const uint16_t crcTrama = (uint16_t)(bufferRecepcionSas_[largoRx - 2] |
                                                (bufferRecepcionSas_[largoRx - 1] << 8));
            acuseValido = (crcCalculado == crcTrama);
        }
    }
    if (EXCEPCIONES_SAS_ACTIVO && acuseValido) {
        excEnVuelo_ = false;
    }

    // Un comando con CRC invalido no se procesa.  Importa en el 0x2F, 0x72 y
    // 0x74, que traen datos (medidores, codigo de bloqueo, monto): un bit
    // volteado en el bus cambiaria el monto a transferir.
    // Si el comando no lleva CRC, se procesa igual.
    if (tramaConCrc && !acuseValido) return;

    ProcesarComandoSAS(bufferRecepcionSas_, largoRx);
}

// ---------------------------------------------------------------------------
//  VIGILANCIA DEL ENLACE
//
//  Corre en cada vuelta del lazo: se da cuenta de que el host se fue y
//  limpia lo que quedo a medias.
//
//  La placa solo habla cuando le preguntan.  La unica excepcion es el latido
//  (al final de vigilarEnlace), que el manual pide cuando pasan 5 s sin
//  ningun byte de direccion: el bus SAS es multipunto y un EGM que transmite
//  sin que le pregunten se atraviesa con la respuesta de otro.
// ---------------------------------------------------------------------------
bool SasEgm::enlaceArriba() const {
    return enlaceArriba_;
}


void SasEgm::vigilarEnlace() {
    const uint32_t ahora = ahoraMs();

    // --- se cayo? ------------------------------------------------------
    if (enlaceArriba_ && (ahora - msUltimaTrama_) >= ENLACE_TIMEOUT_MS) {
        enlaceArriba_ = false;
        caidasEnlace_++;

        // Limpieza de lo que quedo a medias.  Esto NO depende de ninguna
        // bandera: es correccion, no una funcion opcional.
        //
        //   - el bloqueo de AFT se suelta.  Lo pide el manual: una maquina
        //     que entra en link down estando bloqueada tiene que volver a
        //     "no locked".  Si no, queda muerta esperando a un host que ya
        //     no esta.
        //   - la excepcion "en vuelo" se tira.  Esa conversacion se murio
        //     con el host viejo; reenviarsela al que vuelva seria contarle
        //     el final de una historia que nunca escucho.
        //
        // Lo que SI se conserva es excPendiente_: si hubo un hand pay
        // mientras el host no estaba, se le cuenta en cuanto vuelva.
        liberarBloqueoAft();
        excEnVuelo_ = false;
    }

    // --- latido ("chirp", SAS 6.02 seccion 4.2) ---------------------------
    // Si no llega NINGUN byte de direccion en LATIDO_SILENCIO_MS, la placa manda
    // su direccion con el noveno bit en 1 (01 01 01 ...) cada LATIDO_PERIODO_MS.
    // Con el primer poleo se calla y todo sigue normal.  Tampoco sale si hay un
    // mensaje llegando o esperando en la cola.
    if (LATIDO_ACTIVO && ensambleLen_ == 0 && colaHead_ == colaTail_ &&
        (ahora - msUltimoWakeup_) >= LATIDO_SILENCIO_MS &&
        (ahora - msUltimoLatido_) >= LATIDO_PERIODO_MS) {
        msUltimoLatido_ = ahora;
        const uint16_t latido = (uint16_t)(direccionSas_ | BIT_WAKEUP);
        serial_->write9bit(&latido, 1);
    }
}

// ---------------------------------------------------------------------------
//  REEMISION DE LA EXCEPCION 0x51
//
//  Seccion 7.8.1 del SAS 6.02: si el host no viene por el 0x1B dentro de los
//  quince segundos siguientes al aviso, hay que repetirlo, y seguir
//  repitiendolo cada quince segundos mientras el hand pay siga pendiente.
//
//  El reloj se reinicia cada vez que se encola la excepcion, asi que la
//  primera reemision cae quince segundos despues del aviso original y no
//  quince segundos despues del arranque.
//
//  Ojo con la diferencia entre las dos banderas: excPendiente_ es "hay algo
//  que mandar en el proximo poleo" y excEnVuelo_ es "ya se mando pero el
//  host no la ha acusado".  Solo se reemite cuando las DOS estan en falso, o
//  sea cuando el aviso ya se dio por entregado y aun asi el host nunca vino
//  a preguntar.  Reemitir con una en vuelo seria pisar el reintento que ya
//  maneja atenderSas().
// ---------------------------------------------------------------------------
void SasEgm::reemitirExcepcionHandpay() {
    if (!EXCEPCIONES_SAS_ACTIVO) return;
    if (!handpayPendiente_)     return;
    if (excPendiente_ != ExcepcionSas::NINGUNA || excEnVuelo_) return;

    if ((ahoraMs() - msUltimaExc51_) < SAS_REEMITIR_51_MS) return;

    encolarExcepcionSas(ExcepcionSas::HANDPAY_PENDIENTE);
}

