/*
 * flags.h
 *
 *  Created on: May 21, 2024
 *      Author: INICIO
 */

#ifndef SRC_FLAGS_H_
#define SRC_FLAGS_H_

class Flags {
public:
	volatile int Flag_Total_In_Meter;
	    volatile int Flag_Total_BET_meter;
	    volatile int Flag_HopperMeter;
	    volatile int Flag_Refill;
	    volatile int Flag_HandPayMeter;
	    volatile int Flag_Botton_CashOut;

	    Flags(int In_Meter, int BET_meter, int HopperMeter, int Refill , int HandPayMeter , int Botton_CashOut);



	virtual ~Flags();


};

#endif /* SRC_FLAGS_H_ */
