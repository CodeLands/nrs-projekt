/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
#include "stm32f3xx_hal.h"

#include <stdio.h>
#include <string.h>
#include <math.h>  // For fabsf and fmaxf
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBUG 1

#ifdef DEBUG
#define LED_PIN_GYRO GPIO_PIN_11
#define LED_PIN_ACCEL GPIO_PIN_12
#define LED_PIN_MAGNET GPIO_PIN_13
#define LED_PIN_UART GPIO_PIN_14
#define LED_PIN_SEND_MODE GPIO_PIN_15

#define LED_PORT GPIOC
#endif

// Response Statuses
#define TIMEOUT 0
#define SUCCESS 1
#define ERROR 2
#define WAITING 3
#define IDLE 4
#define SEND_REQUEST 5

// Setup Stages
#define AT_TEST 0
#define AT_SET_CONNECT_MODE 1
#define AT_SET_MAX_CONNECTIONS 2
#define AT_START_SERVER 3
#define AT_SEND_HTML_HEADER 4
#define AT_SEND_HTML 5
#define AT_SEND_CONNECT_REQUEST 6

#define HEADER_MAG 0xAAAB
#define HEADER_ACC 0xBBBB
#define HEADER_GYR 0xCCCC
#define BUFFER_SIZE 64

#define ENABLE_MAGNETOMETER 1
#define ENABLE_ACCELEROMETER 1
#define ENABLE_GYROSCOPE 1

#define MODE_NONE 0
#define MODE_BINARY_UART 1
#define MODE_ASCII_UART 2
#define MODE_BINARY_CDC 3
#define MODE_ASCII_CDC 4

#define RX_BUFFER_SIZE 2048 * 4

//#define APP_RX_DATA_SIZE 2048
//#define APP_TX_DATA_SIZE 2048
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
volatile uint8_t data_ready_mag = 0;
volatile uint8_t data_ready_acc = 0;
volatile uint8_t data_ready_gyr = 0;
volatile uint16_t packet_number = 0;

volatile uint8_t transmission_mode = MODE_NONE;

volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
volatile uint8_t last_buffer[RX_BUFFER_SIZE];
volatile uint16_t rx_index = 0;
//volatile uint8_t rx_data_ready = 0;

volatile uint32_t button_press_start = 0;
volatile uint8_t button_pressed = 0;
volatile uint8_t button_action_pending = 0;
volatile uint8_t button_action_type = 0; // 0 for short press, 1 for long press

volatile uint8_t setup_stage = AT_TEST;
volatile uint8_t response_status = IDLE;
volatile uint8_t has_response_changed = 0;
volatile uint32_t tick_when_sent = 0; // Added to track response timing

volatile uint8_t is_client_connected = 0;
volatile uint8_t was_client_connected = 0;

volatile uint8_t is_client_requesting_page = 0;
volatile uint8_t was_client_requesting_page = 0;

uint8_t connection_established = 0;

//#define RX_TIMEOUT_MS 1000 // Timeout for data reception in milliseconds

volatile uint32_t last_rx_tick = 0;  // Time of the last received byte
volatile uint8_t receiving_data = 0; // Flag to indicate ongoing reception

const char *html_page2 =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 253\r\n\r\n<!DOCTYPE html><html><head><title>Wi-Fi Config</title></head><body><form method=\"GET\" action=\"/\" accept-charset=\"utf-8\">SSID: <input type=\"text\" name=\"ssid\"><br>Password: <input type=\"text\" name=\"password\"><br><input type=\"submit\" value=\"Submit\"></form></body></html>\r\n";

const char *html_page = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 45\r\n\r\n<html><body><h1>Hello, World!</h1></body></html>\r\n";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void Init_All_Sensors(void);
void Pack_Data(uint8_t *binary_buffer, uint16_t header, int16_t x, int16_t y, int16_t z);
void Transmit_Data_ASCII(const char *sensor_label, float x, float y, float z);
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);
uint8_t Pisi_Register(uint8_t device, uint8_t reg, uint8_t value);
void Beri_Registre(uint8_t device, uint8_t reg, uint8_t* data, uint8_t length);
float Convert_To_Gauss(int16_t raw_value);
void Verify_Sensors(void);
void Clear_Interrupts(void);
void Send_Command(const char* cmd);
void Change_Response_Status(uint8_t new_status);
void Send_Data_To_Server(const char *json_data);

