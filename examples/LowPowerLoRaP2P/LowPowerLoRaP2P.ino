/**
 * @file LowPowerLoRaP2P.ino
 * @brief Low power LoRa P2P example: periodic CAD-gated transmission, radio in sleep between
 *
 * @details LoRa P2P mode (916 MHz, SF7, BW 125 kHz, CR 4/5, 22 dBm, CAD enabled). A timer wakes up the
 * loop() task every UPLINK_INTERVAL_MS (30 seconds). Then a channel activity detection (CAD) runs
 * and, if the channel is clear, "hello p2p" is sent. After the TX the node listens until shortly
 * before the next wake-up. When the receive window is over the radio is put into sleep.
 * In between, loop() waits on a FreeRTOS semaphore, so the MCU can sleep.
 *
 * Supports RAK4631 (nRF52840) and RAK3312 (ESP32-S3). The library background task handles the LoRa
 * events. AT commands: USB receive only wakes up loop() through a wake callback (atRxWake()), the
 * command is processed in loop() by at_serial.handleSerial().
 */
#include "main.h"

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

bool waitingForCad = false;
const uint32_t UPLINK_INTERVAL_MS = 30000;

/** Flag for the event type */
volatile uint16_t g_task_event_type = 0;

/**
 * The event flags are set from several tasks (LBM task, USB event task,
 * timers) and cleared from loop(), possibly on different cores. A plain
 * "flags |= x" / "flags &= ~x" is a read-modify-write that can lose an event
 * when two of them overlap, so all updates go through these helpers.
 */
#if defined ARDUINO_ARCH_ESP32
static portMUX_TYPE g_event_mux = portMUX_INITIALIZER_UNLOCKED;
#define EVENT_LOCK() portENTER_CRITICAL(&g_event_mux)
#define EVENT_UNLOCK() portEXIT_CRITICAL(&g_event_mux)
#elif defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
#define EVENT_LOCK() taskENTER_CRITICAL()
#define EVENT_UNLOCK() taskEXIT_CRITICAL()
#else
#define EVENT_LOCK()
#define EVENT_UNLOCK()
#endif

/**
 * @brief Set event flags
 *
 * The event flags are set from several tasks (LBM task, USB event task, timers) and cleared from
 * loop(), possibly on different cores. A plain `flags |= x` is a read-modify-write that can lose an
 * event when two of them overlap, so all updates run in a critical section.
 *
 * @param bits Event flag bits to set, e.g. STATUS
 */
static inline void taskEventSet(uint16_t bits)
{
	EVENT_LOCK();
	g_task_event_type |= bits;
	EVENT_UNLOCK();
}

/**
 * @brief Clear event flags
 *
 * Counterpart of taskEventSet(), runs in a critical section as well.
 *
 * @param mask Inverted event mask, e.g. N_AT_CMD (all bits set except the one to clear)
 */
static inline void taskEventClear(uint16_t mask)
{
	EVENT_LOCK();
	g_task_event_type &= mask;
	EVENT_UNLOCK();
}

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
// Define alternate pdMS_TO_TICKS that casts uint64_t for long intervals due to limitation in nrf52840 BSP
#define mypdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))

// Prepare timer and seamphore to wake up loop for frequent sending
/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;

/** Timer to wakeup task frequently and send message */
TimerHandle_t g_task_wakeup_timer;
/**
 * @brief Timer callback that wakes up the loop task regularly (nRF52)
 *
 * Switches on the green LED, sets the STATUS event and gives the semaphore.
 *
 * @param unused FreeRTOS timer handle, not used
 */
