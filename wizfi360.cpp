/*
 *  wizfi360.cpp
 *
 *  Created on: Sep 8, 2020
 *  Author: NguyenPham
 */

#include "wizfi360.h"

//Set wifi SSID and PASSWORD here ||======> Change to use SMARTCONFIG
#define WIFI_SSID	"SSID"
#define WIFI_PASS	"Password"

//Enable/Disable debug via uart
#define debug 					true  //show raw tcp/ip data

//Change between HTTP and HTTPS server
#define HTTPS 					true

//Timeout config
#define SPI_RECV_TIMEOUT		500		//timeout wait for wizfi data
#define SPI_TIMEOUT				10		//timeout spi send

//Hex command for wizfi spi
#define SPI_REG_INT_STTS		0x06
#define SPI_REG_RX_DAT_LEN		0x02
#define SPI_REG_TX_BUFF_AVAIL	0x03

#define SPI_CMD_RX_DATA			0x10
#define SPI_CMD_TX_CMD			0x91
#define SPI_CMD_TX_DATA			0x90

//enable/disable chip select pin
#define SPI_CS_OFF				HAL_GPIO_WritePin(WIZ_CS2_GPIO_Port, WIZ_CS2_Pin, GPIO_PIN_RESET)
#define SPI_CS_ON				HAL_GPIO_WritePin(WIZ_CS2_GPIO_Port, WIZ_CS2_Pin, GPIO_PIN_SET)

//interrupt pin
#define SPI_INT_STTS			HAL_GPIO_ReadPin(WIZ_INT_GPIO_Port, WIZ_INT_Pin)

extern bool Spi_rx_flag;
SPI_HandleTypeDef hspi2;

#if debug
	UART_HandleTypeDef huart2; //change uart
	uint8_t buffer_uart[768];
#endif

//SPI buffer
uint8_t SPI_TX_BUFF[768]; //1024
uint8_t SPI_RX_BUFF[768]; //1024

bool connection_response = false;
bool tcp_already_response = false;

bool WIZFI360::init()
{
	bool status = false;
	//reset wizfi
	reset();

	//get wizfi data
	while (Spi_rx_flag||(SPI_INT_STTS == 0)){
		dataParsing(Start,0,0);
		osDelay(1);
	}

	//set wizfi station mode
	status = sendAndReceiveData(Station_mode,0,0);
	if (status == true)
	{
		return true;
	}
	else
		return false;
}

bool WIZFI360::checkConnection()
{
	bool status = false;

	//check Connection_status
	status = sendAndReceiveData(Connection_status,0,0);
	if (status == true)
	{
		return true;
	}
	else
		return false;
}

bool WIZFI360::enableSmartconfig()
{
	bool status = false;
	status = sendAndReceiveData(Enable_smartconfig,0,0);
	if (status == true)
	{
		return true;
	}
	else
		return false;
}


//reset wizfi
void WIZFI360::reset()
{
	#if debug
	  snprintf((char*)buffer_uart, sizeof buffer_uart,"======Reset wizfi======\r\n");//r
	  HAL_UART_Transmit(&huart2,buffer_uart, sizeof buffer_uart,HAL_MAX_DELAY);
	  memset(buffer_uart, 0, sizeof(buffer_uart));
	#endif
	HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_RESET);
	osDelay(100);
	HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_SET);
	osDelay(5000); //long time wait for wizfi first coming data
}


//Send cmd and receive data
bool WIZFI360::sendAndReceiveData(enum cmd_type type,int data_len, char* cmd_data)
{
	int len = 0;
	bool status = false;
	uint8_t cmd_buff[1024];
	uint8_t type_cmd = 0;
	osDelay(2);

	if(type == AT_test)
	{
		len = sprintf((char*)cmd_buff,"AT\r\n");
	}
	else if(type == Connection_status)
	{
		len = sprintf((char*)cmd_buff,"AT+CIPSTATUS\r\n");
	}
	else if(type == Station_mode)
	{
		len = sprintf((char*)cmd_buff,"AT+CWMODE_CUR=1\r\n");
	}
	else if(type == Connect_wifi)
	{
		len = sprintf((char*)cmd_buff,"AT+CWJAP_CUR=\"%s\",\"%s\"\r\n",WIFI_SSID,WIFI_PASS);
	}
	else if(type == Disconnect_wifi)
	{
		len = sprintf((char*)cmd_buff,"AT+CWQAP\r\n");
	}
	else if(type == IP_address)
	{
		len = sprintf((char*)cmd_buff,"AT+CIPSTA_CUR?\r\n");
		//len = sprintf((char*)cmd_buff,"AT+CIPSTO?\r\n");
	}
	else if(type == Connect_to_server)
	{
		#if HTTPS
			len = sprintf((char*)cmd_buff,"AT+CIPSTART=\"SSL\",\"%s\",443,20\r\n",SERVER_URL); //for https
		#else
			len = sprintf((char*)cmd_buff,"AT+CIPSTART=\"TCP\",\"%s\",80\r\n",SERVER_URL); //for http
		#endif
	}
	else if(type == Disconnect_to_server)
	{
		len = sprintf((char*)cmd_buff,"AT+CIPCLOSE\r\n");
	}
	else if(type == Enable_smartconfig)
	{
		len = sprintf((char*)cmd_buff,"AT+CWSTARTSMART\r\n");
	}
	else if(type == Disable_smartconfig)
	{
		len = sprintf((char*)cmd_buff,"AT+CWSTOPSMART\r\n");
	}
	else if(type == Send_data_len)
	{
		len = sprintf((char*)cmd_buff,"AT+CIPSEND=%d\r\n",data_len);
	}
	else if((type == Get_date)||(type == Get_token)||(type == Check_config)||(type == Update_config)||(type == Update_log))
	{
		len = sprintf((char*)cmd_buff, cmd_data);
		type_cmd = 1;
	}

	//send data via SPI
	status = spiSend(type_cmd, cmd_buff, len);
	osDelay(5);
	uint16_t time_out = 0, error = 0;

	//if send SUCCESS => try to get data from wizfi
	if (status == true)
	{
		int checkData_status = 0;
		connection_response = false; //false = OK
		tcp_already_response = false;

		while ((SPI_INT_STTS == 0)||(checkData_status == 0))
		{
			//osMutexWait(mutexWizfiHandle, osWaitForever);
			checkData_status = dataParsing(type,cmd_buff,len);
			//osMutexRelease(mutexWizfiHandle);
			osDelay(10);
			time_out++;
			if (time_out >= SPI_RECV_TIMEOUT)
			{
				error = 1;
				break;
			}
			if ((checkData_status != 0)&&((type == Send_data_len)||(type == Get_date)||(type == Get_token)||(type == Check_config)||(type == Update_config)||(type == Update_log)))
				break;
		}

		if ((checkData_status == 1)&&(error == 0))
			return true;
		else
			return false;
	}
	else
		return false;
}