#if ENABLE_MAGNETOMETER
void Handle_Magnetometer(void);
#endif
#if ENABLE_ACCELEROMETER
void Handle_Accelerometer(void);
#endif
#if ENABLE_GYROSCOPE
void Handle_Gyroscope(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Sensor initialization */
void Init_All_Sensors(void) {
    #if ENABLE_MAGNETOMETER
    // Initialize magnetometer
    Pisi_Register(0x1E, 0x60, 0x8C);  // CFG_REG_A: Enable XYZ, 20 Hz
    Pisi_Register(0x1E, 0x61, 0x00);  // CFG_REG_B: ±50 gauss
    Pisi_Register(0x1E, 0x62, 0x01);  // CFG_REG_C: DRDY interrupt
    HAL_Delay(10);
    #endif

    #if ENABLE_ACCELEROMETER
	Pisi_Register(0x19, 0x20, 0x47); // CTRL_REG1_A: ODR=50Hz, enable XYZ
	Pisi_Register(0x19, 0x23, 0x18); // CTRL_REG4_A: Full-scale ±4g, High resolution
	Pisi_Register(0x19, 0x22, 0x10); // CTRL_REG3_A: Enable INT1 for data ready
	Pisi_Register(0x19, 0x30, 0x00); // INT1_CFG_A: OR combination of events

	HAL_Delay(10);
    #endif

    #if ENABLE_GYROSCOPE
    // Initialize gyroscope
    spi1_pisiRegister(0x20, 0x80);  // Soft reset
    HAL_Delay(100);
    spi1_pisiRegister(0x20, 0x7F);  // CTRL1: Enable XYZ, 200Hz
    spi1_pisiRegister(0x22, 0x08);  // CTRL3: Enable data ready on INT2
    spi1_pisiRegister(0x23, 0x10);  // CTRL4: ±500dps range
    HAL_Delay(10);
    #endif
}

/* Verify Sensor Communication */
void Verify_Sensors(void) {
    uint8_t who_am_i = 0;

    #if ENABLE_MAGNETOMETER
    Beri_Registre(0x1E, 0x4F, &who_am_i, 1); // Read WHO_AM_I register
    if (who_am_i != 0x6E) {
        CDC_Transmit_FS((uint8_t *)"Magnetometer communication failed\n", 34);
    } else {
        CDC_Transmit_FS((uint8_t *)"Magnetometer initialized\n", 25);
    }
    HAL_Delay(10);
    #endif

    #if ENABLE_ACCELEROMETER
    Beri_Registre(0x19, 0x0F, &who_am_i, 1); // Read WHO_AM_I register
    if (who_am_i != 0x33) {
        CDC_Transmit_FS((uint8_t *)"Accelerometer communication failed\n", 36);
    } else {
        CDC_Transmit_FS((uint8_t *)"Accelerometer initialized\n", 27);
    }
    HAL_Delay(10);
    #endif

    #if ENABLE_GYROSCOPE
    Beri_Registre(0x6B, 0x0F, &who_am_i, 1); // Read WHO_AM_I register
    if (who_am_i != 0xD4) {
        CDC_Transmit_FS((uint8_t *)"Gyroscope communication failed\n", 32);
    } else {
        CDC_Transmit_FS((uint8_t *)"Gyroscope initialized\n", 23);
    }
    HAL_Delay(10);
    #endif
}

/* Clear Interrupt Flags */
void Clear_Interrupts(void) {
    uint8_t dummy[6];

    #if ENABLE_MAGNETOMETER
    Beri_Registre(0x1E, 0x68, dummy, 6); // Clear magnetometer DRDY
    #endif

    #if ENABLE_ACCELEROMETER
    Beri_Registre(0x19, 0x28 | 0x80, dummy, 6); // Clear accelerometer interrupt
    #endif

    #if ENABLE_GYROSCOPE
    Beri_Registre(0x6B, 0x28 | 0x80, dummy, 6); // Clear gyroscope interrupt
    #endif
}

void pavza(){
  uint32_t counter = 0;
  for(counter=0; counter<600; counter++){
    asm("nop");
  }
}

uint8_t spi1_beriRegister(uint8_t reg)
{
  uint16_t buf_out, buf_in;
  reg |= 0x80;  // Set read bit
  buf_out = reg;
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);  // CS low
  pavza();
  HAL_SPI_TransmitReceive(&hspi1, &((uint8_t*)&buf_out)[0], &((uint8_t*)&buf_in)[0], 1, 2);
  pavza();
  HAL_SPI_TransmitReceive(&hspi1, &((uint8_t*)&buf_out)[1], &((uint8_t*)&buf_in)[1], 1, 2);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);  // CS high
  pavza();
  return buf_in >> 8;
}

void spi1_pisiRegister(uint8_t reg, uint8_t vrednost)
{
  uint16_t buf_out;
  buf_out = reg | (vrednost<<8);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
  pavza();
  HAL_SPI_Transmit(&hspi1, &((uint8_t*)&buf_out)[0], 1, 2);
  pavza();
  HAL_SPI_Transmit(&hspi1, &((uint8_t*)&buf_out)[1], 1, 2);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
  pavza();
}

void spi1_beriRegistre(uint8_t reg, uint8_t* buffer, uint8_t velikost)
{
  reg |= 0xC0;
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
  pavza();
  HAL_SPI_Transmit(&hspi1, &reg, 1, 10);
  pavza();
  HAL_SPI_Receive(&hspi1, buffer, velikost, velikost);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
  pavza();
}

/* I2C helper functions */
uint8_t Pisi_Register(uint8_t device, uint8_t reg, uint8_t value) {
    device <<= 1;
    return HAL_I2C_Mem_Write(&hi2c1, device, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, HAL_MAX_DELAY);
}

