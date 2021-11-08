/*
 *  wizfi360.h
 *
 *  Created on: Sep 8, 2020
 *  Author: NguyenPham
 */

#ifndef WIZFI360_H_
#define WIZFI360_H_

#include "stm32l0xx_hal.h"
#include "main.h"
#include "rtos.h"

#include <stdio.h>
#include <stdint.h>

//type of command
typedef enum cmd_type
{
	Start,
	AT_test,
	Connection_status,
	Station_mode,
	Connect_wifi,
	Disconnect_wifi,
	IP_address,
	Connect_to_server,
	Disconnect_to_server,
	Enable_smartconfig,
	Wait_smartconfig,
	Disable_smartconfig,
	Send_data_len,
	Get_date,
	Get_token,
	Check_config,
	Update_config,
	Update_log
}cmd_type_t;

class WIZFI360
{
public:
    bool init(); 		//start wizfi

    bool checkConnection(); //check wifi connection true = connected | false = disconnected
    bool enableSmartconfig();

private:
    void reset(); //reset wizfi
    bool sendAndReceiveData(enum cmd_type type,int data_len, char* cmd_data); //send and recive data
    bool spiSend(uint8_t type, uint8_t *data, uint16_t len); //spi send
    int spiReadRegister(uint8_t CMD); //read register
    int spiReceive(); //spi receive
};


#endif /* WIZFI360_H_ */
