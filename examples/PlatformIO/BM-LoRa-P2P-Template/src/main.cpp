/**
 * @file main.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief WisBlock application template for LoRa P2P using BasicModem WisBlockLoRaWAN library
 * @version 0.1
 * @date 2026-10-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "main.h"

/** LoRa event handler */
WisBlockLoRaWAN lora;
/** AT command handler */
WisBlockLoRaAT at_serial;

/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;
/** Flag for the event type */
volatile uint16_t g_task_event_type = 0;

// Custom settings
CustomAtSettings custom_settings;
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
/** Timer to wakeup task frequently and send message */
TimerHandle_t g_task_wakeup_timer;
#elif defined(ARDUINO_ARCH_ESP32)
/** Timer to wakeup task frequently and send message */
Ticker g_task_wakeup_timer;
#endif

/** Uplink buffer in Cayenne LPP format */
WisCayenne g_solution_data(255);
/** Counter for packages */
uint32_t package_cnt = 0;

/** Counter for join attempts */
uint8_t join_attempt = 0;

/**
 * @brief Arduino setup function, called once after reset or power up
 *
 */
void setup(void)
{
	pinMode(LED_BLUE, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
	digitalWrite(LED_GREEN, HIGH);
	digitalWrite(LED_BLUE, LOW);
	Serial.begin(115200);

	time_t serial_timeout = millis();
	// On nRF52840 the USB serial is not available immediately
	while (!Serial)
	{
		if ((millis() - serial_timeout) < 5000)
		{
			delay(100);
			digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
			digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
		}
		else
		{
			break;
		}
	}
	digitalWrite(LED_GREEN, LOW);
	digitalWrite(LED_BLUE, LOW);

	// Announce module
	Serial.println("==========================================================");
	Serial.println("WisBlock Basic Modem example application");
	Serial.println("==========================================================");
	Serial.println("If device is not configured, it will stay in a loop until");
	Serial.println("LoRaWAN credentials and region are setup with AT commands.");
	Serial.println("Then reboot the device");
	Serial.println("==========================================================");

	// Create the task event semaphore
	g_task_sem = xSemaphoreCreateBinary();
	// Initialize semaphore
	xSemaphoreGive(g_task_sem);
	// Take the semaphore so the loop will go to sleep until an event happens
	xSemaphoreTake(g_task_sem, 10);

	// Start LoRa engine
	lora.begin();
	// This is LoRaWAN only, check if the configuration is set to LoRaWAM
	WisBlockWorkMode current_mode = lora.getWorkMode();
	if (current_mode == WISBLOCK_MODE_LORA_P2P)
	{
		MYLOG("SETUP", "All good, we are on LoRa P2P");
	}
	else
	{
		MYLOG("SETUP", "Need to switch from LoRaWAN to P2P");
		lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
		lora.saveConfig();
	}

	// Enable LoRa background task
	lora.enableBackgroundTask();

	// Start AT command engine
	at_serial.begin(lora, Serial);
	at_serial.onUnhandledData(onUnhandledLine);
	if (!at_serial.enableBackgroundRx())
	{
		MYLOG("SETUP", "AT command USB RX hook failed");
	}

	// Check if a valid configuration is available.
	if (!lora.hasValidConfig())
	{
		// No valid configuration found
		MYLOG("SETUP", "No valid configuration found.");
		Serial.println("Device will not attempt a LoRaWAN join.");
		//==================================================================================//
		// Setup device with AT commands first or add configuration through API calls here. //
		// Reboot device after finishing the configuration.                                 //
		//==================================================================================//
		taskEventSet(NO_CONFIG);
	}

	// Setup callbacks
	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);
	lora.onP2PCadResult(onCad);

	// Register application-defined custom AT commands (ATC+SENDINT=<seconds> -
	// see custom_at.h/.cpp) and load any previously-saved values for them.
	registerCustomATCommands(at_serial);
	// Get saved custom settings
	MYLOG("SETUP", "Get saved custom settings");
	custom_settings = getCustomAtSettings();

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
	// Initialize the timer for frequent sending
	g_task_wakeup_timer = xTimerCreate(NULL, mypdMS_TO_TICKS(custom_settings.sendIntervalS * 1000), true, NULL, periodic_wakeup);

	// Start the timer for frequent sending
	if (custom_settings.sendIntervalS != 0)
	{
		xTimerStart(g_task_wakeup_timer, 0);
	}
#elif defined ESP32
	// Start the timer for frequent sending
	if (custom_settings.sendIntervalS != 0)
	{
		g_task_wakeup_timer.attach_ms(custom_settings.sendIntervalS * 1000, periodic_wakeup);
	}
#endif
	if (custom_settings.sendIntervalS != 0)
	{
		MYLOG("SETUP", "Request first packet sending");
		// Request to send first packet
		if (g_task_sem != NULL)
		{
			taskEventSet(STATUS);
			// Wake up task to report CAD fail
			xSemaphoreGive(g_task_sem);
		}
	}
	Serial.flush();
}