void Beri_Registre(uint8_t device, uint8_t reg, uint8_t* data, uint8_t length) {
    if (length > 1) {
        reg |= 0x80; // Auto-increment for magnetometer
    }
    device <<= 1;
    HAL_I2C_Mem_Read(&hi2c1, device, reg, I2C_MEMADD_SIZE_8BIT, data, length, HAL_MAX_DELAY);
}

float Convert_To_Gauss(int16_t raw_value) {
    return raw_value * (50.0f / 32768.0f);
}

/* Data packing function */
void Pack_Data(uint8_t *binary_buffer, uint16_t header, int16_t x, int16_t y, int16_t z) {
    binary_buffer[0] = header & 0xFF;
    binary_buffer[1] = (header >> 8) & 0xFF;
    binary_buffer[2] = packet_number & 0xFF;
    binary_buffer[3] = (packet_number >> 8) & 0xFF;
    binary_buffer[4] = x & 0xFF;
    binary_buffer[5] = (x >> 8) & 0xFF;
    binary_buffer[6] = y & 0xFF;
    binary_buffer[7] = (y >> 8) & 0xFF;
    binary_buffer[8] = z & 0xFF;
    binary_buffer[9] = (z >> 8) & 0xFF;
}

/* ASCII transmission function */
void Transmit_Data_ASCII(const char *sensor_label, float x, float y, float z) {
	 static uint32_t last_send_time = 0;
	 const uint32_t MIN_SEND_INTERVAL = 100;

    char ascii_buffer[BUFFER_SIZE];
    snprintf(ascii_buffer, BUFFER_SIZE, "{\"%s\":%d,\"X\":%.3f,\"Y\":%.3f,\"Z\":%.3f}",
             sensor_label, packet_number, x, y, z);
    if (transmission_mode == MODE_ASCII_UART) {
    	// Pošlji na UART
        // HAL_UART_Transmit(&huart2, (uint8_t *)ascii_buffer, strlen(ascii_buffer), HAL_MAX_DELAY);
        // pošiljanje podatkov ESP-ju
       	if (connection_established) {
			// Preveri časovni interval
			uint32_t current_time = HAL_GetTick();
			if (current_time - last_send_time >= MIN_SEND_INTERVAL) {
				Send_Data_To_Server(ascii_buffer);
				last_send_time = current_time;
			}
       	}
    } else if (transmission_mode == MODE_ASCII_CDC) {
        CDC_Transmit_FS((uint8_t *)ascii_buffer, strlen(ascii_buffer));
    }
}

#if ENABLE_MAGNETOMETER
/* Magnetometer handler */
void Handle_Magnetometer(void) {
    data_ready_mag = 0;

    int16_t raw_data[3];
    Beri_Registre(0x1E, 0x68, (uint8_t*)raw_data, 6);
    Clear_Interrupts(); // Clear interrupt flags

    if (transmission_mode == MODE_BINARY_UART || transmission_mode == MODE_BINARY_CDC) {
        uint8_t binary_buffer[10];
        Pack_Data(binary_buffer, HEADER_MAG, raw_data[0], raw_data[1], raw_data[2]);
        if (transmission_mode == MODE_BINARY_UART) {
            HAL_UART_Transmit(&huart2, binary_buffer, 10, HAL_MAX_DELAY);
        } else {
            CDC_Transmit_FS(binary_buffer, 10);
        }
    } else if (transmission_mode == MODE_ASCII_UART || transmission_mode == MODE_ASCII_CDC) {
        float x = Convert_To_Gauss(raw_data[0]);
        float y = Convert_To_Gauss(raw_data[1]);
        float z = Convert_To_Gauss(raw_data[2]);
        Transmit_Data_ASCII("MAG", x, y, z);
    }
    packet_number++;
}
#endif

#if ENABLE_ACCELEROMETER
/* Accelerometer handler */
void Handle_Accelerometer(void) {
    data_ready_acc = 0;

    int16_t raw_data[3];
    Beri_Registre(0x19, 0x28 | 0x80, (uint8_t*)raw_data, 6);
    Clear_Interrupts(); // Clear interrupt flags

    if (transmission_mode == MODE_BINARY_UART || transmission_mode == MODE_BINARY_CDC) {
        uint8_t binary_buffer[10];
        Pack_Data(binary_buffer, HEADER_ACC, raw_data[0], raw_data[1], raw_data[2]);
        if (transmission_mode == MODE_BINARY_UART) {
            HAL_UART_Transmit(&huart2, binary_buffer, 10, HAL_MAX_DELAY);
        } else {
            CDC_Transmit_FS(binary_buffer, 10);
        }
    } else if (transmission_mode == MODE_ASCII_UART || transmission_mode == MODE_ASCII_CDC) {
        float x = raw_data[0] * (4.0f / 32768.0f);
        float y = raw_data[1] * (4.0f / 32768.0f);
        float z = raw_data[2] * (4.0f / 32768.0f);
        Transmit_Data_ASCII("ACC", x, y, z);
    }
    packet_number++;
}
#endif

