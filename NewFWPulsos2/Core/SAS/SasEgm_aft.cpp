/*
 * SasEgm_aft.cpp
 *
 *  AFT - Transferencia electronica de fondos.  Seccion 8 del SAS 6.02.
 *
 *  Aqui vive todo lo que necesita el cash out remoto que dispara el host.
 *  Es una pieza que se entiende sola: entra un bloqueo, sale un pago, se
 *  desbloquea.
 *
 *  LO QUE SI HACE:  retiro de la maquina al host -tipo 0x80-.
 *  LO QUE NO HACE:  cargar credito desde el host, tickets, bonos, debito.
 *                   Todo eso se rechaza con su codigo de error; no se finge.
 */

#include "SasEgm.h"

// ---------------------------------------------------------------------------
//  El estado del AFT -el bloqueo, la ultima transferencia, la posicion en el
//  historial y los contadores de diagnostico- son miembros privados de la
//  clase.  Estan declarados en SasEgm.h, con su explicacion.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
//  EL BLOQUEO NO PUEDE SER ETERNO
//
//  Corre en cada vuelta del lazo.  Si el host pidio bloqueo y nunca mando la
//  transferencia -se cayo, se reinicio, perdio el enlace-, la placa se suelta
//  sola pasado AFT_TIMEOUT_BLOQUEO_MS.
//
//  Sin esto, un host que se muere a la mitad deja la maquina bloqueada para
//  siempre.  El manual lo pide explicito: si el EGM entra en una condicion
//  donde no puede jugar, o el enlace se cae, tiene que volver a "no locked".
// ---------------------------------------------------------------------------
// Suelta el bloqueo, venga de donde venga la razon.  La usa el timeout de
// aqui abajo y tambien la vigilancia del enlace cuando el host desaparece.
void SasEgm::liberarBloqueoAft() {
    aftEstado_ = EstadoAft::LIBRE;
}

void SasEgm::vigilarBloqueoAft() {
    if (!AFT_ACTIVO) return;
    if (aftEstado_ == EstadoAft::LIBRE) return;

    if ((ahoraMs() - aftMsBloqueo_) >= AFT_TIMEOUT_BLOQUEO_MS) {
        aftEstado_ = EstadoAft::LIBRE;
    }
}


