/**
 * @file callbacks.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief LoRaWAN callbacks
 * @version 0.1
 * @date 2026-10-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "main.h"

/** Buffer for receive downlinks */
WisBlockRxResult rx_buffered;
/** Time received from LNS */
time_t last_unixTime;
/** LinkCheck results */
WisBlockLinkCheckResult last_linkCheck;
/** LinkCheck success flag */
bool last_LinkCheckSuccess = false;
/** USB data buffer for non-AT commands */
char usb_data[256];

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
/**
 * @brief Timer event that wakes up the loop task frequently
 *
 * @param unused
 */
void periodic_wakeup(TimerHandle_t unused)
{
	if (g_task_sem != NULL)
	{
		taskEventSet(STATUS);
		// Wake up task to handle status 
		xSemaphoreGive(g_task_sem);
	}
}
#elif defined ESP32 || defined ARDUINO_ARCH_RP2040
/**
 * @brief Timer event that wakes up the loop task frequently
 *
 * @param unused
 */
void periodic_wakeup(void)
{
	if (g_task_sem != NULL)
	{
		// Switch on LED to show we are awake
		digitalWrite(LED_GREEN, HIGH);
		taskEventSet(STATUS);
		// Wake up task to handle status
		xSemaphoreGive(g_task_sem);
	}
}
#else
#warning MCU not supported
#endif

/**
 * @brief Handler for received lines that are not AT commands
 *
 * Anything that does not start with "AT" lands here instead of being answered with an AT error.
 * Use it to run your own serial protocol alongside the AT command set. This example prints the line.
 *
 * @param line Zero terminated text of the received line
 */
void onUnhandledLine(const char *line)
{
	if (g_task_sem != NULL)
	{
		int idx = 0;
		while ((line[idx] != 0) && (idx < 254))
		{
			usb_data[idx] = line[idx];
			idx++;
		}
		usb_data[idx] = line[idx];
		// Switch on LED to show we are awake
		digitalWrite(LED_GREEN, HIGH);
		taskEventSet(USB_DATA);
		// Wake up task to handle USB data
		xSemaphoreGive(g_task_sem);
	}
}

/**
 * @brief Callback after LoRaWAN transmission has finished
 *
 * Wakes up loop with event LORA_TX_FIN
 *
 * @param result WisBlockTxResult structure with result details
 */
void onTxDone(const WisBlockTxResult &result)
{
	if (g_task_sem != NULL)
	{
		taskEventSet(LORA_TX_FIN);
		// Wake up task
		xSemaphoreGive(g_task_sem);
	}
}

/**
 * @brief Callback after downlink packet was received
 *
 * Wakes up loop with event LORA_DATA
 * WisBlockRxResult content is backed up into structure rx_buffered
 *
 * @param result WisBlockRxResult structure with received data and details
 */
void onRxDone(const WisBlockRxResult &result)
{
	if (result.length > 0)
	{
		if (g_task_sem != NULL)
		{
			rx_buffered.port = result.port;
			rx_buffered.length = result.length;
			rx_buffered.rssi = result.rssi;
			rx_buffered.snr = result.snr;

			memcpy(rx_buffered.data, result.data, result.length);
			// or
			for (int idx = 0; idx < result.length; idx++)
			{
				rx_buffered.data[idx] = result.data[idx];
			}
			rx_buffered.fpending = result.fpending;
			rx_buffered.isMulticast = result.isMulticast;
			rx_buffered.multicastGroupId = result.multicastGroupId;

			taskEventSet(LORA_DATA);
			// Wake up task to check received data
			xSemaphoreGive(g_task_sem);
		}
	}
}

/**
 * @brief Callback for CAD result
 *
 * Wakes up loop with event CAD_FAIL if channel activity was detected.
 * Otherwise it will send the payload hold in g_solution_data over P2P.
 *
 * @param CAD result, WISBLOCK_CAD_CHANNEL_CLEAR or WISBLOCK_CAD_CHANNEL_DETECTED
 */
void onCad(WisBlockCADResult result)
{
	if (result == WISBLOCK_CAD_CHANNEL_CLEAR)
	{
		MYLOG("CAD","Channel free, sending payload");
		lora.sendP2P(g_solution_data.getBuffer(), g_solution_data.getSize());
	}
	else
	{
		if (g_task_sem != NULL)
		{
			taskEventSet(CAD_FAIL);
			// Wake up task to report CAD fail
			xSemaphoreGive(g_task_sem);
		}
	}
}
