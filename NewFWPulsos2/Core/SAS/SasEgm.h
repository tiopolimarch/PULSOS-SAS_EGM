/*
 * SasEgm.h
 *
 *  SAS 6.02 LADO MAQUINA (EGM), COMO LIBRERIA.
 *
 *  Se usa asi, y no hace falta nada mas:
 *
 *      #include "SasEgm.h"
 *
 *      SasEgm* SAS_obj = SasEgm::getInstance();
 *
 *      // al arrancar, despues de MX_USARTx_UART_Init()
 *      SAS_obj->init(&huart2);
 *
 *      // en la maquina de estados, en cada vuelta
 *      SAS_obj->servicio();
 *
 *      // cuando el firmware mueve dinero
 *      SAS_obj->set_CurrentCredit(200);
 *      SAS_obj->set_Bet(5);
 *      SAS_obj->set_Handpay(50);
 *
 *  Y en los callbacks del HAL, una linea cada uno (el propio SAS revisa si
 *  el UART es el suyo, no hace falta el if):
 *
 *      SAS_obj->onRxEvent(huart, size);     // HAL_UARTEx_RxEventCallback
 *      SAS_obj->onTxComplete(huart);        // HAL_UART_TxCpltCallback
 *      SAS_obj->onError(huart);             // HAL_UART_ErrorCallback
 *
 *  DOS CLASES, CADA UNA CON UNA SOLA RESPONSABILIDAD
 *
 *      SasEgm       Todo el protocolo SAS: constantes, variables, metodos, el
 *                   armado de tramas y la maquina de estados.  No conoce el
 *                   hardware.  No tiene nada de otros protocolos.
 *      Serial9Bit   La comunicacion serial a 9 bits (Serial9Bit.h).  No conoce
 *                   el protocolo.  Se implementa una vez por plataforma.
 *
 *  Para otro micro o para una PC se escribe otra implementacion de Serial9Bit
 *  y se arranca con init(&miSerial).  SasEgm no cambia: solo hay que dar
 *  tambien un ahoraMs() para esa plataforma (en STM32 esta en SasEgmStm32.cpp).
 *
 *  VISIBILIDAD.  Publico solo lo necesario:
 *      - inicializacion         init()
 *      - loop principal         servicio()
 *      - interrupciones         onRxEvent(), onTxComplete(), onError()
 *      - getters y setters      creditoActual(), enlaceArriba(), set_*,
 *                               vincularContadores(), registrarCashOutLocal()
 *  Todo lo demas -constantes, variables, metodos y diagnostico- es privado.
 *
 *  POR QUE INSTANCIA UNICA.  Hay un solo bus SAS y una sola contabilidad
 *  por placa.  Dos objetos serian dos contabilidades contestandole al mismo
 *  host y los medidores empezarian a contradecirse sin que nada marcara error.
 */

#ifndef SRC_SASEGM_H_
#define SRC_SASEGM_H_

#include <stdint.h>
#include "Serial9Bit.h"

// La clase NO incluye el HAL.  Los metodos de STM32 (init(huart) y los tres
// callbacks) solo existen si el proyecto usa el HAL de ST: CubeIDE define
// USE_HAL_DRIVER solo, asi que en la placa no hay que hacer nada.  En otro
// micro o en PC no se declaran, y el nombre UART_HandleTypeDef no puede
// chocar con un tipo del mismo nombre de otro fabricante.
//
// -DSAS_SIN_HAL_ST los apaga aunque el proyecto use el HAL de ST (por ejemplo
// una placa ST que arranca el SAS con su propia Serial9Bit).
#if defined(USE_HAL_DRIVER) && !defined(SAS_SIN_HAL_ST)
#define SAS_CON_HAL_ST
// Es el mismo typedef que trae el HAL, asi que repetirlo aqui no choca.
typedef struct __UART_HandleTypeDef UART_HandleTypeDef;
#endif

// Lo que devuelve init().  Si no es OK, el SAS NO arranca y dice por que:
// son justo los errores de configuracion de CubeMX que hacen que "no
// funcione" sin ningun otro sintoma.
enum class SasInitResultado : uint8_t {
	OK = 0,
	SIN_UART,              // se paso nullptr
	NO_ES_19200,           // Baud Rate tiene que ser 19200
	NO_ES_9_BITS,          // Word length tiene que ser 9 bits
	CON_PARIDAD,           // Parity tiene que ser None
	SIN_DMA_RX,            // falta el DMA de RX (o su handle en main)
	DMA_RX_NO_CIRCULAR,    // el DMA de RX tiene que ser Circular
	DMA_RX_NO_HALFWORD     // el DMA de RX tiene que ser Half Word
};

class SasEgm {
public:
	// =====================================================================
	//  1. LA INSTANCIA
	// =====================================================================
	static SasEgm* getInstance();

	// =====================================================================
	//  2. INICIALIZACION  -  una vez, despues de inicializar el UART
	//
	//  El UART: 19200, 9 bits, sin paridad, 1 stop, DMA RX circular halfword.
	//  El DMA de TX es opcional: si no esta, se transmite por interrupcion.
	// =====================================================================
#ifdef SAS_CON_HAL_ST
	SasInitResultado init(UART_HandleTypeDef* huartSas);
#endif