// ---------------------------------------------------------------------------
//  LONG POLL 0x74  -  bloqueo y estado
//
//  Peticion (tabla 8.2a), 8 bytes:
//      01 74 <codigo> <condicion> <timeout 2 BCD> <CRC 2>
//
//      codigo 00 = pedir bloqueo
//             80 = cancelar bloqueo
//             FF = solo preguntar, NO tocar nada
//
//  Respuesta (tabla 8.2b), 40 bytes clavados:
//      01 74 23 <activo 4> <estado 1> <transf disponibles 1>
//      <cashout host 1> <estado aft 1> <max buffer 1>
//      <cashable 5 BCD> <restringido 5> <no restringido 5> <limite 5>
//      <expiracion 4 BCD> <pool 2> <CRC 2>
//
//  El 0x23 es el largo de lo que sigue sin contar el CRC: 35 bytes.
//  4+1+1+1+1+1+5+5+5+5+4+2 = 35.  Si esa cuenta no cuadra, el host lee la
//  trama corrida y todo lo de atras es basura.
// ---------------------------------------------------------------------------
void SasEgm::responderAftLockStatus(const uint8_t* trama, uint16_t largoTrama) {
    if (!AFT_ACTIVO) return;              // callados: no prometemos AFT
    if (trama == nullptr || largoTrama < 6) return;

    const CodigoBloqueoAft codigo = static_cast<CodigoBloqueoAft>(trama[2]);

    // --- Que hacer con el bloqueo -----------------------------------------
    switch (codigo) {
    case CodigoBloqueoAft::CANCELAR:
    {
        // Cancelar.  Siempre se puede.
        aftEstado_ = EstadoAft::LIBRE;
        break;
    }
    case CodigoBloqueoAft::PEDIR:
    {
        // Pedir bloqueo.  El manual prohibe bloquear si la maquina esta
        // esperando un hand pay: ya hay dinero comprometido y meter una
        // transferencia encima seria pagarlo dos veces.
        const bool puedoBloquear = (aftEstado_ == EstadoAft::LIBRE) &&
                                   !hayHandpayPendiente();

        if (puedoBloquear) {
            // -----------------------------------------------------------
            //  El bloqueo se concede de inmediato (estado BLOQUEADO, no
            //  PENDIENTE).  "Pendiente" es para una maquina que debe terminar
            //  una jugada antes de bloquearse; esta placa no juega, asi que
            //  ya esta bloqueada.
            //
            //  El host recibe la excepcion 0x6F, y si interroga el estado con
            //  0x74 FF recibe el estado bloqueado.
            // -----------------------------------------------------------
            aftEstado_    = EstadoAft::BLOQUEADO;
            aftMsBloqueo_ = ahoraMs();
            aftBloqueos_++;

            encolarExcepcionSas(ExcepcionSas::JUEGO_BLOQUEADO);
        }
        break;
    }
    case CodigoBloqueoAft::INTERROGAR:   // no toca nada, por mandato del manual
    default:
        break;
    }

    // --- Armar la respuesta -----------------------------------------------
    // respuesta: la trama que se arma y se manda al final.
    // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
    uint8_t  respuesta[40] = {};
    uint16_t len = 0;

    respuesta[len++] = direccionSas_;
    respuesta[len++] = static_cast<uint8_t>(ComandoSas::AFT_BLOQUEO_Y_ESTADO);
    respuesta[len++] = AFT_LARGO_RESPUESTA_BLOQUEO;                       // 35 bytes siguen, sin contar CRC

    // Numero de activo, byte bajo primero.
    respuesta[len++] = (uint8_t)( NUMERO_ACTIVO        & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >>  8) & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >> 16) & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >> 24) & 0xFF);

    // Estado del bloqueo.  El 0x40 -pendiente- es lo que mi host espera en
    // la primera respuesta; el 0x6F que va detras es el que lo mueve.
    switch (aftEstado_) {
        case EstadoAft::BLOQUEADO: respuesta[len++] = static_cast<uint8_t>(EstadoBloqueoEnTrama::BLOQUEADO); break;

        // PENDIENTE existe en el enum pero esta placa nunca lo usa: no tiene
        // jugada que terminar antes de bloquearse.  Queda declarado porque
        // es un estado legitimo del protocolo y si algun dia la placa tiene
        // que esperar algo -un pulso a medias, por ejemplo- este es el lugar.
        case EstadoAft::PENDIENTE: respuesta[len++] = static_cast<uint8_t>(EstadoBloqueoEnTrama::PENDIENTE); break;
        default:                   respuesta[len++] = static_cast<uint8_t>(EstadoBloqueoEnTrama::LIBRE); break;
    }

    // Transferencias disponibles.  Bit 1 = "se puede transferir DESDE la
    // maquina".  Los demas en cero y es la verdad: esta placa no recibe
    // credito del host, no tiene impresora y no hace bonos.  Prender un bit
    // aqui es prometer algo que despues no se puede cumplir.
    respuesta[len++] = (creditoActual() > 0) ? AFT_TRANSFERENCIAS_DESDE : 0x00;

    // Estado del cash out al host: el bit 1 dice que esta habilitado.
    respuesta[len++] = AFT_CASHOUT_HABILITADO;

    // Estado de AFT.  Bit 7 = "AFT habilitado", bit 4 = "transferencias
    // in-house habilitadas", bit 1 = "acepto transferir menos del total".
    // Ese ultimo importa: mi host pide 99 99 99 99 99 -o sea "todo"- y sin
    // ese bit tendria que rechazarle cualquier monto que no sea exacto.
    respuesta[len++] = AFT_ESTADO_HABILITADO;

    // Tope del buffer de historial.  La tabla 8.2b solo admite de 0x46 a
    // 0x7F, o sea que no hay forma de declarar "guardo una sola".  Pongo el
    // minimo que el manual permite en vez del maximo, para no prometer un
    // historial grande que no tengo.
    //
    // LIMITACION CONOCIDA: solo se guarda la ULTIMA transferencia.  Si el host
    // interroga una posicion vieja, recibe la ultima.
    respuesta[len++] = AFT_BUFFER_HISTORIAL;

    // Importes, TODOS en centavos (tabla 8.2b).
    const uint64_t centavos = (uint64_t)creditoActual() *
                              (uint64_t)SAS_CREDITOS_POR_PULSO *
                              (uint64_t)CENTAVOS_POR_CREDITO;

    bcdEncodeSas(&respuesta[len], centavos, 5); len += 5;   // cashable: lo que hay
    bcdEncodeSas(&respuesta[len], 0,        5); len += 5;   // restringido: no manejamos
    bcdEncodeSas(&respuesta[len], 0,        5); len += 5;   // no restringido: tampoco

    // Limite de lo que se puede transferir HACIA la maquina.  Cero, porque
    // esta placa no sabe recibir credito: solo paga.
    bcdEncodeSas(&respuesta[len], 0, 5); len += 5;

    bcdEncodeSas(&respuesta[len], 0, 4); len += 4;   // expiracion de lo restringido
    respuesta[len++] = 0x00; respuesta[len++] = 0x00;        // pool ID

    const uint16_t crc = calcularCrc16Kermit(respuesta, len);
    respuesta[len++] = (uint8_t)(crc & 0xFF);
    respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);

    enviarTramaSas(respuesta, len);                // len tiene que ser 40
}


