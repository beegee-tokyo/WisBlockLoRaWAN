/**
 * BasicLoRaWAN.ino
 * OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.
 * Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h
 * has the right pins for your revision and LBM is vendored in (see README).
 */
#include "main.h"

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

// Custom settings
CustomAtSettings custom_settings;

// Flag if pending downlinks are all pulled automatically until queue is empty
#define GET_PENDING_DLP 1

// Time management and RX buffer
time_t unixTime; // a time stamp
WisBlockRxResult rx_buffered;

// Replace with your device's real OTAA credentials.
#if defined ARDUINO_ARCH_NRF52
uint8_t devEui[8] = {0xac, 0x1f, 0x09, 0xff, 0xfe, 0x06, 0x79, 0xdb}; // ac1f09fffe0679db // 0x09, 0x01, 0x88
#else
uint8_t devEui[8] = {0xac, 0x1f, 0x09, 0xff, 0xfe, 0x18, 0xF0, 0xC4}; // ac1f09fffe18f0c4
#endif
uint8_t joinEui[8] = {0x70, 0xb3, 0xd5, 0x7e, 0xd0, 0x02, 0x01, 0xe1};												   // 70b3d57ed00201e1
uint8_t appKey[16] = {0x2b, 0x84, 0xe0, 0xb0, 0x9b, 0x68, 0xe5, 0xcb, 0x42, 0x17, 0x6f, 0xe7, 0x53, 0xdc, 0xee, 0x79}; // 2b84e0b09b68e5cb42176fe753dcee79

uint32_t UPLINK_INTERVAL_MS = 0; // 240000;

/** Flag for the event type */
volatile uint16_t g_task_event_type = 0;

/** Uplink buffer in Cayenne LPP format */
WisCayenne g_solution_data(255);
/** Counter for packages */
uint32_t package_cnt = 0;

#if defined ARDUINO_ARCH_NRF52
// Define alternate pdMS_TO_TICKS that casts uint64_t for long intervals due to limitation in nrf52840 BSP
#define mypdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))

// Prepare timer and seamphore to wake up loop for frequent sending
/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;

/** Timer to wakeup task frequently and send message */
TimerHandle_t g_task_wakeup_timer;
/**
 * @brief Timer event that wakes up the loop task frequently
 *
 * @param unused
 */
void periodic_wakeup(TimerHandle_t unused)
{
	if (g_task_sem != NULL)
	{
		// Switch on LED to show we are awake
		digitalWrite(LED_GREEN, HIGH);
		g_task_event_type |= STATUS;
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
}
#elif defined ESP32
/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;

/** Timer to wakeup task frequently and send message */
Ticker g_task_wakeup_timer;
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
		g_task_event_type |= STATUS;
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
}
#else
#warning MCU not supported
#endif

void onJoined()
{
	Serial.println("[LoRaWAN] Join succeeded");
	digitalWrite(LED_BLUE, LOW);
	g_task_event_type |= LORA_JOIN_FIN;
	if (g_task_sem != NULL)
	{
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
	Serial.flush();
}

void onJoinFailed()
{
	// FIX: this used to unconditionally call lora.join() again here on every
	// single failed attempt - see LoRaWANEngine::join()'s doc comment for
	// why that's wrong now that AT+JOIN=w:x:y:z can configure a retry
	// interval/max attempt count: join() always resets the internal attempt
	// counter back to 0, so calling it from here meant maxJoinAttempts could
	// never actually be reached (this callback's own join() call kept
	// resetting it first), and it raced the library's own scheduled retry,
	// producing shorter, irregular gaps than the configured interval instead
	// of honoring it. The library already retries on its own after a
	// failure - nothing to do here except react to a final give-up if
	// desired.
	if (lora.joinState() == WISBLOCK_JOIN_GAVE_UP)
	{
		Serial.println("[LoRaWAN] Join failed - reached AT+JOIN's configured max attempts, giving up");
		// \todo application-specific recovery here - e.g. retry after a longer
		// cooldown, fall back to a different region/channel plan, or signal
		// the user (LED/display) that this unit needs attention.
	}
	else
	{
		Serial.println("[LoRaWAN] Join attempt failed, retrying automatically...");
	}
	Serial.flush();
}

void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[LoRaWAN] TX %s, airtime %lu ms\n", result.success ? "OK" : "FAILED", result.airtimeMs);
	Serial.flush();
}