#if ENABLE_GYROSCOPE
void Handle_Gyroscope(void) {
    data_ready_gyr = 0;

    int16_t raw_data[3];
    spi1_beriRegistre(0x28 | 0x80, (uint8_t*)raw_data, 6);
    Clear_Interrupts();

    if (transmission_mode == MODE_BINARY_UART || transmission_mode == MODE_BINARY_CDC) {
        uint8_t binary_buffer[10];
        Pack_Data(binary_buffer, HEADER_GYR, raw_data[0], raw_data[1], raw_data[2]);
        if (transmission_mode == MODE_BINARY_UART) {
            HAL_UART_Transmit(&huart2, binary_buffer, 10, HAL_MAX_DELAY);
        } else {
            CDC_Transmit_FS(binary_buffer, 10);
        }
    } else if (transmission_mode == MODE_ASCII_UART || transmission_mode == MODE_ASCII_CDC) {
        float x = raw_data[0] * (500.0f / 32768.0f);
        float y = raw_data[1] * (500.0f / 32768.0f);
        float z = raw_data[2] * (500.0f / 32768.0f);
        Transmit_Data_ASCII("GYR", x, y, z);
    }
    packet_number++;
}
#endif

void Change_Response_Status(uint8_t new_status) {
	if (new_status < 0 || new_status > 5) {
/*#ifdef DEBUG
	    CDC_Transmit_FS((uint8_t *)"new_status is invalid...\r\n", 26);
	    HAL_Delay(10);
#endif*/
	    return;
	}
	has_response_changed = 1;
	response_status = new_status;
}

void Clear_RX_Buffer() {
    HAL_UART_AbortReceive_IT(&huart2);  // Stop any ongoing reception
    memset((void*)rx_buffer, 0, RX_BUFFER_SIZE);
    rx_index = 0;
    HAL_UART_Receive_IT(&huart2, &rx_buffer[0], 1);
#ifdef DEBUG
    //CDC_Transmit_FS((uint8_t*)"Cleaned Buffer\r\n", 16);
    //HAL_Delay(10);
#endif
}

void Send_Command(const char* cmd) {
	HAL_UART_AbortReceive_IT(&huart2);
    Clear_RX_Buffer();
    tick_when_sent = HAL_GetTick();
	Change_Response_Status(WAITING);
    HAL_UART_Transmit(&huart2, (uint8_t*)cmd, strlen(cmd), HAL_MAX_DELAY);
    HAL_UART_Receive_IT(&huart2, &rx_buffer[rx_index], 1);
}

uint8_t Is_Timedout(uint32_t timeout_ms) {
	uint32_t current_tick = HAL_GetTick();

	if ((current_tick - tick_when_sent) > timeout_ms) {
	    return 1; // Timed out
	}
	return 0;
}

void Configure_ESP_As_Access_Point(void) {
    switch(setup_stage) {
        case AT_TEST: // AT Test
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending AT Test command\r\n", 25);
            HAL_Delay(10);
#endif
            Send_Command("AT\r\n\0");
            break;

        case AT_SET_CONNECT_MODE:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending AT set HotSpot and Connect mode command\r\n", 49);
            HAL_Delay(10);
#endif
            Send_Command("AT+CWMODE=3\r\n\0");
            break;

        case AT_SET_MAX_CONNECTIONS:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending AT set max connections command\r\n", 40);
            HAL_Delay(10);
#endif
            Send_Command("AT+CIPMUX=1\r\n\0");
            break;

        case AT_START_SERVER:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending AT start server command\r\n", 33);
            HAL_Delay(10);
#endif
            Send_Command("AT+CIPSERVER=1,80\r\n\0");
            break;

        case AT_SEND_HTML_HEADER:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending HTML Header\r\n", 21);
            HAL_Delay(10);
#endif
            Send_HTML_Header();
            break;

        case AT_SEND_HTML:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"Sending HTML\r\n", 14);
            HAL_Delay(10);
#endif
            Send_Command(html_page2);
            break;

        default:
#ifdef DEBUG
            CDC_Transmit_FS((uint8_t *)"That Setup Stage not implemented yet\r\n", 38);
            HAL_Delay(10);
#endif
            break;
    }
    HAL_Delay(10);
}

void Send_HTML_Header() {
    Send_Command("AT+CIPSEND=0,334\r\n");
}

void Send_Connect_Request(char *ssid, char *password) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    Send_Command(cmd);
}

