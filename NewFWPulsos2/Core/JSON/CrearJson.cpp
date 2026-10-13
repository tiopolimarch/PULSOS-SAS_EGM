/*
 * CrearJson.cpp
 *
 *  Created on: May 6, 2024
 *      Author: INICIO
 */

#include "CrearJson.h"


CrearJson::CrearJson(){
	// TODO Auto-generated constructor stub



	fecha = obtener_fecha_compilacion();
	hora = obtener_hora_compilacion();
	crear_version_fw(version_fw, sizeof(version_fw));

}




static bool ExtraerEnteroJSON(const char* json, const char* clave, int& valor)
{
    const char* pos = strstr(json, clave);
    if (!pos) return false;

    pos += strlen(clave);

    char* endptr;
    long v = strtol(pos, &endptr, 10);

    if (endptr == pos || *endptr != '"') return false;
    if (v < 0 || v > INT_MAX) return false;

    valor = (int)v;
    return true;
}





// ===== Construcción del JSON =====
void CrearJson::ConstruirJson(const JsonData data, char* content) {
    int j = 0;
    j  = sprintf(content, "{\"egm\":%lu,", data.egm);
    j += sprintf(content+j, "\"sec\":%lu,", data.sec);
        switch(data.evt) {
            case EventoTipo::Contador1:
                j += sprintf(content+j, "\"evt\":\"e%s\",", Nombre_Contador1);
                j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
                break;

            case EventoTipo::Contador2:
                j += sprintf(content+j, "\"evt\":\"e%s\",",Nombre_Contador2);
                j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
                break;

            case EventoTipo::Contador3:
			    j += sprintf(content+j, "\"evt\":\"e%s\",",Nombre_Contador3);
			   	j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				break;

            case EventoTipo::RamClear:
            	j += sprintf(content+j, "\"evt\":\"ermc\","); // Evento RamClear
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
				break;

            case EventoTipo::EdoContadores:
            	j += sprintf(content+j, "\"evt\":\"eedo\","); // Evento RamClear
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
				break;

            case EventoTipo::VersionFW:
				j += sprintf(content+j, "\"evt\":\"ever\","); // Evento Version
				j += sprintf(content + j, "\"ver\":\"%s\",", version_fw);
				j += sprintf(content + j, "\"fech\":\"%d\",", fecha);
				j += sprintf(content + j, "\"hora\":\"%d\",", hora);
				break;


            case EventoTipo::Diagnostico_STM:
            	j += sprintf(content+j, "\"evt\":\"diag\","); // Evento Cash In
				/* Temperatura con decimal */
				j += sprintf(content+j, "\"tem\":\"%d.%d\",", data.temp10 / 10, abs(data.temp10 % 10));
				j += sprintf(content + j, "\"noff\":\"%d\",", data.Num_Apagado);
				/*Causa de reset === */
				j += sprintf(content + j, "\"rst\":\"%s\",", data.CR);
				j += sprintf(content + j, "\"mem\":\"%s\",", data.S_M);
				j += sprintf(content + j, "\"vsrc\":\"%d\",", data.vmcu);
				j += sprintf(content + j, "\"vmcu\":\"%d\"", data.vsrc);
            	break;

            default:
            	j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1, data.contador1);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2, data.contador2);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador3, data.contador3);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador4, data.contador4);
				j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador5, data.contador5);
            	break;
        }
        // Contadores adicionales opcionales
         //  j += sprintf(content+j, "\"handpay\":%d,", data.contador4);
         //  j += sprintf(content+j, "\"refill\":%d,", data.contador5);

    j += sprintf(content+j, "\"typ\":\"prg\",\"sta\":\"%s\"}\r\n" , data.sta );
    content[j] = '\0';
}

void CrearJson::JSON_Evento_INSTANTANEO(const JsonData& data, int Valor_Inst, char* content){ //Evento BET Instantaneo

	int j = 0;
	j  = sprintf(content, "{\"egm\":%lu,", data.egm);
	j += sprintf(content+j, "\"sec\":%lu,", data.sec);

	 switch(data.evt) {
	            case EventoTipo::Contador1:
	            	j += sprintf(content+j, "\"evt\":\"e%s\",", Nombre_Contador1); //
	            	j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador1,  Valor_Inst);
	            	break;

	            case EventoTipo::Contador2:
	            	j += sprintf(content+j, "\"evt\":\"e%s\",", Nombre_Contador2); //
	            	j += sprintf(content+j, "\"%s\":%d,", Nombre_Contador2,  Valor_Inst);
	            	break;

	            case EventoTipo::Contador3:
	            	j += sprintf(content+j, "\"evt\":\"e%s\",", Contador3_Inst); //
	                j += sprintf(content+j, "\"%s\":%d,", Contador3_Inst,  Valor_Inst);
	           	            	break;
	            default:
	            	break;

	 }

	j += sprintf(content+j,  "\"typ\":\"inc\"}\r\n"); // Progresivos
	content[j] = '\0'; // Null

	//size_t len = strlen(content);
	//Serial_Json->write(reinterpret_cast<uint8_t*>(content), len);
//	memset(content, 0, len); // opcional
}