// ---------------------------------------------------------------------------
//  LONG POLL 0x72  -  la transferencia
//
//  Peticion (tabla 8.3a):
//      01 72 <largo> <codigo> <indice> <tipo> <cashable 5 BCD>
//      <restringido 5> <no restringido 5> <banderas 1> <activo 4>
//      <llave 20> <largo txid 1> <txid N> ... <CRC 2>
//
//      codigo 00 = solo transferencia completa
//             01 = se permite parcial
//             80 = cancelar
//             FE/FF = solo interrogar
//
//      tipo   80 = de la maquina al host  <- el unico que hacemos
//             00 = del host a la maquina  <- se rechaza
//
//  Mi host manda: 01 72 47 01 00 80 99 99 99 99 99 ...
//  o sea "sacame todo lo que puedas, acepto parcial".
// ---------------------------------------------------------------------------
void SasEgm::responderAftTransfer(const uint8_t* trama, uint16_t largoTrama) {
    if (!AFT_ACTIVO) return;
    if (trama == nullptr || largoTrama < 6) return;

    const uint8_t largoDatos = trama[2];
    const CodigoTransferAft codigo = static_cast<CodigoTransferAft>(trama[3]);

    // --- Interrogacion: repetir lo ultimo sin tocar nada -------------------
    switch (codigo) {
        case CodigoTransferAft::INTERROGAR_FE:
        case CodigoTransferAft::INTERROGAR_FF:
            armarRespuestaAft(aftUltEstado_, aftUltTipo_, aftUltCentavos_);
            return;
        default:
            break;
    }

    // AFT_LARGO_DATOS_MIN (8): lo minimo para que el monto (5 BCD desde el
    // byte 6) este dentro de la trama.  Con menos, se leerian bytes de una
    // trama vieja del mismo buffer.
    if (largoDatos < AFT_LARGO_DATOS_MIN || largoTrama < (uint16_t)(3 + largoDatos + 2)) return;

    const uint8_t tipo          = trama[5];
    const bool    permiteParcial = (codigo == CodigoTransferAft::PERMITE_PARCIAL);

    // Monto pedido, en centavos.  5 BCD a partir del byte 6.
    uint64_t pedidoCentavos = 0;
    for (uint8_t i = 0; i < 5; i++) {
        const uint8_t byteBcd = trama[6 + i];
        pedidoCentavos = pedidoCentavos * 100 + (uint64_t)((byteBcd >> 4) * 10 + (byteBcd & 0x0F));
    }

    // Se guarda el txid para devolverlo igual: el host lo usa para saber de
    // que transferencia se trata.
    aftUltTxIdLen_ = 0;
    {
        const uint16_t posLargoTx = AFT_POS_LARGO_TXID;
        if (posLargoTx < largoTrama) {
            uint8_t largoTxId = trama[posLargoTx];
            if (largoTxId > AFT_TXID_MAX) largoTxId = AFT_TXID_MAX;
            if ((uint16_t)(posLargoTx + 1 + largoTxId) <= largoTrama) {
                for (uint8_t i = 0; i < largoTxId; i++) aftUltTxId_[i] = trama[posLargoTx + 1 + i];
                aftUltTxIdLen_ = largoTxId;
            }
        }
    }

    // --- Cancelacion -------------------------------------------------------
    if (codigo == CodigoTransferAft::CANCELAR) {
        aftEstado_ = EstadoAft::LIBRE;
        armarRespuestaAft(EstadoTransferAft::SIN_TRANSFERENCIA, tipo, 0);       // C0 = no hay transferencia
        return;
    }

    // --- Lo que esta placa NO sabe hacer -----------------------------------
    //
    // Cualquier tipo que no sea "de la maquina al host" se rechaza con su
    // codigo.  Rechazar es una respuesta valida y honesta; contestar "listo"
    // a una carga que nunca ocurrio le mueve la contabilidad al casino.
    if (tipo != static_cast<uint8_t>(TipoTransferAft::DE_MAQUINA_AL_HOST)) {
        aftRechazadas_++;
        armarRespuestaAft(EstadoTransferAft::TIPO_NO_SOPORTADO, tipo, 0);       // 8F = tipo no soportado
        return;
    }

    // --- Cuanto hay de verdad ----------------------------------------------
    const uint64_t disponibleCentavos = (uint64_t)creditoActual() *
                                        (uint64_t)SAS_CREDITOS_POR_PULSO *
                                        (uint64_t)CENTAVOS_POR_CREDITO;

    // Sin credito no hay nada que transferir.
    if (disponibleCentavos == 0) {
        aftRechazadas_++;
        armarRespuestaAft(EstadoTransferAft::SIN_CREDITO, tipo, 0);       // 9A = no hay credito
        return;
    }

    uint64_t aTransferir = pedidoCentavos;
    EstadoTransferAft estado = EstadoTransferAft::COMPLETA;                // 00 = completa

    if (pedidoCentavos > disponibleCentavos) {
        // El host pide 99 99 99 99 99, que es su forma de decir "todo".
        // Con parcial permitido le doy lo que hay y le aviso que fue
        // parcial; sin parcial permitido no puedo hacer nada.
        if (!permiteParcial) {
            aftRechazadas_++;
            armarRespuestaAft(EstadoTransferAft::PARCIAL_NO_PERMITIDO, tipo, 0);   // 92 = no se permite parcial
            return;
        }
        aTransferir = disponibleCentavos;
        estado = EstadoTransferAft::PARCIAL;                     // 01 = parcial
    }

    // --- El pago -----------------------------------------------------------
    //
    // set_AftOut, NO set_Handpay.  Sube el medidor 0x18 -transferencias
    // electronicas al host- y baja el saldo, sin tocar el 0x04 ni el 0x23.
    //
    // Un retiro por AFT no es un credito cancelado: nadie lo pago a mano,
    // viajo por el sistema.  Si los mezclara, el casino perderia la unica
    // forma de distinguir un pago de atendiente de uno remoto, y ademas el
    // hand pay pendiente que levanta set_Handpay impediria el SIGUIENTE
    // bloqueo de AFT -el manual lo prohibe mientras haya un pago esperando-.
    // Sin division de 64 bits: se cuentan los creditos restando el valor de un
    // credito (en centavos) las veces que quepa, por escalas de 10 (de 10^9
    // a 1) para que nunca pase de unas 90 restas aunque el monto sea enorme.
    const uint64_t centavosPorCredito = (uint64_t)CENTAVOS_POR_CREDITO * (uint64_t)SAS_CREDITOS_POR_PULSO;
    uint64_t centavosRestantes = aTransferir;
    uint32_t creditos          = 0;
    // Cada escala resta de golpe un bloque de 10^n creditos (n = 9 ... 0).
    uint64_t centavosPorBloque = centavosPorCredito * 1000000000ULL;   // bloque de 10^9 creditos
    uint32_t creditosPorBloque = 1000000000UL;
    for (uint8_t indiceEscala = 0; indiceEscala < 10; indiceEscala++) {
        while (centavosRestantes >= centavosPorBloque) {
            centavosRestantes -= centavosPorBloque;
            creditos          += creditosPorBloque;
        }
        if (indiceEscala < 9) {
            // Baja una escala sin dividir: el bloque de 10^n se reconstruye
            // multiplicando por 10 desde 1 credito.
            centavosPorBloque = centavosPorCredito;
            creditosPorBloque = 1;
            const uint8_t multiplicacionesPor10 = (uint8_t)(8 - indiceEscala);
            for (uint8_t paso = 0; paso < multiplicacionesPor10; paso++) {
                centavosPorBloque *= 10;
                creditosPorBloque *= 10;
            }
        }
    }
    set_AftOut(creditos);

    aftTransferidas_++;
    aftEstado_ = EstadoAft::LIBRE;             // la transferencia suelta el bloqueo

    // Posicion nueva: le dice al host que esta es OTRA transferencia y no
    // la misma de antes.
    aftPosBuffer_ = (aftPosBuffer_ >= 0x7F) ? (uint8_t)1 : (uint8_t)(aftPosBuffer_ + 1);   // 1..0x7F

    armarRespuestaAft(estado, tipo, aTransferir);

    // Y se le avisa que ya quedo.  Prioritaria, igual que el 0x51.
    encolarExcepcionSas(ExcepcionSas::AFT_COMPLETA);
}