	// Inicializacion con cualquier Serial9Bit ya preparada (emulador de PC,
	// otro micro).  init(huart) es esto mismo con la serial STM32 ya validada.
	SasInitResultado init(Serial9Bit* serial);

	// =====================================================================
	//  3. LOOP PRINCIPAL  -  en CADA vuelta del lazo
	//
	//  Contesta los poleos del host.  Si deja de llamarse, el enlace SAS se
	//  cae.  Por eso tiene que correr en todas las vueltas, o en un estado
	//  del que la maquina no salga.  No bloquea.
	// =====================================================================
	void servicio();

	// =====================================================================
	//  4. INTERRUPCIONES  -  pasar el huart tal cual llega al callback
	//     (solo con el HAL de ST; otras plataformas alimentan su Serial9Bit)
	// =====================================================================
#ifdef SAS_CON_HAL_ST
	void onRxEvent(UART_HandleTypeDef* huart, uint16_t size);
	void onTxComplete(UART_HandleTypeDef* huart);
	void onError(UART_HandleTypeDef* huart);
#endif

	// =====================================================================
	//  5. GETTERS Y SETTERS  -  todo en CREDITOS, nunca en centavos
	// =====================================================================
	uint32_t creditoActual() const;             // getter: saldo en creditos
	bool     enlaceArriba() const;              // getter: el host esta poleando

	// Lo que el host ordeno con los long polls 01 a 09.  La libreria solo
	// contesta ACK y guarda la orden: el FIRMWARE tiene que hacerles caso en su
	// lazo (apagar la entrada de juego, el billetero, el sonido).  Si la placa no
	// puede obedecer alguna, pon COMANDOS_HABILITAR_ACTIVO en false y no se
	// contestan.
	bool     juegoBloqueadoPorHost() const;           // 01 shutdown, o 09 deshabilitando el juego
	bool     sonidoApagadoPorHost() const;            // 03 o 05, hasta que llegue el 04
	bool     billeteroDeshabilitadoPorHost() const;   // 07, hasta que llegue el 06
	uint32_t billeteroDenominacionesPorHost() const;  // 08: bit en 1 = denominacion habilitada

	void set_CurrentCredit(uint32_t creditos);  // FIJA el saldo 0x1A; si sube, sube el 0x0B
	                                            // (con el contador del billetero vinculado solo fija el saldo: el contador ya lo cuenta)
	void set_Handpay(uint32_t creditos);        // SUMA: 0x04 y 0x23, baja el saldo, avisa 0x51
	                                            // (con el contador de cash out vinculado solo avisa 0x51: el contador ya lo cuenta)
	void set_Bet(uint32_t creditos);            // SUMA: 0x00

	// Cash out propio de la placa, ya pagado.  Si se apunta o no lo decide
	// CASHOUT_REFLEJA_EN_SAS (abajo), para no contarlo dos veces.
	void registrarCashOutLocal(uint32_t creditos);

	// OPCIONAL.  Si tu placa ya lleva contadores de pulsos persistentes
	// (en EEPROM), se los prestas y el SAS los reporta como medidores.
	// Cualquiera puede ir en nullptr.  Si no llamas esto, los medidores salen
	// solo de lo que metas con los set_*.
	//
	//   billIn    entrada de billetero    -> medidor 0x0B (y sube el saldo)
	//   apostado  total apostado          -> medidor 0x00
	//   cashOut   pulsos de hopper        -> medidor 0x04 / 0x23 (y baja el saldo)
	//   handpay   pulsos de hand pay      -> medidor 0x03
	void vincularContadores(const int* billIn, const int* apostado,
	                        const int* cashOut, const int* handpay);

	// OPCIONAL, SOLO PARA DEPURAR.  Si se registra una funcion, el SAS la llama
	// con cada mensaje del host que atiende (esTx = false) y con cada respuesta
	// que manda (esTx = true).  Corre en el lazo principal, nunca en una
	// interrupcion.  Con nullptr (el valor por defecto) no hace nada.
	typedef void (*HookTrama)(bool esTx, const uint8_t* datos, uint16_t largo);
	void setHookTramas(HookTrama hook) { hookTramas_ = hook; }

