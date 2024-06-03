/*
 * main.c
 *
 *  Created on: 31 lug 2023
 *      Author: Mattia
 */


#include "esp_log.h"
#include "nvs_flash.h"



#include <string.h>

#include "WiFi.h"




#define DEBUG 1






void app_main(void)
{

#ifdef DEBUG
#endif


	// Start Wifi
	startWifi();







}