/**
 * @brief Arduino loop, called frequently, here paused by waiting for a semaphore
 *
 */
void loop(void)
{
	// Switch off green LED to show we go to sleep
	digitalWrite(LED_GREEN, LOW);
	delay(10);

	// Wait until semaphore is released (FreeRTOS)
	xSemaphoreTake(g_task_sem, portMAX_DELAY);
	// Switch on LED to show we are awake
	digitalWrite(LED_GREEN, HIGH);

	while (g_task_event_type != NO_EVENT)
	{
		// No config data, just hang in there and wait for configuration
		if ((g_task_event_type & NO_CONFIG) == NO_CONFIG)
		{
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// EXPERIMENTAL, LOOP HERE AND WAIT FOR CREDENTIALS TO BE SET
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//

			digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
			digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
			delay(1000);
		}
		// Wakeup to send data
		if ((g_task_event_type & STATUS) == STATUS)
		{
			taskEventClear(N_STATUS);
			MYLOG("LOOP", "Send");
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Add real code like sensor readings here
			g_solution_data.reset();
			g_solution_data.addVoltage(0x01, 4.03f);
			g_solution_data.addFrequency(0x02, package_cnt);
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			digitalWrite(LED_BLUE, HIGH);
			// lora.stopP2PReceive();
			lora.startP2PCad();
			MYLOG("LOOP", "P2P CAD started");
			package_cnt++;
		}
		// LoRa TX finished
		if ((g_task_event_type & LORA_TX_FIN) == LORA_TX_FIN)
		{
			taskEventClear(N_LORA_TX_FIN);
			digitalWrite(LED_BLUE, LOW);
			MYLOG("LOOP", "TX finished");
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Add your own strategy here for this case.
			// In this template, enable RX for indefinitive
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			MYLOG("LOOP", "Start RX");
			lora.startP2PReceive(0);
			// if (custom_settings.sendIntervalS != 0)
			// {
			// 	lora.startP2PReceive(custom_settings.sendIntervalS / 2 * 1000);
			// }
		}
		// Downlink data received
		if ((g_task_event_type & LORA_DATA) == LORA_DATA)
		{
			taskEventClear(N_LORA_DATA);
			MYLOG("LOOP", "RX %u bytes on port %u, RSSI %d SNR %d", rx_buffered.length, rx_buffered.port, rx_buffered.rssi, rx_buffered.snr);
#if MY_DEBUG > 0
			Serial.print("RX: ");
			if (rx_buffered.length > 0)
			{
				for (int idx = 0; idx < rx_buffered.length; idx++)
				{
					Serial.printf("0x%X ", rx_buffered.data[idx]);
				}
				Serial.println("");
			}
#endif
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Add here potential handling of the received data.
			// Cannot be time consuming, because next received data will overwrite rx_buffered
			// Here we just start sleeping
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//

			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Add your own strategy here for this case.
			// In this template, enable RX for indefinitive
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			MYLOG("LOOP", "Start RX");
			lora.startP2PReceive(0);
		}
		// CAD returned channel busy
		if ((g_task_event_type & CAD_FAIL) == CAD_FAIL)
		{
			taskEventClear(N_CAD_FAIL);
			MYLOG("LOOP", "CAD returned channel busy");
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Add your own strategy here for this case.
			// In this template, it will not retry to send the data
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
		}
		// Serial input event
		if ((g_task_event_type & USB_DATA) == USB_DATA)
		{
			taskEventClear(N_USB_DATA);
			// Add handling of non AT commands here
			// In the template, just print it out
			Serial.printf("USB RX: %s\n", usb_data);
		}
	}
	Serial.flush();
	delay(10);
	// Switch off green LED to show we go to sleep
	digitalWrite(LED_GREEN, LOW);
	delay(10);
}