	// Direccion de ESTA maquina en el bus SAS (1 a 127).  Por defecto 1.
	// Llamarla ANTES de init(), una sola vez.  Devuelve false (y no cambia nada)
	// si el valor esta fuera de rango: 0 es el broadcast y de 128 para arriba
	// choca con el bit del poleo general.
	bool setDireccionSas(uint8_t direccion) {
		if (direccion < 1 || direccion > 127) return false;
		direccionSas_ = direccion;
		return true;
	}
	uint8_t direccionSas() const { return direccionSas_; }

private:
	// =====================================================================
	//  SOBRE EL USO DE "static" EN ESTA CLASE
	//
	//  Solo se usa en tres casos, y en los tres esta justificado:
	//
	//   1. getInstance(): variable estatica DENTRO de la funcion.  Se
	//      construye la primera vez que alguien la pide y no antes.  En un
	//      arranque de STM32 el orden de construccion de las globales entre
	//      archivos no esta definido; una que dependa de otra puede
	//      encontrarsela a medio hacer.
	//   2. static constexpr (constantes): una sola copia para toda la clase,
	//      resuelta en compilacion, sin ocupar RAM.
	//   3. Funciones static (bcdEncodeSas, calcularCrc16Kermit, leerContador,
	//      excEsPrioritaria, tamanoMedidorSas, copiarAsciiFijo): son
	//      funciones puras.  No leen ni escriben el estado del objeto; lo que
	//      entra determina lo que sale.  Declararlas static lo hace explicito
	//      y evita que alguien les meta estado sin darse cuenta.
	//
	//  El estado (saldo, cola de excepciones, estado del AFT...) vive en
	//  miembros privados de la clase, abajo; no hay variables static de archivo.
	// =====================================================================

	// =====================================================================
	//  CONFIGURACION  -  REVISAR ANTES DE USAR
	//
	//  Todas son constantes de compilacion a proposito: mueven dinero, y
	//  cambiarlas tiene que costar editar, recompilar y flashear.
	// =====================================================================

	// Velocidad del bus SAS.
	static constexpr uint32_t BAUDIOS_SAS = 19200;

	// Direccion en el bus, de 1 a 127.  Se contesta el poleo (DIRECCION|0x80).
	// Es el valor por defecto; se cambia SIN recompilar la libreria con
	// setDireccionSas() (publica), que guarda el valor en direccionSas_.
	static constexpr uint8_t DIRECCION_SAS_DEFECTO = 0x01;
	uint8_t direccionSas_ = DIRECCION_SAS_DEFECTO;

	// DENOMINACION.  En el codigo y en la API todo se maneja en PULSOS enteros:
	// 1 PULSO = 1 CREDITO de la placa = $1.00 (set_CurrentCredit(20) son $20).
	// Por el cable, los medidores y el saldo viajan en "creditos SAS", y el host los
	// multiplica por la denominacion del 0x1F.  Con denominacion $0.01 un credito
	// SAS es un centavo, asi que cada pulso vale 100 de ellos: 10 pulsos viajan como
	// 1000 y el host muestra $10.00, tanto si lee el 0x1F como si supone centavos.
	//   $1.00 por pulso  ->  SAS_CREDITOS_POR_PULSO = 100 y CODIGO_DENOMINACION = 0x01
	//   (si el host leyera el 0x1F, tambien serviria 1 y 0x06: 10 pulsos = 10 x $1.00)
	static constexpr uint32_t SAS_CREDITOS_POR_PULSO = 100;

	// Denominacion, tabla C-4: 0x01 = $0.01, 0x03 = $0.10, 0x06 = $1.00.
	static constexpr uint8_t CODIGO_DENOMINACION = 0x01;
	static constexpr uint8_t APUESTA_MAXIMA      = 0x00;   // POR CONFIRMAR
	static constexpr uint8_t GRUPO_PROGRESIVO    = 0x00;   // no hay progresivo
	static constexpr uint8_t OPCIONES_JUEGO_HI   = 0x00;
	static constexpr uint8_t OPCIONES_JUEGO_LO   = 0x00;

	// Identidad que contesta el 0x54.  Va en ASCII.
	static constexpr const char* SAS_VERSION_ASCII = "602";
	static constexpr const char* SERIE_MAQUINA     = "PULSOS001";   // la dada de alta en el casino

	// -----------------------------------------------------------------
	//  Identidad que contesta el 0x1F.  Tambien en ASCII, y con el LARGO
	//  EXACTO que pide la tabla 7.10 del SAS 6.02: 2, 3, 6 y 4 caracteres.
	//
	//  El armador recorta o rellena con '0' para que siempre salgan del
	//  tamano correcto, pero dejarlos ya del largo bueno hace obvio que se
	//  leyo la tabla y no se puso lo primero que cupo.
	//
	//  El porcentaje va SIN punto: el "88.50" se transmite "8850", y el
	//  host le pone el punto donde va.
	// -----------------------------------------------------------------
	static constexpr const char* ID_JUEGO_ASCII        = "PL";      // PuLsos
	static constexpr const char* ID_JUEGO_EXTRA_ASCII  = "000";     // no hay ID extra
	static constexpr const char* ID_PAYTABLE_ASCII     = "000000";  // POR CONFIRMAR
	static constexpr const char* PORCENTAJE_PAGO_ASCII = "0000";    // POR CONFIRMAR

	// Numero de activo para AFT.  El host lo manda en su trama 0x72.
	static constexpr uint32_t NUMERO_ACTIVO = 1;

	// Contesta el 0x74 y el 0x72 (retiro de la maquina al host).
	static constexpr bool AFT_ACTIVO = true;

	// Avisa los hand pay con la excepcion 0x51.  Va en true.
	static constexpr bool EXCEPCIONES_SAS_ACTIVO = true;