void onRxDone(const WisBlockRxResult &result)
{
	if (result.isMulticast)
	{
		Serial.printf("[LoRaWAN] Multicast ID %u RX %u bytes on port %u, RSSI %d SNR %d\n",
					  result.multicastGroupId, result.length, result.port, result.rssi, result.snr);
	}
	else
	{
		Serial.printf("[LoRaWAN] Unicast RX %u bytes on port %u, RSSI %d SNR %d\n",
					  result.length, result.port, result.rssi, result.snr);
	}
	if (result.length > 0)
	{
		g_task_event_type |= LORA_DATA;
		if (g_task_sem != NULL)
		{
			rx_buffered.port = result.port;
			rx_buffered.length = result.length;
			rx_buffered.rssi = result.rssi;
			rx_buffered.snr = result.snr;
			for (int idx = 0; idx < result.length; idx++)
			{
				rx_buffered.data[idx] = result.data[idx];
			}
			rx_buffered.fpending = result.fpending;
			rx_buffered.isMulticast = result.isMulticast;
			rx_buffered.multicastGroupId = result.multicastGroupId;
			// Wake up task to check received data
			xSemaphoreGive(g_task_sem);
		}
	}
	Serial.flush();
}

void onTimeAnswer(bool success, const WisBlockTimeAnswer &t)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Network time: %lu s (GPS epoch)\n", t.gpsEpochSeconds);
		unixTime = t.gpsEpochSeconds;
		// Make it PH time
		unixTime = unixTime + 315964800 - 18; // Convert GPS Epoch Seconds to UTC
		g_task_event_type |= LORA_TIME;
		if (g_task_sem != NULL)
		{
			// Wake up task to send initial packet
			xSemaphoreGive(g_task_sem);
		}
	}
	Serial.flush();
}

void onLinkCheck(bool success, const WisBlockLinkCheckResult &r)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Link check: margin %u dB, %u gateways\n", r.demodMargin, r.gatewayCount);
	}
	Serial.flush();
}

void setupMulticastGroup()
{
	// 1) Switch to Class C (or Class B) first - a multicast group has no
	//    meaning on Class A, which has no standing RX window to receive it on.
	if (!lora.setDeviceClass(WISBLOCK_CLASS_C))
	{
		Serial.println("[Multicast] Failed to switch to Class C");
		return;
	}

	// 2) These four values are provisioned by the network operator/join
	//    server out-of-band - NOT derived from this device's own unicast
	//    OTAA session keys (they're a completely separate key pair). In a
	//    real deployment you'd get these from your backend, not hardcode
	//    them - shown here just to make the example concrete.
	uint32_t mcDevAddr = 0x01beee5f;
	uint8_t mcNwkSKey[16] = {0x82, 0x91, 0x99, 0xe0, 0xc2, 0x49, 0x7e, 0xd6, 0xa1, 0xbd, 0x06, 0x5e, 0x27, 0x40, 0x15, 0x82};
	uint8_t mcAppSKey[16] = {0x9b, 0xd5, 0xf6, 0x37, 0xd2, 0x76, 0x07, 0x55, 0xfe, 0x1e, 0x47, 0xcf, 0xb2, 0x86, 0xe9, 0x5e};
	uint32_t mcFrequencyHz = 916800000; // must match what the gateway/operator will transmit on
	uint8_t mcDataRate = 3;

	// 3) Configure the group AND start its RX session in one call - group
	//    ID 0 here (0-3 available, WISBLOCK_MULTICAST_GROUP_COUNT total).
	//    `periodicity` (last param, default 0) only matters for Class B -
	//    harmless to leave at 0 for Class C.
	if (!lora.setMulticastGroup(0, WISBLOCK_CLASS_C, mcDevAddr, mcNwkSKey, mcAppSKey,
								mcFrequencyHz, mcDataRate))
	{
		Serial.println("[Multicast] Failed to configure group 0");
		return;
	}

	Serial.println("[Multicast] Group 0 active");
}