void periodic_wakeup(TimerHandle_t unused)
{
	// Switch on LED to show we are awake
	digitalWrite(LED_GREEN, HIGH);
	taskEventSet(STATUS);
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
 * @brief Timer callback that wakes up the loop task regularly (ESP32)
 *
 * Switches on the green LED, sets the STATUS event and gives the semaphore.
 */
void periodic_wakeup(void)
{
	// Switch on LED to show we are awake
	digitalWrite(LED_GREEN, HIGH);
	taskEventSet(STATUS);
	if (g_task_sem != NULL)
	{
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
}
#else
#warning MCU not supported
#endif

#if defined ARDUINO_ARCH_NRF52 || defined ESP32
/**
 * @brief Called by WisBlockLoRaAT when USB CDC RX data arrives
 *
 * Runs in the USB driver's task context, so it only sets the AT_CMD event and wakes up loop(). The
 * AT command itself is parsed and executed in loop() by at_serial.handleSerial().
 */
void atRxWake(void)
{
	taskEventSet(AT_CMD);
	if (g_task_sem == NULL)
	{
		return;
	}
#if defined ARDUINO_ARCH_NRF52
	bool inIsr = isInISR();
#else
	bool inIsr = xPortInIsrContext();
#endif
	if (inIsr)
	{
		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(g_task_sem, &woken);
		portYIELD_FROM_ISR(woken);
	}
	else
	{
		xSemaphoreGive(g_task_sem);
	}
}
#endif

/**
 * @brief LoRa P2P TX finished callback
 *
 * Prints the result and starts a receive window that ends shortly before the next wake-up.
 *
 * @param result TX result, result.success is true if the packet was sent
 */
void onTxDone(const WisBlockTxResult &result)
{
	waitingForCad = false;
	Serial.printf("[P2P] TX %s\n", result.success ? "OK" : "FAILED");
	lora.startP2PReceive(UPLINK_INTERVAL_MS -1000); // / 2); // listen for 1/2 of sleep time after each TX
	Serial.flush();
}

/**
 * @brief LoRa P2P RX finished callback
 *
 * Prints the received packet (length, RSSI, SNR and data). A length of 0 means the receive window
 * ended without a packet. In both cases the radio is put into sleep, because nothing keeps it busy
 * until the next wake-up.
 *
 * @param result RX result with payload, length, RSSI and SNR
 */
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

/**
 * @brief LoRa P2P channel activity detection (CAD) result callback
 *
 * Sends "hello p2p" if the channel is clear, otherwise the transmission of this cycle is skipped.
 *
 * @param result CAD result, WISBLOCK_CAD_CHANNEL_CLEAR or WISBLOCK_CAD_CHANNEL_DETECTED
 */
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

/**
 * @brief Arduino setup function
 *
 * Starts the serial port, configures LoRa P2P, registers the callbacks, saves the configuration,
 * starts the library background task and the AT command interface, creates the wake-up semaphore
 * and starts the periodic wake-up timer.
 */
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
#if defined ARDUINO_ARCH_NRF52 || defined ESP32
	// USB RX only wakes loop() (atRxWake), the AT commands are processed in
	// loop() with at_serial.handleSerial()
	at_serial.setRxWakeCallback(atRxWake);
	if (!at_serial.enableBackgroundRx())
	{
		Serial.println("[Setup] AT command USB RX hook failed");
	}
#elif defined ARDUINO_ARCH_RP2040
	// Arduino-Pico: Serial has no receive notification, so there is no wake callback. The AT task of
	// the library (needs lora.enableBackgroundTask(), FreeRTOS SMP) polls Serial and runs the commands.
	if (!at_serial.enableBackgroundRx())
	{
		Serial.println("[Setup] AT command background task failed");
	}
#endif

	// Prepare timer and seamphore to wake up loop for frequent sending
#if defined ARDUINO_ARCH_NRF52 || defined ESP32 || defined ARDUINO_ARCH_RP2040
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
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
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

/**
 * @brief Arduino loop function
 *
 * Sleeps on the semaphore until an event wakes it up. A STATUS event (timer) starts a CAD, an AT_CMD
 * event (USB data) lets at_serial.handleSerial() process the AT command.
 */
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
			taskEventClear(N_STATUS);
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
			taskEventClear(N_AT_CMD);
			// Reads and executes everything that is available
			at_serial.handleSerial();
		}
	}
}