	// Monto del 0x1B.  El manual lo pide en creditos SAS, o sea en la unidad de la
	// denominacion declarada (ver SAS_CREDITOS_POR_PULSO): con $0.01 un hand pay de
	// 7 pulsos sale 700.  false deja el 1B asi.  true lo multiplica ademas por
	// CENTAVOS_POR_CREDITO (solo para un host raro que divida ese campo entre 100).
	static constexpr bool     HANDPAY_1B_EN_CENTAVOS = false;

	// Centavos que vale un credito SAS, para el AFT (0x72 y 0x74), que el manual
	// define en centavos.  Con denominacion $0.01 un credito SAS ya es un centavo,
	// asi que va en 1: 20 pulsos viajan como 2000 centavos.  Con denominacion $1.00
	// (SAS_CREDITOS_POR_PULSO = 1, CODIGO_DENOMINACION = 0x06) iria en 100.
	static constexpr uint32_t CENTAVOS_POR_CREDITO   = 1;

	// CABLEADO: si los pulsos del hopper REGRESAN por la entrada de la placa,
	// el medidor ya sube solo y esto va en false (si no, se cuenta dos
	// veces).  Si NO regresan, va en true.
	//   true  = los pulsos del hopper NO regresan a la placa: se apunta aqui.
	//   false = SI regresan: el medidor ya sube solo, apuntarlo lo contaria doble.
	static constexpr bool CASHOUT_REFLEJA_EN_SAS = true;

	// Silencio antes de dar el host por caido.
	static constexpr uint32_t ENLACE_TIMEOUT_MS = 5000;

	// Latido ("chirp", SAS 6.02 seccion 4.2): si la placa no recibe NINGUN byte de
	// direccion en LATIDO_SILENCIO_MS, manda su direccion con el noveno bit en 1
	// (01 01 01 ...) cada LATIDO_PERIODO_MS.  Con el primer poleo se calla.
	// false = la placa no habla nunca sin que le pregunten.
	static constexpr bool     LATIDO_ACTIVO      = true;
	static constexpr uint32_t LATIDO_SILENCIO_MS = 5000;
	static constexpr uint32_t LATIDO_PERIODO_MS  = 200;

	// Contesta los long polls 01 a 09 (apagar/encender juego, sonido, billetero,
	// juego N).  Ojo: el ACK le dice al host "ya lo hice".  Solo va en true si el
	// firmware lee juegoBloqueadoPorHost(), sonidoApagadoPorHost() y
	// billeteroDeshabilitadoPorHost() y de verdad apaga lo que corresponde.
	static constexpr bool COMANDOS_HABILITAR_ACTIVO = true;

	// Un mensaje que lleva a medias mas de esto (byte perdido, ruido) se
	// descarta.  Se cuenta desde el primer byte del mensaje; un mensaje nuevo
	// siempre corta al anterior sin esperar este tiempo.  Validado en placa.
	static constexpr uint32_t TIMEOUT_TRAMA_MS = 50;

	// Una trama que lleva en la cola mas de esto se tira sin contestar:
	// contestar tarde es peor que no contestar, porque la respuesta se le
	// cruza al host con su siguiente poleo.  Validado en placa.
	static constexpr uint32_t EDAD_MAXIMA_MS = 20;

	// Si el host no pide el bloqueo AFT a tiempo, se suelta solo.
	static constexpr uint32_t AFT_TIMEOUT_BLOQUEO_MS = 15000;

	// =====================================================================
	//  CONSTANTES DEL PROTOCOLO  -  no se tocan
	// =====================================================================
	static constexpr uint8_t  SAS_MAX_MEDIDORES  = 60;      // tabla 7.3b
	static constexpr uint8_t  SAS_2F_LARGO_MAX   = 0x3E;    // tabla 7.3b
	static constexpr uint32_t SAS_REEMITIR_51_MS = 15000;   // seccion 7.8.1

	// El poleo general es un solo byte: la direccion con el bit 7 puesto.
	static constexpr uint8_t BIT_POLEO_GENERAL = 0x80;

	// Tamano de los medidores en el 0x2F (tabla C-7, "Min Size"):
	//   0x0D..0x10  tickets SAS en centavos            -> 5 bytes BCD
	//   0x80..0xBE  tickets y transferencias AFT       -> 5 bytes BCD
	//   todo lo demas                                  -> 4 bytes BCD
	static constexpr uint8_t MEDIDOR_TICKETS_MIN = 0x0D;
	static constexpr uint8_t MEDIDOR_TICKETS_MAX = 0x10;
	static constexpr uint8_t MEDIDOR_AFT_MIN     = 0x80;
	static constexpr uint8_t MEDIDOR_AFT_MAX     = 0xBE;
	static constexpr uint8_t BCD_TAMANO_CORTO    = 4;
	static constexpr uint8_t BCD_TAMANO_LARGO    = 5;

	// Recepcion: el noveno bit y los ocho de dato de cada palabra.
	static constexpr uint16_t BIT_WAKEUP    = 0x0100;   // 9no bit: inicio de mensaje
	static constexpr uint16_t MASCARA_DATO  = 0x00FF;

	// Trama SAS mas larga (long poll con muchos medidores) + margen.
	static constexpr uint16_t MAX_TRAMA = 260;