void Handle_Client_Request(void) {
    if (strstr((char *)rx_buffer, "GET /?ssid=")) {
        char *ssid_start = strstr((char *)rx_buffer, "ssid=") + 5;
        char *password_start = strstr((char *)rx_buffer, "password=") + 9;

        char ssid[32] = {0};
        char password[64] = {0};

        char *end_ssid = strstr(ssid_start, "&");
        if (end_ssid) {
            strncpy(ssid, ssid_start, end_ssid - ssid_start);
        } else {
            strncpy(ssid, ssid_start, sizeof(ssid) - 1);
        }

        char *end_password = strstr(password_start, " ");
        if (end_password) {
            strncpy(password, password_start, end_password - password_start);
        } else {
            strncpy(password, password_start, sizeof(password) - 1);
        }

        #ifdef DEBUG // Debug: Print extracted SSID and password
        char debug_msg[128];
        snprintf(debug_msg, sizeof(debug_msg), "SSID: %s, Password: %s\r\n", ssid, password);
        CDC_Transmit_FS((uint8_t *)debug_msg, strlen(debug_msg));
        HAL_Delay(10);
        #endif

        CDC_Transmit_FS("Sending Connect to WiFi with SSID Command\r\n", 43);
        HAL_Delay(10);

        Clear_RX_Buffer(); // Clear the buffer after handling

        Send_Connect_Request(ssid, password);
    }
}

void Log_Response_Status_Change() {
	if (response_status == TIMEOUT)
		CDC_Transmit_FS((uint8_t *)"Response status changed to: TIMEOUT\r\n\r\n", 39);
	else if (response_status == SUCCESS)
		CDC_Transmit_FS((uint8_t *)"Response status changed to: SUCCESS\r\n", 37);
	else if (response_status == ERROR)
		CDC_Transmit_FS((uint8_t *)"Response status changed to: ERROR\r\n", 35);
	else if (response_status == WAITING)
		CDC_Transmit_FS((uint8_t *)"Response status changed to: WAITING\r\n", 37);
	else if (response_status == IDLE)
		CDC_Transmit_FS((uint8_t *)"Response status changed to: IDLE\r\n\r\n", 36);

    HAL_Delay(10);
}

uint8_t Has_Response_Finished(){
#ifdef DEBUG
	if (has_response_changed == 1){
		Log_Response_Status_Change();
		has_response_changed = 0;
	}
#endif
	if (response_status >= TIMEOUT && response_status < WAITING) {
		return 1;
	}
	return 0;
}

void Log_Setup_Stage_Change() {
	if (setup_stage == AT_TEST)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_TEST\r\n", 33);
	else if (setup_stage == AT_SET_CONNECT_MODE)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_SET_CONNECT_MODE\r\n", 45);
	else if (response_status == AT_SET_MAX_CONNECTIONS)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_SET_MAX_CONNECTIONS\r\n", 48);
	else if (response_status == AT_START_SERVER)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_START_SERVER\r\n", 41);
	else if (response_status == AT_SEND_HTML_HEADER)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_SEND_HTML_HEADER\r\n", 45);
	else if (response_status == AT_SEND_HTML)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_SEND_HTML\r\n", 38);
	else if (response_status == AT_SEND_CONNECT_REQUEST)
		CDC_Transmit_FS((uint8_t *)"Setup stage changed to: AT_SEND_CONNECT_REQUEST\r\n", 49);

    HAL_Delay(10);
}

void Set_Setup_Stage(uint8_t new_stage) {
	if (new_stage < 0 || new_stage > 6) {
#ifdef DEBUG
	    CDC_Transmit_FS((uint8_t *)"new_stage is invalid...\r\n", 25);
	    HAL_Delay(10);
#endif
	    return;
	}
	setup_stage = new_stage;
#ifdef DEBUG
	Log_Setup_Stage_Change();
#endif
}

void Log_Uart_Response() {
#ifdef DEBUG
    CDC_Transmit_FS((uint8_t *)"===ESP_RESPONSE===\r\n", 20);
    HAL_Delay(10);
    CDC_Transmit_FS((uint8_t *)rx_buffer, strlen((char *)rx_buffer));
    HAL_Delay(10);
    CDC_Transmit_FS((uint8_t *)"===ESP_RESPONSE_END===\r\n", 24);
    HAL_Delay(10);
#endif
}

void Handle_Response() {

#ifdef DEBUG
	if (response_status == SUCCESS || response_status == ERROR) {
        //Log_Uart_Response();

    	/*if (response_status == SUCCESS)
    		CDC_Transmit_FS((uint8_t *)"=ESP_RESPONSE: SUCCESS...\r\n", 27);
    	else //if (response_status == ERROR) {
    		CDC_Transmit_FS((uint8_t *)"=ESP_RESPONSE: ERROR...\r\n", 25);*/
	} else {
		CDC_Transmit_FS((uint8_t *)"=ESP_RESPONSE: TIMEOUT...\r\n", 27);

	}
	HAL_Delay(10);

#endif

    if (response_status != SUCCESS) {
        Clear_RX_Buffer();
        Change_Response_Status(IDLE);
        return;
    }

    switch(setup_stage) {
        case AT_TEST:
            Set_Setup_Stage(AT_SET_CONNECT_MODE);
            break;
        case AT_SET_CONNECT_MODE:
            Set_Setup_Stage(AT_SET_MAX_CONNECTIONS);
            break;
        case AT_SET_MAX_CONNECTIONS:
            Set_Setup_Stage(AT_START_SERVER);
            break;
        case AT_START_SERVER:
            Set_Setup_Stage(AT_SEND_HTML_HEADER);
            break;
        case AT_SEND_HTML_HEADER:
            Set_Setup_Stage(AT_SEND_HTML);
            break;
        case AT_SEND_HTML:
            Set_Setup_Stage(AT_SEND_HTML_HEADER);
            break;
        /*case AT_SEND_CONNECT_REQUEST:
            if (strstr((char *)rx_buffer, "WIFI CONNECTED")) {
                CDC_Transmit_FS((uint8_t *)"Wi-Fi Connected Successfully\r\n", 31);
            } else if (strstr((char *)rx_buffer, "WIFI DISCONNECT")) {
                CDC_Transmit_FS((uint8_t *)"Failed to Connect to Wi-Fi\r\n", 29);
            }
            Set_Setup_Stage(AT_SET_CONNECT_MODE);
            break;*/

        default:
            CDC_Transmit_FS((uint8_t *)"RESPONSE_NOT_IMPLEMENTED: Unknown Setup Stage\r\n", 47);
            HAL_Delay(10);
            break;
    }

    Clear_RX_Buffer(); // Then clear it after we’re done
    Change_Response_Status(IDLE);
}