//read register when send or receive data
int WIZFI360::spiReadRegister(uint8_t CMD)
{
	uint8_t dum = 0xFF, dum2=0x00, rx_temp[2];
	uint16_t ret = 0;
	HAL_SPI_TransmitReceive(&hspi2, &CMD, &dum2, 1, 10);
	dum = 0xFF;
	HAL_SPI_TransmitReceive(&hspi2, &dum, rx_temp, 2, 10);
	ret = rx_temp[0] | (rx_temp[1] << 8);
	return ret;
}

//SPI SEND Message
bool WIZFI360::spiSend(uint8_t type, uint8_t *data, uint16_t len)
{
	//read TX_BUFF_AVAIL
	uint8_t temp_CMD, retry = 0, err = 0, dum2=0x00;
	uint16_t TX_len = 0, SPI_RX_REG = 0;
	memset(SPI_TX_BUFF,0, sizeof(SPI_TX_BUFF));
	memset(SPI_RX_BUFF,0, sizeof(SPI_RX_BUFF));
	osDelay(1);
	temp_CMD = SPI_REG_TX_BUFF_AVAIL;
	SPI_CS_OFF;
	while((SPI_RX_REG != 0xffff) && (0 == (SPI_RX_REG & 0x02)))
	{
		retry++;

	    SPI_RX_REG = spiReadRegister(temp_CMD);
		SPI_CS_ON;
		osDelay(300);
	    if(retry > SPI_TIMEOUT)
	    {
	      retry = 0;
	      err = 1;
	      break;
	    }
	}

	TX_len = len + 2;
	if(TX_len % 4){
		TX_len = ((TX_len + 3)/4) << 2;
	}

	if(err==0)
	{
		SPI_CS_OFF;
		if(type){
	      temp_CMD = SPI_CMD_TX_DATA;
	    }
	    else{
	      temp_CMD = SPI_CMD_TX_CMD;
	    }

	    HAL_SPI_TransmitReceive(&hspi2, &temp_CMD, &dum2, 1, 10);
	    memcpy(SPI_TX_BUFF , &len, sizeof(len));
	    memcpy(SPI_TX_BUFF + 2, data, len);
	    //HAL_SPI_TransmitReceive(&hspi2, SPI_TX_BUFF, RX_BUFF, TX_len, 10);
	    HAL_SPI_TransmitReceive(&hspi2, SPI_TX_BUFF, SPI_RX_BUFF, TX_len, 10);
	    SPI_CS_ON;
	}

	if (err == 0)
		return true;
	else
		return false;
}

// SPI Receive
int WIZFI360::spiReceive()
{
	uint8_t temp_CMD, dum = 0xFF, dum2=0x00;
	uint16_t SPI_RX_REG = 0;
	//SPI_RX_REG = 0;
	memset(SPI_RX_BUFF, 0, sizeof(SPI_RX_BUFF));
	osDelay(1);

	SPI_CS_OFF;
	temp_CMD = SPI_REG_INT_STTS;
	SPI_RX_REG = spiReadRegister(temp_CMD);
	SPI_CS_ON;

	if(SPI_RX_REG == 0)
		return 0;
	if((SPI_RX_REG != 0xffff) && (SPI_RX_REG & 0x01))
	{
		//if(SPI_RX_REG == 0x0001){
		SPI_CS_OFF;
		temp_CMD = SPI_REG_RX_DAT_LEN;
		SPI_RX_REG = spiReadRegister(temp_CMD);
		SPI_CS_ON;
	}

	if(SPI_RX_REG > 0)
	{
		SPI_CS_OFF;
		temp_CMD = SPI_CMD_RX_DATA;
		HAL_SPI_TransmitReceive(&hspi2, &temp_CMD, &dum2, 1, 10);
		HAL_SPI_TransmitReceive(&hspi2, &dum, SPI_RX_BUFF, SPI_RX_REG, 10);
		SPI_RX_BUFF[SPI_RX_REG] = 0;
		SPI_CS_ON;

		return SPI_RX_REG;
	}
	else
		return 0;
}

