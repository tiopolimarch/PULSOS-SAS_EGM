/*
 * flags.cpp
 *
 *  Created on: May 21, 2024
 *      Author: INICIO
 */

#include "flags.h"

Flags::Flags(int In_Meter, int BET_meter, int HopperMeter, int Refill , int HandPayMeter, int Botton_CashOut):
Flag_Total_In_Meter(In_Meter),
Flag_Total_BET_meter(BET_meter),
Flag_HopperMeter(HopperMeter),
Flag_Refill(Refill),
Flag_HandPayMeter(HandPayMeter),
Flag_Botton_CashOut(Botton_CashOut){};

Flags::~Flags() {
	// TODO Auto-generated destructor stub
}