	// Cuantas tramas completas pueden esperar en cola.
	static constexpr uint8_t COLA_TRAMAS = 8;

	// AFT: valores fijos de las respuestas (tablas 8.2b y 8.3c).
	static constexpr uint8_t  AFT_LARGO_RESPUESTA_BLOQUEO = 0x23;  // 35 bytes sin CRC
	static constexpr uint8_t  AFT_TRANSFERENCIAS_DESDE    = 0x02;  // bit 1: desde la maquina
	static constexpr uint8_t  AFT_CASHOUT_HABILITADO      = 0x02;  // bit 1: habilitado
	static constexpr uint8_t  AFT_ESTADO_HABILITADO       = 0x92;  // bits 7, 4 y 1
	static constexpr uint8_t  AFT_BUFFER_HISTORIAL        = 0x46;  // el minimo que permite el manual
	static constexpr uint8_t  AFT_RECIBO_NINGUNO          = 0xFF;  // no se imprimio ninguno
	static constexpr uint8_t  AFT_BANDERAS_RESPUESTA      = 0x0A;  // bit 3: pidio el host, bit 1: cash out
	static constexpr uint8_t  AFT_TXID_MAX                = 20;
	static constexpr uint16_t AFT_POS_LARGO_TXID          = 46;    // donde va el largo del txid en el 0x72

	// Minimo de 'largoDatos' (tabla 8.3a) para que el monto este dentro de
	// la trama: codigo(1) + indice(1) + tipo(1) + cashable(5) = 8.
	// Con menos, el 0x72 se ignora.
	static constexpr uint8_t  AFT_LARGO_DATOS_MIN         = 8;

	// -----------------------------------------------------------------
	//  Codigos del protocolo con nombre.  Se usan en los switch-case en
	//  lugar de numeros en hexadecimal.
	// -----------------------------------------------------------------

	// Long polls.  El numero del LONG POLL y el del MEDIDOR son dos
	// numeraciones distintas: el long poll 0x11 pide el medidor 0x00.
	enum class ComandoSas : uint8_t {
		// --- los que la placa contesta ---
		// Habilitar / deshabilitar (tipo S: direccion + comando + CRC).  Solo se
		// contestan si COMANDOS_HABILITAR_ACTIVO esta en true.
		APAGAR_JUEGO                = 0x01,   // shutdown: bloquear el juego
		ENCENDER_JUEGO              = 0x02,   // startup: habilitar el juego
		SONIDO_APAGAR               = 0x03,
		SONIDO_ENCENDER             = 0x04,
		SONIDO_JUEGO_APAGAR         = 0x05,   // sonidos de giro y juego
		BILLETERO_HABILITAR         = 0x06,
		BILLETERO_DESHABILITAR      = 0x07,
		BILLETERO_CONFIGURAR        = 0x08,   // denominaciones (4 bytes) + bandera
		JUEGO_N_HABILITAR           = 0x09,   // juego N (2 BCD) + 00 deshabilitar / 01 habilitar

		MANTENIMIENTO_ENTRAR        = 0x0A,
		MANTENIMIENTO_SALIR         = 0x0B,
		BLOQUE_DESDE_CANCELADOS     = 0x0F,   // cancelados, coin in, coin out, drop, jackpot, jugadas
		TOTAL_CANCELADOS            = 0x10,
		TOTAL_COIN_IN               = 0x11,   // = total apostado (BET)
		TOTAL_COIN_OUT              = 0x12,
		TOTAL_DROP                  = 0x13,
		TOTAL_JACKPOT               = 0x14,
		PARTIDAS_JUGADAS            = 0x15,
		PARTIDAS_GANADAS            = 0x16,
		PARTIDAS_PERDIDAS           = 0x17,
		PARTIDAS_DESDE_ENCENDIDO    = 0x18,
		BLOQUE_DESDE_COIN_IN        = 0x19,   // lo mismo sin los cancelados
		CREDITOS_ACTUALES           = 0x1A,
		INFO_HANDPAY                = 0x1B,
		IDENTIDAD_MAQUINA           = 0x1F,
		DOLARES_BILLETES            = 0x20,
		MEDIDORES_SELECTOS          = 0x2F,
		CREDITOS_BILLETES           = 0x46,
		VERSION_Y_SERIE             = 0x54,
		AFT_TRANSFERENCIA           = 0x72,
		AFT_BLOQUEO_Y_ESTADO        = 0x74,
		RESET_HANDPAY               = 0x94,
		FUNCIONES_HABILITADAS       = 0xA0,

		// --- solo para saber cuanto mide el mensaje; no se contestan ---
		VALIDACION_NUMERO           = 0x2E,
		FECHA_HORA_FIJAR            = 0x4C,
		FECHA_HORA_RECIBIR          = 0x4D,
		VALIDACION_ESTADO_EXTENDIDO = 0x7B,
		MEDIDORES_EXTENDIDOS        = 0xAF,
		MULTIDENOM_PREAMBULO        = 0xB0
	};