// ---------------------------------------------------------------------------
//  LA RESPUESTA DEL 0x72  (tabla 8.3c)
//
//      01 72 <largo> <posicion> <estado> <recibo> <tipo>
//      <cashable 5 BCD> <restringido 5> <no restringido 5> <banderas 1>
//      <activo 4> <largo txid 1> <txid N> <fecha 4 BCD> <hora 3 BCD>
//      <expiracion 4 BCD> <pool 2> <3 tamanos de medidor acumulado> <CRC 2>
//
//  Los tres medidores acumulados van con tamano CERO.  El manual lo permite
//  -"0 until complete"- y es lo correcto aqui: esta placa no lleva medidores
//  acumulados por tipo de transferencia.  Mandar un tamano y no los bytes
//  seria desincronizar al host.
// ---------------------------------------------------------------------------
void SasEgm::armarRespuestaAft(EstadoTransferAft estado, uint8_t tipo, uint64_t centavos) {
    // respuesta: la trama que se arma y se manda al final.
    // len: cuantos bytes de 'respuesta' ya estan escritos (el proximo va en len).
    uint8_t  respuesta[80] = {};
    uint16_t len = 0;

    respuesta[len++] = direccionSas_;
    respuesta[len++] = static_cast<uint8_t>(ComandoSas::AFT_TRANSFERENCIA);
    const uint16_t posLargo = len;
    respuesta[len++] = 0x00;                        // se rellena al final

    respuesta[len++] = aftPosBuffer_;              // posicion en el historial
    respuesta[len++] = static_cast<uint8_t>(estado);                      // tabla 8.3e
    respuesta[len++] = AFT_RECIBO_NINGUNO;                        // recibo: no se imprimio ninguno
    respuesta[len++] = tipo;

    bcdEncodeSas(&respuesta[len], centavos, 5); len += 5;   // cashable transferido
    bcdEncodeSas(&respuesta[len], 0,        5); len += 5;   // restringido
    bcdEncodeSas(&respuesta[len], 0,        5); len += 5;   // no restringido

    // Banderas: bit 3 = el host pidio el retiro, bit 1 = cash out habilitado.
    respuesta[len++] = AFT_BANDERAS_RESPUESTA;

    respuesta[len++] = (uint8_t)( NUMERO_ACTIVO        & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >>  8) & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >> 16) & 0xFF);
    respuesta[len++] = (uint8_t)((NUMERO_ACTIVO >> 24) & 0xFF);

    // El mismo txid que manda el host, para que sepa de cual transferencia se habla.
    respuesta[len++] = aftUltTxIdLen_;
    for (uint8_t i = 0; i < aftUltTxIdLen_; i++) respuesta[len++] = aftUltTxId_[i];

    // Fecha y hora.  En ceros: esta placa no tiene reloj de tiempo real.
    // Es dato faltante, no dato inventado.
    bcdEncodeSas(&respuesta[len], 0, 4); len += 4;
    bcdEncodeSas(&respuesta[len], 0, 3); len += 3;

    bcdEncodeSas(&respuesta[len], 0, 4); len += 4;    // expiracion
    respuesta[len++] = 0x00; respuesta[len++] = 0x00;         // pool ID

    respuesta[len++] = 0x00;                        // acumulado cashable: tamano 0
    respuesta[len++] = 0x00;                        // acumulado restringido: tamano 0
    respuesta[len++] = 0x00;                        // acumulado no restringido: tamano 0

    respuesta[posLargo] = (uint8_t)(len - 3);

    const uint16_t crc = calcularCrc16Kermit(respuesta, len);
    respuesta[len++] = (uint8_t)(crc & 0xFF);
    respuesta[len++] = (uint8_t)((crc >> 8) & 0xFF);

    enviarTramaSas(respuesta, len);

    // Se guarda por si el host interroga despues con codigo FE o FF.
    aftUltEstado_   = estado;
    aftUltTipo_     = tipo;
    aftUltCentavos_ = centavos;
}