void setup()
{
	pinMode(LED_BLUE, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
	digitalWrite(LED_GREEN, HIGH);
	digitalWrite(LED_BLUE, HIGH);
	Serial.begin(115200);

	time_t serial_timeout = millis();
	// On nRF52840 the USB serial is not available immediately
	while (!Serial)
	{
		if ((millis() - serial_timeout) < 5000)
		{
			delay(100);
			digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
		}
		else
		{
			break;
		}
	}

	// Prepare seamphore to wake up loop for frequent sending
#if defined ARDUINO_ARCH_NRF52 || defined ESP32

	// Create the task event semaphore
	g_task_sem = xSemaphoreCreateBinary();
	// Initialize semaphore
	xSemaphoreGive(g_task_sem);
	// Take the semaphore so the loop will go to sleep until an event happens
	xSemaphoreTake(g_task_sem, 10);
#else
#warning MCU not supported
#endif

	Serial.println("[Setup] lora.begin");
	lora.begin();
	// Serial.println("[LoRaWAN] setup");
	// 	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	// 	lora.setOTAAKeys(devEui, joinEui, appKey);
	// 	lora.setRegion(WISBLOCK_RUI3_BAND_AS923_3); // RUI3 AT+BAND numbering - see WisBlockRUI3Band
	// 	const WisBlockPersistedConfig &cfg = lora.getConfig();
	// 	int idx = cfg.lorawan.region;
	// 	Serial.printf("[Setup] Current band selection %d\n", cfg.lorawan.region);

	// 	uint16_t mask = 0xFFFF;
	// 	if (idx == WISBLOCK_RUI3_BAND_AU915) // WISBLOCK_REGION_AU915
	// 	{
	// 		// sub-band 1 (channels 0-7 + 64) - e.g. The Things Network AU915 recommended subband
	// 		mask = 0x0001;
	// 	}
	// 	if (idx == WISBLOCK_RUI3_BAND_US915) // WISBLOCK_REGION_US915
	// 	{
	// 		// sub-band 2 (channels 8-15 + 65) - e.g. The Things Network US915 recommended subband
	// 		mask = 0x0002;
	// 	}
	// 	if (mask != 0xffff)
	// 	{
	// 		if (!lora.setChannelMask(mask))
	// 		{
	// 			Serial.printf("[Setup] Failed to set channel mask %04X\n", mask);
	// 		}
	// 		else
	// 		{
	// 			Serial.printf("[Setup] Set channel mask %04X\n", mask);
	// 		}
	// 	}
	// 	lora.setDeviceClass(WISBLOCK_CLASS_A);
	// #if GET_PENDING_DLP == 1
	// 	lora.setFetchPendingDownlinks(true);
	// #else
	// 	lora.setFetchPendingDownlinks(false);
	// #endif
	// lora.setTxPower(0);
	// if (!lora.setADR(false))
	// {
	// 	Serial.println("[Setup] Failed to disable ADR");
	// }
	// if (!lora.setDataRate(3))
	// {
	// 	Serial.println("[Setup] Failed to set DR3");
	// }

	// lora.setConfirmedUplinks(false);

	Serial.println("[LoRaWAN] setup from saved config");

	Serial.println("[Setup] set callbacks");
	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onTimeRequestAnswer(onTimeAnswer);
	lora.onLinkCheckAnswer(onLinkCheck);

	if (lora.enableBackgroundTask())
	{
		Serial.println("[Setup] Background task active");
	}
	else
	{
		Serial.println("[Setup] FreeRTOS unavailable");
	}

	//  Check if Join is controlled via AT command with manual join or autojoin
	// Serial.println("[Setup] join");
	// lora.join();
	if (!lora.getAutoJoin())
	{
		lora.setAutoJoin(true);
		lora.setJoinReattemptInterval(30);
		lora.setMaxJoinAttempts(3);
		lora.join();
		Serial.println("[Setup] Start manual join");
	}
	else
	{
		Serial.println("[Setup] Auto join is enabled");
	}

	Serial.println("[Setup] save");
	if (!lora.saveConfig())
	{
		Serial.println("[Setup] Failed to save settings");
	}

	// // Start AT command interface
	at_serial.begin(lora, Serial);
	// at_serial.enableBackgroundRx();

	// Register application-defined custom AT commands (ATC+SENDINT=<seconds> -
	// see custom_at.h/.cpp) and load any previously-saved values for them.
	registerCustomATCommands(at_serial);

	// Get saved settings
	Serial.println("[Setup] Get saved custom settings");
	custom_settings = getCustomAtSettings();
	UPLINK_INTERVAL_MS = custom_settings.sendIntervalS * 1000; // seconds to milli seconds

#if defined ESP32
	Serial.onEvent(usbEventCallback);
#endif

// Initialize the timer for frequent sending
#if defined ARDUINO_ARCH_NRF52
	g_task_wakeup_timer = xTimerCreate(NULL, mypdMS_TO_TICKS(UPLINK_INTERVAL_MS), true, NULL, periodic_wakeup);
#endif
}

void loop()
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

		if ((g_task_event_type & LORA_JOIN_FIN) == LORA_JOIN_FIN)
		{
			g_task_event_type &= N_LORA_JOIN_FIN;

			if (!lora.setADR(false))
			{
				Serial.println("[JOIN] Failed to disable ADR");
			}
			if (!lora.setDataRate(3))
			{
				Serial.println("[JOIN] Failed to set DR3");
			}
			lora.setConfirmedUplinks(false);

			// Start the timer for frequent sending
#if defined ARDUINO_ARCH_NRF52
			if (UPLINK_INTERVAL_MS != 0)
			{
				if (isInISR())
				{
					BaseType_t xHigherPriorityTaskWoken = pdFALSE;
					xTimerStartFromISR(g_task_wakeup_timer, &xHigherPriorityTaskWoken);
					portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
				}
				else
				{
					xTimerStart(g_task_wakeup_timer, 0);
				}
			}
#elif defined ESP32
			if (UPLINK_INTERVAL_MS != 0)
			{
				g_task_wakeup_timer.attach_ms(UPLINK_INTERVAL_MS, periodic_wakeup);
			}
#endif
			if (custom_settings.sendIntervalS != 0)
			{ // Request to send first packet
				g_task_event_type |= STATUS;
			}

			// Enable Multicast group
			setupMulticastGroup();
		}
		if ((g_task_event_type & STATUS) == STATUS)
		{
			g_task_event_type &= N_STATUS;
			if (lora.isJoined())
			{
				Serial.println("[LOOP] Send");
				g_solution_data.reset();
				g_solution_data.addVoltage(0x01, 4.03f);
				g_solution_data.addFrequency(0x02, package_cnt);
				if ((package_cnt % 10) == 0)
				{
					lora.requestLinkCheck();
					lora.requestDeviceTime();
				}
				if (!lora.sendLoRaWAN(1, g_solution_data.getBuffer(), g_solution_data.getSize()))
				{
					Serial.println("[LOOP] Start sending failed");
				}
				package_cnt++;
			}
		}
		// RX event
		if ((g_task_event_type & LORA_DATA) == LORA_DATA)
		{
			g_task_event_type &= N_LORA_DATA;
			// Serial.printf("[LOOP] RX %u bytes on port %u, RSSI %d SNR %d\n",
			// 			  rx_buffered.length, rx_buffered.port, rx_buffered.rssi, rx_buffered.snr);
			Serial.print("[LOOP] RX: ");
			if (rx_buffered.length > 0)
			{
				for (int idx = 0; idx < rx_buffered.length; idx++)
				{
					Serial.printf("0x%X ", rx_buffered.data[idx]);
				}
				Serial.println("");
			}
			Serial.printf("[LOOP] fPending %s\n", rx_buffered.fpending ? "true" : "false");
			Serial.flush();
#if GET_PENDING_DLP == 0
			if (rx_buffered.fpending)
			{
				if (!lora.sendLoRaWAN(1, devEui, 0))
				{
					Serial.println("[LOOP] Pull next pending DL failed");
				}
			}
#endif
		}
		// Server time received
		if ((g_task_event_type & LORA_TIME) == LORA_TIME)
		{
			g_task_event_type &= N_LORA_TIME;
			setTime(unixTime + (8 * 60 * 60)); // Philippine time
			char buf[40];
			sprintf(buf, "%02d/%02d/%4d %02d:%02d:%02d", day(), month(), year(), hour(), minute(), second());
			Serial.print("[LOOP] Local time: ");
			Serial.println(buf);
			Serial.flush();
		}
		// Serial input event
		if ((g_task_event_type & AT_CMD) == AT_CMD)
		{
			// Serial.println("[LOOP] AT CMD");
			// Serial.flush();
			g_task_event_type &= N_AT_CMD;
			// at_serial.handleSerial();
			while (Serial.available() > 0)
			{
				at_serial.processIncomingBytes();
				delay(5);
			}
		}
	}
}
