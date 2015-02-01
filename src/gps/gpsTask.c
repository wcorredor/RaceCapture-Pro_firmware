/*
 * gpsTask.c
 *
 *  Created on: Jun 22, 2013
 *      Author: brent
 */
#include "gpsTask.h"
#include "gps.h"
#include "gps_device.h"
#include "FreeRTOS.h"
#include "printk.h"
#include "task.h"
#include "serial.h"
#include "taskUtil.h"

#define GPS_TASK_STACK_SIZE			200

static bool g_enableGpsDataLogging = false;

//void setGpsDataLogging(bool enable) {
//   g_enableGpsDataLogging = enable;
//}

/**
 * Prints the output to the serial port if it is set.
 * @param buf The buffer containing the read in line.
 * @param len The length of the string in the buffer.
 */
//static void logGpsInput(const char *buf, int len) {
//   if (!g_enableGpsDataLogging) return;
//   pr_info(buf);
//}

void GPSTask(void *pvParameters) {
	GpsSamp gpsSample;
	Serial *gpsSerial = get_serial(SERIAL_GPS);
	int rc = GPS_device_provision(gpsSerial);
	if (!rc){
		pr_error("Error provisioning GPS module\r\n");
	}

	for (;;) {
		gps_msg_result_t result = GPS_device_get_update(&gpsSample, sample);
		if (result == GPS_MSG_SUCCESS){
			processGPSUpdate(&gpsSample);
		}
		else{
			pr_warning("timeout getting GPS update\r\n");
		}
   }
}

void startGPSTask(int priority){
	initGPS();
	xTaskCreate( GPSTask, ( signed portCHAR * )"GPSTask", GPS_TASK_STACK_SIZE, NULL, priority, NULL );
}