	// Medidores (tabla C-7 del manual 6.02).
	enum class MedidorSas : uint8_t {
		COIN_IN_APOSTADO        = 0x00,   // el BET: en SAS lo apostado se llama "coin in"
		HANDPAY_CANCELADOS      = 0x03,   // pulso de HAND PAY propio de la placa
		CREDITOS_CANCELADOS     = 0x04,   // el cash out
		CREDITOS_BILLETE        = 0x0B,   // el cash in entra por billetero
		CREDITOS_ACTUALES       = 0x0C,   // el saldo, igual que el long poll 0x1A
		TRANSFERENCIAS_AFT_HOST = 0x18,   // retiros por AFT
		HANDPAY_PAGADOS         = 0x23    // va a la par del 0x04
	};

	// Excepciones.  Las cuatro con valor llevan dinero detras, y por eso el
	// manual las marca como prioritarias.
	enum class ExcepcionSas : uint8_t {
		NINGUNA           = 0x00,   // "sin novedad"
		HANDPAY_PENDIENTE = 0x51,
		HANDPAY_RESETEADO = 0x52,
		AFT_COMPLETA      = 0x69,
		JUEGO_BLOQUEADO   = 0x6F
	};

	// AFT: en que punto de la secuencia vamos.
	enum class EstadoAft : uint8_t {
		LIBRE = 0,      // 0xFF para el host
		PENDIENTE,      // 0x40
		BLOQUEADO       // 0x00 - listo para recibir la transferencia
	};

	// AFT: como se ve el estado del bloqueo dentro de la trama (tabla 8.2b).
	enum class EstadoBloqueoEnTrama : uint8_t {
		BLOQUEADO = 0x00,
		PENDIENTE = 0x40,
		LIBRE     = 0xFF
	};

	// AFT: lo que pide el host en el 0x74 (tabla 8.2a).
	enum class CodigoBloqueoAft : uint8_t {
		PEDIR      = 0x00,
		CANCELAR   = 0x80,
		INTERROGAR = 0xFF      // solo preguntar, NO tocar nada
	};

	// AFT: lo que pide el host en el 0x72 (tabla 8.3a).
	enum class CodigoTransferAft : uint8_t {
		SOLO_COMPLETA   = 0x00,
		PERMITE_PARCIAL = 0x01,
		CANCELAR        = 0x80,
		INTERROGAR_FE   = 0xFE,
		INTERROGAR_FF   = 0xFF
	};

	// AFT: tipo de transferencia (tabla 8.3a).
	enum class TipoTransferAft : uint8_t {
		DEL_HOST_A_MAQUINA = 0x00,   // se rechaza
		DE_MAQUINA_AL_HOST = 0x80    // el unico que se hace
	};

	// AFT: codigos de estado de la transferencia (tabla 8.3e).
	enum class EstadoTransferAft : uint8_t {
		COMPLETA             = 0x00,
		PARCIAL              = 0x01,
		TIPO_NO_SOPORTADO    = 0x8F,
		PARCIAL_NO_PERMITIDO = 0x92,
		SIN_CREDITO          = 0x9A,
		SIN_TRANSFERENCIA    = 0xC0
	};

	// =====================================================================
	//  ESTADO INTERNO
	// =====================================================================
	SasEgm() = default;
	SasEgm(const SasEgm&)            = delete;
	SasEgm& operator=(const SasEgm&) = delete;

	Serial9Bit*         serial_   = nullptr;
#ifdef SAS_CON_HAL_ST
	UART_HandleTypeDef* huart_    = nullptr;
#endif
	bool                iniciado_ = false;

	// Contadores prestados en vincularContadores().  Cualquiera puede ser nullptr.
	const int* pBillIn_   = nullptr;
	const int* pApostado_ = nullptr;
	const int* pCashOut_  = nullptr;
	const int* pHandpay_  = nullptr;

	uint8_t  bufferRecepcionSas_[MAX_TRAMA] = {};
	uint32_t temporizadorUltimoSas_         = 0;

	// ---------------------------------------------------------------------
	//  ARMADO DE TRAMAS
	//
	//  Junta las palabras de 9 bits que entrega la serial en mensajes SAS
	//  completos.  Corre en el lazo principal.
	// ---------------------------------------------------------------------
	uint8_t  ensamble_[MAX_TRAMA]           = {};
	uint16_t ensambleLen_                   = 0;
	uint32_t ensambleTick_                  = 0;    // cuando llego su primer byte

	uint8_t  cola_[COLA_TRAMAS][MAX_TRAMA]  = {};
	uint16_t colaLen_[COLA_TRAMAS]          = {};
	uint32_t colaTick_[COLA_TRAMAS]         = {};   // cuando llego su ultimo byte
	bool     colaCompleta_[COLA_TRAMAS]     = {};
	bool     colaConCrc_[COLA_TRAMAS]       = {};
	uint8_t  colaHead_                      = 0;
	uint8_t  colaTail_                      = 0;

