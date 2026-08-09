/**
 * BasicLoRaWAN.ino
 * OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.
 * Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h
 * has the right pins for your revision and LBM is vendored in (see README).
 */
#include "main.h"

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

bool waitingForCad = false;
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

void onTxDone(const WisBlockTxResult &result)
{
	waitingForCad = false;
	Serial.printf("[P2P] TX %s\n", result.success ? "OK" : "FAILED");
	lora.startP2PReceive(UPLINK_INTERVAL_MS / 2); // listen for 1/2 of sleep time after each TX
	Serial.flush();
}

void onRxDone(const WisBlockRxResult &result)
{
	if (result.length > 0)
	{
		Serial.printf("[P2P] RX %u bytes, RSSI %d SNR %d\n", result.length, result.rssi, result.snr);
		for (int idx = 0; idx < result.length; idx++)
		{
			Serial.printf("0x%X ", result.data[idx]);
		}
		Serial.println("");
	}
	else
	{
		// length == 0 also covers the RX_TIMEOUT case (no packet arrived
		// within the window opened in onTxDone()) - either way, the RX
		// window has now concluded, so there's nothing keeping the radio
		// busy until the next scheduled wake. Sleep it - see
		// LoRaP2PEngine::sleep()'s doc comment for why this matters: left
		// unslept, the radio sits in STANDBY drawing several mA
		// continuously (with the TCXO active) instead of the ~1.5uA SLEEP
		// mode achieves.
		Serial.println("[P2P] RX window closed, nothing received");
	}
	lora.sleepRadio();
	Serial.flush();
}

void onCad(WisBlockCADResult result)
{
	waitingForCad = false;
	if (result == WISBLOCK_CAD_CHANNEL_CLEAR)
	{
		Serial.println("[P2P] Channel free, start TX");
		uint8_t payload[] = "hello p2p";
		lora.sendP2P(payload, sizeof(payload) - 1);
	}
	else
	{
		Serial.println("[P2P] Channel busy, skipping TX this cycle");
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

	Serial.println("[P2P] lora.begin");
	lora.begin();
	Serial.println("[P2P] setup");
	Serial.flush();
	lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
	lora.setP2PFrequency(916000000UL);
	lora.setP2PSpreadingFactor(7);
	lora.setP2PBandwidth(WISBLOCK_BW_125);
	lora.setP2PCodingRate(WISBLOCK_CR_4_5);
	lora.setP2PPreambleLength(8);
	lora.setP2PTxPower(22);
	lora.setP2PCad(true);

	Serial.println("[P2P] lora callbacks");

	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);
	lora.onP2PCadResult(onCad);

	Serial.println("[P2P] save");
	lora.saveConfig();

	if (lora.enableBackgroundTask())
	{
		Serial.println("[P2P] Background task active - loop() no longer needs handleEvents()");
	}
	else
	{
		Serial.println("[P2P] FreeRTOS unavailable, falling back to loop()-polled handleEvents()");
	}

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
	// period - the xTimerChangePeriod() call that used to be here was
	// setting it to the exact same value it already had, achieving nothing
	// except a serious side effect: per FreeRTOS's own documented behavior,
	// xTimerChangePeriod() on a dormant (created but never started) timer
	// auto-starts it. The xTimerStart() call right after it was then a
	// second, redundant start request racing the first - both get posted
	// to the timer command queue almost back-to-back, and could result in
	// two expiry notifications close together instead of one. A single
	// xTimerStart() on the already-correctly-configured dormant timer is
	// the whole fix - see the same note in examples/LowPowerLoRaWAN, where
	// this was actually caught in a real log capture.
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
			if (!waitingForCad)
			{
				waitingForCad = true;
				Serial.println("[P2P] lora.startP2PCad");
				lora.startP2PCad();
				// Send directly
				// uint8_t payload[] = "hello p2p";
				// lora.sendP2P(payload, sizeof(payload) - 1);
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