void Log_Client_Status_Change() {
	if (was_client_connected == is_client_connected)
		return;
	was_client_connected = is_client_connected;
#ifdef DEBUG

	if (is_client_connected) {
		CDC_Transmit_FS((uint8_t *)"HTTP: Client Connected\r\n", 24);
	} else {
		CDC_Transmit_FS((uint8_t *)"HTTP: Client Disconnected\r\n", 27);
	}
        HAL_Delay(10);
#endif
}

void Log_Client_Request_Change() {
	if (was_client_requesting_page == is_client_requesting_page)
		return;
	was_client_requesting_page = is_client_requesting_page;
#ifdef DEBUG

	if (is_client_requesting_page) {
		CDC_Transmit_FS((uint8_t *)"HTTP: Client Started Requesting Page\r\n", 38);
	} else {
		CDC_Transmit_FS((uint8_t *)"HTTP: Client Stopped Requesting Page\r\n", 38);
	}
        HAL_Delay(10);
#endif
}

void Check_Reception_Completion() {
    if (strstr((char *)rx_buffer, "\r\n\r\n")) {
        // Timeout expired since the last received byte
        receiving_data = 0; // Mark that reception is complete

        #ifdef DEBUG
        CDC_Transmit_FS((uint8_t *)"===Data Reception Complete===\r\n", 31);
        HAL_Delay(10);

        char debug_info[50]; // Print buffer length and content
        snprintf(debug_info, sizeof(debug_info), "Buffer length: %d\r\n", strlen((char *)rx_buffer));
        CDC_Transmit_FS((uint8_t *)debug_info, strlen(debug_info));
        HAL_Delay(10);

        if (strlen((char *)rx_buffer) > 0) {
            Log_Uart_Response(); // Print the actual buffer content
            HAL_Delay(10);
        } else {
            CDC_Transmit_FS((uint8_t *)"<Empty Buffer>\r\n", 16);
            HAL_Delay(10);
        }

        #endif

        if (strstr((char *)rx_buffer, "GET /?ssid=")) {
            Handle_Client_Request();
        }

        if (strstr((char *)rx_buffer, "+STA_CONNECTED")) {
        	is_client_connected = 1;
        } else if (strstr((char *)rx_buffer, "+STA_DISCONNECTED")) {
        	is_client_connected = 0;
        }

        if (strstr((char *)rx_buffer, "0,CONNECT")) {
        	is_client_requesting_page = 1;
        } else if (strstr((char *)rx_buffer, "0,CLOSED")) {
            is_client_requesting_page = 0;
        }

        if (strstr((char *)rx_buffer, "OK")){
            Change_Response_Status(SUCCESS);
        } else if (strstr((char *)rx_buffer, "ERROR")){
        	Change_Response_Status(ERROR);
        }

        // Clear the buffer for the next reception
        Clear_RX_Buffer();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) // Check if it's USART2
    {
        // Store next byte at next position
        if (rx_index < RX_BUFFER_SIZE - 2) { // Leave room for next byte and null terminator
            rx_index++;
            rx_buffer[rx_index + 1] = '\0';  // Null terminate after current position
            HAL_UART_Receive_IT(&huart2, &rx_buffer[rx_index], 1);  // Set up next receive
        } else {
            // Buffer full - wrap around
            rx_index = 0;
            rx_buffer[1] = '\0';  // Null terminate after first position
            HAL_UART_Receive_IT(&huart2, &rx_buffer[rx_index], 1);  // Start over at beginning
        }



#ifdef DEBUG
        HAL_GPIO_TogglePin(GPIOE, LED_PIN_UART);
		//CDC_Transmit_FS((uint8_t *)rx_buffer, strlen((char *)rx_buffer));
		//HAL_Delay(10);
#endif
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) { // Button press
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) { // Button pressed
            button_pressed = 1;
            button_press_start = HAL_GetTick();
        } else { // Button released
            button_pressed = 0;
            uint32_t press_duration = HAL_GetTick() - button_press_start;

            if (press_duration < 500) { // Short press
                button_action_type = 0;
            } else { // Long press
                button_action_type = 1;
            }
            button_action_pending = 1;
        }
    }

    #if ENABLE_MAGNETOMETER
    if (GPIO_Pin == GPIO_PIN_2) {
        data_ready_mag = 1;
#ifdef DEBUG
        HAL_GPIO_TogglePin(GPIOE, LED_PIN_MAGNET);
#endif
    }
    #endif
    #if ENABLE_ACCELEROMETER
    if (GPIO_Pin == GPIO_PIN_4) { // INT1 for accelerometer
        data_ready_acc = 1;
#ifdef DEBUG
        HAL_GPIO_TogglePin(GPIOE, LED_PIN_ACCEL);
#endif
    }
    #endif
    #if ENABLE_GYROSCOPE
    if (GPIO_Pin == GPIO_PIN_1) { // INT2 for gyroscope
        data_ready_gyr = 1;
#ifdef DEBUG
        HAL_GPIO_TogglePin(GPIOE, LED_PIN_GYRO);
#endif
    }
    #endif
}