	// ---------------------------------------------------------------------
	//  ESTADO DE LOS MEDIDORES QUE LLEVO YO
	//
	//  Estas variables son lo que mueven los set_*.  Lo demas sale de los
	//  contadores de pulsos que se hayan vinculado con vincularContadores().
	//
	//  OJO: viven en RAM.  Si la placa no vincula contadores persistentes, los
	//  medidores vuelven a cero al reiniciar.
	//
	//  LOS AJUSTES SON PROGRESIVOS: se SUMAN a lo que ya traia el contador de
	//  pulsos y nunca se restan.  Un medidor SAS es un acumulado de por vida y
	//  no puede ir para atras; un host que ve un medidor bajar da por perdida
	//  toda la contabilidad de esa maquina.
	// ---------------------------------------------------------------------

	// Lo que reporto en el 0x1A.  Este SI sube y baja: no es un acumulado, es el
	// saldo que el jugador tiene en pantalla ahora mismo.
	uint32_t creditoActual_   = 0;

	// Ajustes que sumo encima de los contadores de pulsos.  Los mueven los
	// setters.
	uint32_t ajusteBillIn_    = 0;   // se suma al 0x0B  (billetero)
	uint32_t ajusteHandpay_   = 0;   // se suma al 0x23 y al 0x04
	uint32_t ajusteBet_       = 0;   // se suma al 0x00  (apostado)
	uint32_t ajusteAftOut_    = 0;   // se suma al 0x18  (retiros por AFT)

	// Con estos dos sigo los contadores de pulsos entre una vuelta y otra, para
	// saber cuanto se movieron y aplicar esa diferencia al saldo.
	int32_t  ultBillInPulsos_  = 0;
	int32_t  ultCashOutPulsos_ = 0;
	bool     primeraLectura_   = true;

	// Creditos que set_CurrentCredit() subio con el contador del billetero
	// vinculado y que el contador todavia no alcanzo: evitan contarlos dos veces.
	uint32_t pendienteSetBill_   = 0;
	uint32_t msPendienteSetBill_ = 0;
	static constexpr uint32_t VENTANA_SET_BILLETE_MS = 1000;

	// ---------------------------------------------------------------------
	//  HAND PAY PENDIENTE  (lo que se contesta en el 0x1B)
	//
	//  El 0x1B no es un medidor: es la ficha del hand pay que esta trabado AHORA.
	//  Mientras no haya ninguno la trama sale toda en ceros, que es justo lo que
	//  manda la seccion 7.8.2 del SAS 6.02.
	// ---------------------------------------------------------------------
	bool     handpayPendiente_ = false;
	uint32_t handpayMonto_     = 0;

	// ---------------------------------------------------------------------
	//  COLA DE EXCEPCIONES  (solo se usa si EXCEPCIONES_SAS_ACTIVO esta en true)
	//
	//  "En vuelo" quiere decir: ya se transmitio pero el host todavia no la
	//  acusa.  Hasta que la acuse NO se borra, porque si el host repite el
	//  mismo poleo general es que no la recibio y se manda la misma.
	// ---------------------------------------------------------------------
	ExcepcionSas excPendiente_     = ExcepcionSas::NINGUNA;   // la que toca mandar
	bool         excEnVuelo_       = false;
	ExcepcionSas excEnVueloCodigo_ = ExcepcionSas::NINGUNA;

	// Cuando se encolo la ultima excepcion.  De aqui cuenta la reemision del
	// 0x51 que pide la seccion 7.8.1.
	uint32_t msUltimaExc51_        = 0;

	// ---------------------------------------------------------------------
	//  ESTADO DEL ENLACE CON EL HOST
	//
	//  Arranca CAIDO: mientras no llegue el primer poleo no hay enlace, y eso es
	//  la verdad.
	// ---------------------------------------------------------------------
	bool     enlaceArriba_     = false;
	uint32_t msUltimaTrama_    = 0;
	uint32_t msUltimoLatido_   = 0;   // cuando salio el ultimo latido
	uint32_t msUltimoWakeup_   = 0;   // cuando llego el ultimo byte de direccion (noveno bit en 1)

	// Ordenes del host (long polls 01 a 09).  Ver juegoBloqueadoPorHost() y demas.
	bool     apagadoPorHost_          = false;   // 01 / 02
	bool     juegoDeshabilitado_      = false;   // 09
	bool     sonidoApagado_           = false;   // 03 / 05 / 04
	bool     billeteroDeshabilitado_  = false;   // 07 / 06
	uint32_t billeteroMascara_        = 0xFFFFFFFFu;   // 08

	// Modo mantenimiento: lo prende el long poll 0x0A y lo apaga el 0x0B.  No
	// hace nada mas que quedar registrado, para poder verlo en el depurador y
	// saber si el host lo esta usando.
	bool     modoMantenimiento_ = false;

	// ---------------------------------------------------------------------
	//  ESTADO DEL AFT  (SasEgm_aft.cpp)
	// ---------------------------------------------------------------------
	EstadoAft aftEstado_        = EstadoAft::LIBRE;
	uint32_t  aftMsBloqueo_     = 0;      // cuando empezo el bloqueo

	// Ultima transferencia, para poder contestar una interrogacion despues.
	EstadoTransferAft aftUltEstado_ = EstadoTransferAft::COMPLETA;   // tabla 8.3e
	uint8_t   aftUltTipo_       = static_cast<uint8_t>(TipoTransferAft::DE_MAQUINA_AL_HOST);
	uint64_t  aftUltCentavos_   = 0;
	uint8_t   aftUltTxIdLen_    = 0;
	uint8_t   aftUltTxId_[AFT_TXID_MAX] = {0};

