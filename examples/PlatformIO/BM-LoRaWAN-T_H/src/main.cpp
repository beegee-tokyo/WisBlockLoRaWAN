/**
 * @file main.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief WisBlock application template for LoRaWAN using BasicModem WisBlockLoRaWAN library
 * @version 0.1
 * @date 2026-10-12
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "main.h"

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;
/** Flag for the event type */
volatile uint16_t g_task_event_type = 0;

// Custom settings
CustomAtSettings custom_settings;

/** Uplink buffer in Cayenne LPP format */
WisCayenne g_solution_data(255);
/** Counter for packages */
uint32_t package_cnt = 0;

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
/** Timer to wakeup task frequently and send message */
TimerHandle_t g_task_wakeup_timer;
#elif defined ESP32
/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;
/** Timer to wakeup task frequently and send message */
Ticker g_task_wakeup_timer;
#else
#warning MCU not supported
#endif

/** Counter for join attempts */
uint8_t join_attempt = 0;

/**
 * @brief Arduino setup function
 *
 * Starts the serial port and the library (RAK3401 hardware configuration if defined), registers
 * the LoRaWAN callbacks, starts the background task and auto join, saves the configuration, starts
 * the AT command interface with the custom AT commands, loads the uplink interval and creates the
 * wake-up semaphore and timer.
 */
void setup()
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
	MYLOG("SETUP", "If device is not configured, it will stay in a loop until");
	MYLOG("SETUP", "LoRaWAN credentials and region are setup with AT commands.");
	MYLOG("SETUP", "Then reboot the device");
	MYLOG("SETUP", "==========================================================");

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
	if (current_mode == WISBLOCK_MODE_LORAWAN)
	{
		MYLOG("SETUP", "All good, we are on LoRaWAN");
	}
	else
	{
		MYLOG("SETUP", "Need to switch from P2P to LoRaWAN");
		lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
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
	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onTimeRequestAnswer(onTimeAnswer);
	lora.onLinkCheckAnswer(onLinkCheck);

	// Register application-defined custom AT commands (ATC+SENDINT=<seconds> -
	// see custom_at.h/.cpp) and load any previously-saved values for them.
	registerCustomATCommands(at_serial);
	// Get saved custom settings
	MYLOG("SETUP", "Get saved custom settings");
	custom_settings = getCustomAtSettings();

// Initialize the timer for frequent sending
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
	g_task_wakeup_timer = xTimerCreate(NULL, mypdMS_TO_TICKS(custom_settings.sendIntervalS * 1000), true, NULL, periodic_wakeup);
#endif

	/* Init sensors and battery reading */
	Wire.begin();
	has_rak1901 = init_rak1901();
	init_batt();
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

	while (g_task_event_type != NO_EVENT)
	{
		// Switch on green LED to show we are awake
		digitalWrite(LED_GREEN, HIGH);

		// Serial.printf("[Loop] Wakeup Cause %s\n", std::bitset<16>(g_task_event_type).to_string().c_str());

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
			if (lora.isJoined())
			{
				MYLOG("LOOP", "Send");

				// Prepare payload
				g_solution_data.reset();
				if (has_rak1901)
				{
					read_rak1901();
				}
				// Get Battery status
				float batt_level_f = 0.0;
				for (int readings = 0; readings < 10; readings++)
				{
					batt_level_f += read_batt();
				}
				batt_level_f = batt_level_f / 10;
				g_solution_data.addVoltage(LPP_CHANNEL_BATT, batt_level_f / 1000.0);

				//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
				// Add a uplink counter, just for testing, not required in real application
				g_solution_data.addFrequency(0x02, package_cnt);
				//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
				// Do a link check and time request every 100 uplinks
				if ((package_cnt % (uint32_t)100) == 0)
				{
					MYLOG("LOOP", "Request linkcheck and time at packet %d", package_cnt);
					lora.requestLinkCheck();
					lora.requestDeviceTime();
				}
				digitalWrite(LED_BLUE, HIGH);
				if (!lora.sendLoRaWAN(1, g_solution_data.getBuffer(), g_solution_data.getSize()))
				{
					digitalWrite(LED_BLUE, LOW);
					MYLOG("LOOP", "Start sending failed");
				}
				package_cnt++;
			}
		}
		// Device joined LoRaWAN network successfully
		if ((g_task_event_type & LORA_JOIN_FIN) == LORA_JOIN_FIN)
		{
			taskEventClear(N_LORA_JOIN_FIN);
			MYLOG("LOOP", "Device joined LoRaWAN network successfully");
			// Start the timer for frequent sending
			if (custom_settings.sendIntervalS != 0)
			{
				MYLOG("LOOP", "Start Timer");
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
				xTimerStart(g_task_wakeup_timer, 0);
#elif defined ESP32
				g_task_wakeup_timer.attach_ms(custom_settings.sendIntervalS * 1000, periodic_wakeup);
#endif
				// Request to send first packet
				taskEventSet(STATUS);
			}
		}
		// Device join request failed, retry attempts used up
		if ((g_task_event_type & LORA_JOIN_FAIL) == LORA_JOIN_FAIL)
		{
			taskEventClear(N_LORA_JOIN_FAIL);
			MYLOG("LOOP", "Join failed, sleep before retrying");
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			// Check if a reset of joinNONCE and devNONCE is possible
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			join_attempt += 1;
			// Check number of attempts, don't delete endless, if sleep time is 6 hours, do not increase anymore
			if (join_attempt == 7)
			{
				join_attempt = 6;
			}
			// Sleep for join_attempt hours (exaggerated) to save battery power before next try
			delay(join_attempt * 60 * 60 * 1000);

			// Next attempt to join with 3 attempts only and 60 seconds delay between attempts
			lora.setJoinReattemptInterval(60);
			lora.setMaxJoinAttempts(3);
			lora.join();
			MYLOG("LOOP", "Start manual join");
		}
		// LoRa TX finished
		if ((g_task_event_type & LORA_TX_FIN) == LORA_TX_FIN)
		{
			taskEventClear(N_LORA_TX_FIN);
			digitalWrite(LED_BLUE, LOW);
			MYLOG("LOOP", "TX finished");
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
			// Cannot be time consuming, because LoRaWAN stack will pull all pending downlinks in a row
			//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
			MYLOG("LOOP", "fPending %s", rx_buffered.fpending ? "true" : "false");
		}
		// Server time received
		if ((g_task_event_type & LORA_TIME) == LORA_TIME)
		{
			taskEventClear(N_LORA_TIME);
			setTime(last_unixTime + (8 * 60 * 60)); // Philippine time
			char buf[40];
			sprintf(buf, "%02d/%02d/%4d %02d:%02d:%02d", day(), month(), year(), hour(), minute(), second());
			MYLOG("LOOP", "Local time: %s", buf);
		}
		// Linkcheck results
		if ((g_task_event_type & LINK_CHECK) == LINK_CHECK)
		{
			taskEventClear(N_LINK_CHECK);
			if (last_LinkCheckSuccess)
			{
				MYLOG("LOOP", "Link check: margin %u dB, %u gateways", last_linkCheck.demodMargin, last_linkCheck.gatewayCount);
			}
			else
			{
				MYLOG("LOOP", "LinkCheck failed, no answer from LNS");
			}
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
}