// Función para convertir el mes de texto a número
int CrearJson::mes_a_numero(const char *mes) {
    if (strcmp(mes, "Jan") == 0) return 1;
    if (strcmp(mes, "Feb") == 0) return 2;
    if (strcmp(mes, "Mar") == 0) return 3;
    if (strcmp(mes, "Apr") == 0) return 4;
    if (strcmp(mes, "May") == 0) return 5;
    if (strcmp(mes, "Jun") == 0) return 6;
    if (strcmp(mes, "Jul") == 0) return 7;
    if (strcmp(mes, "Aug") == 0) return 8;
    if (strcmp(mes, "Sep") == 0) return 9;
    if (strcmp(mes, "Oct") == 0) return 10;
    if (strcmp(mes, "Nov") == 0) return 11;
    if (strcmp(mes, "Dec") == 0) return 12;
    return 0; // Error si el mes no es válido
}


int CrearJson::obtener_fecha_compilacion() {
    // `__DATE__` tiene formato: "Mes DD YYYY"
    char mes[4];
    int dia, anio;

    sscanf(__DATE__, "%s %d %d", mes, &dia, &anio);

    // Convertir mes a número
    int mes_num = mes_a_numero(mes);

    // Formatear la fecha como entero
    return (dia * 10000) + (mes_num * 100) + (anio % 100);
}




// Función para obtener la hora de compilación en formato entero
int CrearJson::obtener_hora_compilacion() {
    int hora, minuto, segundo;

    sscanf(__TIME__, "%d:%d:%d", &hora, &minuto, &segundo);

    return (hora * 10000) + (minuto * 100) + segundo;
}


int CrearJson::Recibe_JSON(uint8_t *JSON_datos_recibidos)
{
	    if (JSON_datos_recibidos[1] != 0x22) {
	        return Cadena_NO_valida;  // Debe empezar con "
	    }

	    const char* data = reinterpret_cast<const char*>(JSON_datos_recibidos);

	    // Tabla de patrones asociados a códigos
	    struct Pattern {
	        const char* key;
	        int code;
	    };

	    static const Pattern requestPatterns[] = {
	        {"rcho", Solicitud_Cash_Out},
	        {"rst1", Reset_Meters},
	        {"chinr", Solicitud_Cash_In},
	        {"conf", Configuracion_Tiempos},
	        {"edo", Solicitud_de_Estado},
	        {"ver", Solicitud_de_FW},
	        {"diag", Diagnostico}
	    };

	    // Verificamos primero si es un "req"
	    if (strstr(data, "req")) {
	        for (auto& p : requestPatterns) {
	            if (strstr(data, p.key)) {
	                return p.code;
	            }
	        }
	        return Cadena_NO_valida;
	    }

	    // Si no es "req", verificamos evento "evt"
	    if (strstr(data, "evt") && strstr(data, "sta") && strstr(data, "on")) {
	        return WEMOS_listo;
	    }

	    return Cadena_NO_valida;


}




int CrearJson::Decodifica_Solicitud_Cash_In( uint8_t *JSON_datos_recibidos, int& creditos, int& Tiempo_pulso)
{
	 if (JSON_datos_recibidos == nullptr)
	    {
	        return 0;
	    }

	    const char* json = (const char*)JSON_datos_recibidos;

	    if (!ExtraerEnteroJSON(json, "\"cred\":\"", creditos))
	    {
	        return 0;
	    }

	    if (!ExtraerEnteroJSON(json, "\"tpin\":\"", Tiempo_pulso))
	    {
	        return 0;
	    }

	    // Ajuste según tu lógica de negocio
	    Tiempo_pulso *= 10;

	    return 1;

}



//Evento de error
void CrearJson::JSON_Eve_Error(uint32_t& Contador_datos_enviados ,  char *content,const char evento ){ //Eventos de cashout  Progresivos
	int j = 0;
	j  = sprintf(content  , "{\"egm\":%d,", 1000);
	j += sprintf(content+j, "\"sec\":%d,", Contador_datos_enviados); //Guardar en memoria no volatil (flash)

	if(evento == 'o'){
		j += sprintf(content+j, "\"evt\":\"echo\","); // Evento Cash Out
	}else if (evento == 'i'){
		j += sprintf(content+j, "\"evt\":\"echi\","); // Evento Cash In
	}
	else if (evento == 'b'){
			j += sprintf(content+j, "\"evt\":\"ebet\","); // Evento Bet
		}
	else{ //c
		j += sprintf(content+j, "\"req\":\"econf\","); // Evento Configuración
	}

	j += sprintf(content+j, "\"sta\":\"fcho\"}\r\n"); // Evento Cash Out
	 content[j] = '\0'; // Null
}




//Estado listo para enviar pulsos "on"
void CrearJson::JSON_Eve_ON(uint32_t& Contador_datos_enviados ,  char *content){ //Eventos de cashout  Progresivos
	int j = 0;
	j  = sprintf(content  , "{\"egm\":%d,", 1000);
	j += sprintf(content+j, "\"sec\":%d,", Contador_datos_enviados); //Guardar en memoria no volatil (flash)

	j += sprintf(content+j, "\"sta\":\"on\"}\r\n"); // Evento listo para enviar pulsos
	 content[j] = '\0'; // Null
}




void CrearJson::crear_version_fw(char *version, size_t len) {

    int fecha = obtener_fecha_compilacion();

    int dia  = fecha / 10000;
    int mes  = (fecha / 100) % 100;
    int anio = fecha % 100;

    // Formato: "AA.MM.DV"  → ej: "25.11.7V"
    snprintf(version, len, "%02d.%02d.%dV", anio, mes, dia);
}