void Establish_Connection(const char *server_ip, uint16_t port) {
    char cmd[128];

    // Zapri obstoječo povezavo, če obstaja
    Send_Command("AT+CIPCLOSE=0\r\n");
    HAL_Delay(100); // Kratek zamik za zaprtje povezave
    Clear_RX_Buffer();

    // Pošlji ukaz za vzpostavitev TCP povezave
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=0,\"TCP\",\"%s\",%d\r\n", server_ip, port);
    Send_Command(cmd);

    // Preverjaj odziv za omejen čas
    uint32_t start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 5000) { // 5 sekund timeout
        if (strstr((char *)rx_buffer, "CONNECT")) {
            CDC_Transmit_FS((uint8_t *)"TCP connection established\n", 26);
            Change_Response_Status(SUCCESS);
            Clear_RX_Buffer();
            return;
        } else if (strstr((char *)rx_buffer, "ERROR") || strstr((char *)rx_buffer, "CLOSED")) {
            CDC_Transmit_FS((uint8_t *)"TCP connection failed\n", 23);
            Change_Response_Status(ERROR);
            Clear_RX_Buffer();
            return;
        }
    }

    // Če ni odgovora v določenem času
    CDC_Transmit_FS((uint8_t *)"Connection attempt timed out\n", 29);
    Change_Response_Status(TIMEOUT);
    Clear_RX_Buffer();
}

void Send_Data_To_Server(const char *json_data) {
    static uint32_t last_send_time = 0;
    const uint32_t MIN_INTERVAL = 500; // Minimum 500ms between sends
    uint32_t current_time = HAL_GetTick();

    // Check if enough time has passed since last send
    if (current_time - last_send_time < MIN_INTERVAL) {
        return;
    }

    char request[512];
    char cmd[128];
    uint32_t start_time;

    // Format HTTP POST request
    snprintf(request, sizeof(request),
             "POST /data HTTP/1.1\r\n"
             "Host: 172.20.10.11\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: keep-alive\r\n\r\n"
             "%s",
             strlen(json_data), json_data);

    if (strlen(request) > 512) {
        CDC_Transmit_FS((uint8_t *)"Data too large to send\n", 23);
        return;
    }

    // Add more debug output
    CDC_Transmit_FS((uint8_t *)"Checking connection status...\n", 29);

    // Check connection status
    Send_Command("AT+CIPSTATUS\r\n");
    HAL_Delay(100);

    if (strstr((char *)rx_buffer, "STATUS:4") || strstr((char *)rx_buffer, "STATUS:5")) {
        CDC_Transmit_FS((uint8_t *)"Connection lost, reconnecting...\n", 32);
        Clear_RX_Buffer();
        connection_established = 0;
        return;
    }

    Clear_RX_Buffer();
    CDC_Transmit_FS((uint8_t *)"Sending CIPSEND command...\n", 27);

    // Send CIPSEND command
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%d\r\n", strlen(request));
    Send_Command(cmd);
    HAL_Delay(200);  // Wait for ESP to process command

    // Wait for '>' prompt with better debug output
    start_time = HAL_GetTick();
    uint8_t got_prompt = 0;
    uint8_t retries = 0;
    const uint8_t MAX_RETRIES = 3;

	while (retries < MAX_RETRIES) {
		while ((HAL_GetTick() - start_time) < 1000) {
			if (strstr((char *)rx_buffer, ">")) {
				got_prompt = 1;
				CDC_Transmit_FS((uint8_t *)"Received '>' prompt\n", 20);
				HAL_Delay(50);
				break;
			}
			HAL_Delay(10);
		}

		if (got_prompt) break;

		// Če nismo dobili prompta, poskusi ponovno
		retries++;
		if (retries < MAX_RETRIES) {
			CDC_Transmit_FS((uint8_t *)"Retrying CIPSEND...\n", 20);
			Clear_RX_Buffer();
			Send_Command(cmd);
			HAL_Delay(200);
			start_time = HAL_GetTick();
		}
	}

	if (!got_prompt) {
		CDC_Transmit_FS((uint8_t *)"Failed after max retries\n", 25);
		Clear_RX_Buffer();
		connection_established = 0; // Resetiraj povezavo
		return;
	}

    Clear_RX_Buffer();
    CDC_Transmit_FS((uint8_t *)"Sending data...\n", 16);

    // Send data
    HAL_UART_Transmit(&huart2, (uint8_t *)request, strlen(request), HAL_MAX_DELAY);

    // Wait for "SEND OK" with timeout
    start_time = HAL_GetTick();
    uint8_t send_ok = 0;

    while ((HAL_GetTick() - start_time) < 2000) {
        if (strstr((char *)rx_buffer, "SEND OK")) {
            send_ok = 1;
            CDC_Transmit_FS((uint8_t *)"Data sent successfully\n", 23);
            last_send_time = HAL_GetTick(); // Update last send time
            break;
        }
        HAL_Delay(10);
    }

    if (!send_ok) {
        CDC_Transmit_FS((uint8_t *)"Send timeout - Response:\n", 25);
        CDC_Transmit_FS(rx_buffer, strlen((char *)rx_buffer));
    }

    Clear_RX_Buffer();
}

