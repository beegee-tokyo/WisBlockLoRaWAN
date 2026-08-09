/**
 * BasicLoRaWAN.ino
 * OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.
 * Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h
 * has the right pins for your revision and LBM is vendored in (see README).
 */
#include "main.h"

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

// Replace with your device's real OTAA credentials.
uint8_t devEui[8] = {0xac, 0x1f, 0x09, 0xff, 0xfe, 0x06, 0x79, 0xdb};
uint8_t joinEui[8] = {0x70, 0xb3, 0xd5, 0x7e, 0xd0, 0x02, 0x01, 0xe1};
uint8_t appKey[16] = {0x2b, 0x84, 0xe0, 0xb0, 0x9b, 0x68, 0xe5, 0xcb, 0x42, 0x17, 0x6f, 0xe7, 0x53, 0xdc, 0xee, 0x79};

const uint32_t UPLINK_INTERVAL_MS = 30000;

/** Flag for the event type */
volatile uint16_t g_task_event_type = 0;

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
	// Switch on LED to show we are awake
	digitalWrite(LED_GREEN, HIGH);
	g_task_event_type |= STATUS;
	if (g_task_sem != NULL)
	{
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
	// Switch on LED to show we are awake
	digitalWrite(LED_GREEN, HIGH);
	g_task_event_type |= STATUS;
	if (g_task_sem != NULL)
	{
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
	g_task_event_type |= STATUS;
	if (g_task_sem != NULL)
	{
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
	Serial.flush();
}

void onJoinFailed()
{
	Serial.println("[LoRaWAN] Join failed, retrying...");
	lora.join();
	Serial.flush();
}

void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[LoRaWAN] TX %s, airtime %lu ms\n", result.success ? "OK" : "FAILED", result.airtimeMs);
	Serial.flush();
}

void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[LoRaWAN] RX %u bytes on port %u, RSSI %d SNR %d\n",
				  result.length, result.port, result.rssi, result.snr);
	if (result.length > 0)
	{
		for (int idx = 0; idx < result.length; idx++)
		{
			Serial.printf("0x%X ", result.data[idx]);
		}
		Serial.println("");
	}
	Serial.flush();
}

void onTimeAnswer(bool success, const WisBlockTimeAnswer &t)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Network time: %lu s (GPS epoch)\n", t.gpsEpochSeconds);
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

	Serial.println("[LoRaWAN] lora.begin");
	lora.begin();
	Serial.println("[LoRaWAN] setup");
	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
	lora.setRegion(WISBLOCK_REGION_AS923_3);
	lora.setDeviceClass(WISBLOCK_CLASS_A);
	lora.setTxPower(0);
	lora.setADR(false);
	lora.setConfirmedUplinks(false);

	Serial.println("[LoRaWAN] set callbacks");
	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onTimeRequestAnswer(onTimeAnswer);
	lora.onLinkCheckAnswer(onLinkCheck);

	Serial.println("[LoRaWAN] save");
	lora.saveConfig();

	if (lora.enableBackgroundTask())
	{
		Serial.println("[LoRaWAN] Background task active - loop() no longer needs handleEvents()");
	}
	else
	{
		Serial.println("[LoRaWAN] FreeRTOS unavailable, falling back to loop()-polled handleEvents()");
	}

	Serial.println("[LoRaWAN] join");
	lora.join();
	at_serial.begin(lora, Serial);

	// Prepare timer and seamphore to wake up loop for frequent sending
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
// Initialize the timer for frequent sending
#if defined ARDUINO_ARCH_NRF52
	g_task_wakeup_timer = xTimerCreate(NULL, mypdMS_TO_TICKS(UPLINK_INTERVAL_MS), true, NULL, periodic_wakeup);
	// FIX: xTimerCreate() above already creates this timer with the correct
	// period - the xTimerChangePeriod() call below was setting it to the
	// exact same value it already had, achieving nothing except a serious
	// side effect: per FreeRTOS's own documented behavior,
	// xTimerChangePeriod() on a dormant (created but never started) timer
	// auto-starts it. The xTimerStart() call right after it was then a
	// second, redundant start request racing the first - both get posted
	// to the timer command queue almost back-to-back, and could result in
	// two expiry notifications close together instead of one, silently
	// dropping whichever of two near-simultaneous sendLoRaWAN() calls
	// didn't win the race to be the pending uplink (see the README's "First
	// send after join lost" note for how this actually presented in a real
	// log capture). A single xTimerStart() on the already-correctly-
	// configured dormant timer is the whole fix.
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
#elif defined ESP32
	g_task_wakeup_timer.attach_ms(UPLINK_INTERVAL_MS, periodic_wakeup);
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

		Serial.println("[Loop] Wakeup");

		if ((g_task_event_type & STATUS) == STATUS)
		{
			g_task_event_type &= N_STATUS;
			if (lora.isJoined())
			{
				Serial.println("[Loop] Send");
				uint8_t payload[7] = {0x01, 0x74, 0x01, 0x93, 0x30, 0x66, 0x01};
				lora.sendLoRaWAN(1, payload, sizeof(payload));
			}
		}
		// Serial input event
		if ((g_task_event_type & AT_CMD) == AT_CMD)
		{
			Serial.println("[LOOP] AT CMD");
			Serial.flush();
			g_task_event_type &= N_AT_CMD;
			at_serial.handleSerial();
		}
	}
}
