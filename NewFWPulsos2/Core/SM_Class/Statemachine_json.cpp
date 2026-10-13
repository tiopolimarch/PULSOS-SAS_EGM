/*
 * Statemachine_json.cpp
 *
 *  Created on: Sep 1, 2026
 *      Author: Saul Luna
 */




/*
 * StateMachine_Json.cpp
 *
 * Construcción y envío de mensajes JSON (eventos, errores, ON).
 * Separado de StateMachine.cpp — la declaración completa de la clase sigue
 * en StateMachine.h.
 *
 */

#include "../SM_Class/StateMachine.h"

JsonData StateMachine::CrearJsonData(EventoTipo tipo_evt,  const char* estado) {
    return JsonData{
        1000,
        data->Contador_datos_enviados,
        tipo_evt,
        estado,
        data->Total_In_Meter_prg,
        data->Total_BET_meter_prg,
        data->HopperMeter_prg,
        data->HandPayMeter_prg,
        data->Refill_prg,
		data->Causas_Reset,
		data->Status_memoria,
		data->temp_mcu_x10,
		data->Numero_Apagado_EGM,
		data->vmcu_mv,
		data->vsrc_mv
    };
}


void StateMachine::Envio_datos_JSON(){
	data->len2 =  strlen(data->json_progre);
	Serial->write(reinterpret_cast<uint8_t*>(data->json_progre), data->len2);
    memset(data->json_progre, 0, data->len2);
}


// Envía JSON de error en Cash Out
void StateMachine::SendCashOutError() {
	data->Contador_datos_enviados++;
    CrearJson_obj->JSON_Eve_Error(data->Contador_datos_enviados, data->json_progre, 'o');
    data->len2 = strlen(data->json_progre);
    Serial->write((uint8_t*)data->json_progre, (uint16_t)data->len2);

}


void StateMachine::ReportarErrorCashIn() {
	data->Contador_datos_enviados++;
    CrearJson_obj->JSON_Eve_Error(data->Contador_datos_enviados, data->json_progre, 'i');
    data->len2 = strlen(data->json_progre);
    Serial->write((uint8_t*)data->json_progre, (uint16_t)data->len2);
}


void StateMachine::enviarEventoOn() {
    CrearJson_obj->JSON_Eve_ON(data->Contador_datos_enviados, data->json_progre);
    data->len2 = strlen(data->json_progre);
    Serial->write((uint8_t*)data->json_progre, data->len2);
    bandera_on = 1;
}