void Test_HTTP_GET_Request() {
    const char *get_request = "GET / HTTP/1.1\r\nHost: 172.20.10.11\r\n\r\n";
    Send_Data_To_Server(get_request);
}

void Indicate_Transmission_Mode(uint8_t mode) {
    #ifdef DEBUG
    const char *modes[] = {
        "None", "Binary UART", "ASCII UART",
        "Binary CDC", "ASCII CDC"
    };
    char debug_msg[50];
    HAL_GPIO_TogglePin(GPIOE, LED_PIN_SEND_MODE); // Signal prek LED
    snprintf(debug_msg, sizeof(debug_msg), "Mode changed to: %s\n", modes[mode]);
    CDC_Transmit_FS((uint8_t *)debug_msg, strlen(debug_msg));
    #endif
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */
  /* MCU Configuration--------------------------------------------------------*/
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */
  /* Configure the system clock */
  SystemClock_Config();
  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */
  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USB_DEVICE_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  //HAL_Delay(2000);
  Init_All_Sensors();
  Verify_Sensors(); // Verify all sensor communication
  Clear_Interrupts(); // Clear interrupt flags
  Log_Response_Status_Change();
    rx_index = 0;
  HAL_UART_Receive_IT(&huart2, &rx_buffer[0], 1);
  /* USER CODE END 2 */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
      Log_Client_Status_Change();
        Log_Client_Request_Change();

    Check_Reception_Completion();

      if (Has_Response_Finished() == 1) {
          Handle_Response();
      }

      if(Is_Timedout(5000) == 1 && response_status == WAITING) {
          Change_Response_Status(TIMEOUT);
      }

      if (response_status == SEND_REQUEST) {
          Configure_ESP_As_Access_Point();
      }

      if (button_action_pending) { // Handle button actions
          if (button_action_type == 0) { // Short press - send AT command
              Change_Response_Status(SEND_REQUEST);
          } else { // Long press - change transmission mode
              transmission_mode = (transmission_mode + 1) % 5;
              Indicate_Transmission_Mode(transmission_mode);
          }
          button_action_pending = 0;
      }

      if (!connection_established && transmission_mode == MODE_ASCII_UART)
	  {
		  CDC_Transmit_FS((uint8_t *)"Attempting to connect...\n", 25);
		  Establish_Connection("172.20.10.11", 5000);

		  /* Preveri status povezave */
		  if (response_status == SUCCESS)
		  {
			  CDC_Transmit_FS((uint8_t *)"Connection successful!\n", 24);
			  connection_established = 1; // Povezava vzpostavljena
		  }
		  else if (response_status == TIMEOUT || response_status == ERROR)
		  {
			  CDC_Transmit_FS((uint8_t *)"Connection failed, retrying...\n", 31);
			  HAL_Delay(1000); // Počakaj pred ponovnim poskusom
		  }
	  }

	  /* Tukaj obdeluj podatke le, če je povezava vzpostavljena */
	  if (connection_established)
	  {
		  //Test_HTTP_GET_Request();

		  #if ENABLE_MAGNETOMETER
		  if (data_ready_mag)
			  Handle_Magnetometer();
		  #endif
		  #if ENABLE_ACCELEROMETER
		  if (data_ready_acc)
			  Handle_Accelerometer();
		  #endif
		  #if ENABLE_GYROSCOPE
		  if (data_ready_gyr)
			  Handle_Gyroscope();
		  #endif
	  }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_USART2
                              |RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.USBClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x0000020B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PE2 PE4 PE1 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT4_Pin */
  GPIO_InitStruct.Pin = MEMS_INT4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT4_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD7_Pin
                           LD9_Pin LD10_Pin LD8_Pin LD6_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD7_Pin
                          |LD9_Pin|LD10_Pin|LD8_Pin|LD6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_TSC_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_TSC_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
