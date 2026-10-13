/*
 * CrearJson.h
 *
 *  Created on: May 6, 2024
 *      Author: INICIO
 */

#ifndef SRC_CREARJSON_H_
#define SRC_CREARJSON_H_

#include <main.h>
#include <string.h>
#include <string>
#include <cstring>
#include <limits.h>
#include "../Src/VariablesGlobales.h"



#define Cadena_NO_valida 	  0
#define Solicitud_Cash_Out	  1
#define Reset_Meters		  2
#define Solicitud_Cash_In	  3
#define Configuracion_Tiempos 4
#define Solicitud_de_Estado   5
#define WEMOS_listo           6
#define Solicitud_de_FW       7
#define Diagnostico 		  8



// ===== Enum para el estado =====
enum class JsonStatus : uint8_t {
    Start,
    End
};


// ===== Estructura con los datos =====
struct JsonData {
    uint32_t egm;
    uint32_t sec;
    EventoTipo evt;
    const char* sta;   // "str" o "end"
    int contador1;
    int contador2;
    int contador3;
    int contador4;
    int contador5;
    const char*  CR;
   	const char*  S_M;
   	uint16_t temp10;
   	int  Num_Apagado;
   	uint16_t vmcu;
   	uint16_t vsrc;
};

class CrearJson {

private:



	// Nombres genéricos para contadores
		const char* Nombre_Contador1 = "chi";
		const char* Nombre_Contador2 = "bet";
	    const char* Nombre_Contador3 = "cho";
	    const char* Contador3_Inst   = "hop";
	    const char* Nombre_Contador4 = "handpay";
	    const char* Nombre_Contador5 = "refill";



	//Version FW
	char version_fw[12];
	int fecha = 0;
	int hora  = 0;



public:
	CrearJson();

	void ConstruirJson(const JsonData data, char* content);

	void JSON_Evento_INSTANTANEO(const JsonData& data, int Valor_Inst, char* content); //Evento BET Instantaneo

	int mes_a_numero(const char *mes);

	int obtener_fecha_compilacion();

	int obtener_hora_compilacion();

	int Recibe_JSON( uint8_t *JSON_datos_recibidos);

	int Decodifica_Solicitud_Cash_In( uint8_t *JSON_datos_recibidos, int& creditos, int& Tiempo_pulso);

	//Evento de error
	void JSON_Eve_Error(uint32_t& Contador_datos_enviados , char *content, char evento );


	void JSON_Eve_ON(uint32_t& Contador_datos_enviados ,  char *content);

	void JSON_Ver_FW(  char *content);

	void crear_version_fw(char *version, size_t len);



};

#endif /* SRC_CREARJSON_H_ */