	// Posicion en el buffer de historial.  El manual permite contestar 0x00
	// -"la transferencia actual, no esta en el historial"-, pero el host de este
	// proyecto usa justo ese byte para darse cuenta de que hay una transferencia
	// NUEVA: con un cero fijo, del segundo cash out en adelante los veia
	// identicos al anterior y los IGNORABA en silencio.  Se incrementa en cada
	// transferencia completada, dando la vuelta de 1 a 0x7F -el 0 queda
	// reservado para "actual"-.
	uint8_t   aftPosBuffer_     = 0;

	// ---------------------------------------------------------------------
	//  DIAGNOSTICO  -  privado; se mira en el depurador
	// ---------------------------------------------------------------------
	uint32_t sasPoleosContestados_ = 0;
	uint32_t sasTramasAtendidas_   = 0;
	uint32_t sasCrcMalos_          = 0;
	uint32_t sasNackImplicitos_    = 0;
	uint32_t excPisadas_           = 0;   // avisos perdidos porque llego otro encima
	uint32_t caidasEnlace_         = 0;
	uint32_t statTramas_           = 0;   // tramas completas recibidas
	uint32_t statColaLlena_        = 0;   // tramas perdidas por cola llena
	uint32_t statIncompletas_      = 0;   // mensajes a medias
	HookTrama hookTramas_          = nullptr;   // depuracion: ver setHookTramas()
	uint32_t statWakeup1_          = 0;   // palabras con el 9no bit
	uint32_t statWakeup0_          = 0;   // palabras sin el 9no bit
	uint32_t aftBloqueos_          = 0;
	uint32_t aftTransferidas_      = 0;
	uint32_t aftRechazadas_        = 0;

	// =====================================================================
	//  MOTOR
	// =====================================================================
	// Unico reloj de la clase, en ms; no debe retroceder.  Lo da el archivo de
	// cada plataforma (en STM32, SasEgmStm32.cpp): la clase no conoce el hardware.
	uint32_t ahoraMs() const;

	// Recepcion y armado de tramas
	static constexpr size_t LOTE_RX = 16;   // palabras que se piden a la serial por llamada
	void     reiniciarEnsamblado();
	void     recibirPalabras();
	void     procesarPalabraRx(uint16_t palabra, uint32_t marcaMs);
	uint16_t largoEsperado(bool* conCrc) const;
	void     publicarTrama(bool completa, uint32_t marcaMs);
	uint16_t leerTramaDeCola(uint8_t* destino, uint16_t maxLen, uint32_t* edadMs,
	                         bool* completa, bool* conCrc);

	void atenderSas();
	void ProcesarComandoSAS(const uint8_t* trama, uint16_t largoTrama);
	bool procesarComandoContadoresDetallados(const uint8_t* trama, uint16_t largoTrama);

	void actualizarCreditoPorPulsos();
	void reemitirExcepcionHandpay();
	void vigilarEnlace();
	void encolarExcepcionSas(ExcepcionSas codigo);

	// Puente codigo de medidor SAS -> valor.  Unico lugar donde se decide
	// que numero contesta la placa para cada medidor.
	bool obtenerValorMedidorRuta(uint8_t codigo, uint64_t* salida);
	uint64_t medidorSas(MedidorSas medidor);

	// Funciones puras (ver "sobre el uso de static", arriba).
	static void     bcdEncodeSas(uint8_t* destino, uint64_t valor, uint8_t nBytes);
	static uint16_t calcularCrc16Kermit(const uint8_t* datos, uint16_t longitud);
	static uint64_t leerContador(const int* contador);
	static bool     excEsPrioritaria(ExcepcionSas codigo);
	static uint8_t  tamanoMedidorSas(uint8_t codigo);
	static void     copiarAsciiFijo(uint8_t* destino, const char* texto, uint8_t largo);

	bool enviarTramaSas(const uint8_t* payload, uint16_t longitud);

	void responderContadorBCD(ComandoSas comando, uint64_t valor);
	void responderBloqueMedidores(ComandoSas comando, const uint64_t* valores, uint8_t nValores);
	void responderIdentidadMaquina();
	void responderHandpayInfo();
	void responderAckSimple();

	// =====================================================================
	//  AFT  (SasEgm_aft.cpp)
	// =====================================================================
	void responderAftLockStatus(const uint8_t* trama, uint16_t largoTrama);
	void responderAftTransfer(const uint8_t* trama, uint16_t largoTrama);
	void armarRespuestaAft(EstadoTransferAft estado, uint8_t tipo, uint64_t centavos);
	void vigilarBloqueoAft();
	void liberarBloqueoAft();
	bool hayHandpayPendiente() const;

	// Solo lo llama el AFT: sube el 0x18 sin levantar hand pay.  Privado para
	// que nadie infle ese medidor sin que haya habido transferencia.
	void set_AftOut(uint32_t creditos);
};

#endif /* SRC_SASEGM_H_ */
