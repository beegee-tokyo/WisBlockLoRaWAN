/**
 * @file LoRaP2PPingPong.ino
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief LoRa P2P PingPong example (Master/Slave)
 * @version 0.1
 * @date 2026-10-04
 *
 * @copyright Copyright (c) 2026
 *
 * @details Two or more identical nodes talk to each other with the 16 byte messages "PING" and "PONG"
 * (followed by a counter pattern). Every node decides by itself if it is Master or Slave. The example
 * is based on the PingPong example of the SX126x-Arduino library.
 *
 *  - After boot a node listens for RX_TIMEOUT_VALUE ms (state NO_STATE).
 *  - RX timeout in NO_STATE: the node becomes MASTER and sends a PING.
 *  - PING received in NO_STATE: the node becomes SLAVE and answers with a PONG.
 *  - PONG or unknown data received in NO_STATE: the node becomes MASTER and sends a PING.
 *  - MASTER: after a PONG it waits 15 seconds and sends the next PING. A PING from another Master
 *    makes it a SLAVE. Unknown data is ignored.
 *  - SLAVE: answers every PING with a PONG. A PONG makes it a MASTER. Unknown data is ignored.
 *  - RX timeout in MASTER or SLAVE state: back to NO_STATE and listen again.
 *
 * The LoRa events wake the loop() task with a FreeRTOS semaphore. The AT command interface and the
 * library background task are enabled, so the radio settings can be checked with AT commands.
 */
#include <Arduino.h>

#include <WisBlockLoRaWAN.h>
#include <WisBlockLoRaAT.h>

#ifdef ARDUINO_ARCH_RP2040
// Arduino-Pico (RAK11300 / RAK11310): needs FreeRTOS SMP (Arduino IDE: Tools -> Operating System -> FreeRTOS SMP,
// PlatformIO: build_flags = -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS) and a flash size with a file system.
#ifndef __FREERTOS
#error "Enable FreeRTOS SMP (Tools -> Operating System, or -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS)"
#endif
#include <FreeRTOS.h>
#include <semphr.h>
#ifndef LED_GREEN
#define LED_GREEN 23 // WisBlock LED1 of the RAK11310, check the pins of your base board
#endif
#ifndef LED_BUILTIN
#define LED_BUILTIN LED_GREEN
#endif
#endif

/**
 * The event flags are set from several tasks (LBM task, USB event task,
 * timers) and cleared from loop(), possibly on different cores. A plain
 * "flags |= x" / "flags &= ~x" is a read-modify-write that can lose an event
 * when two of them overlap, so all updates go through these helpers.
 */
// Event flag values
#define NO_EVENT 0b0000000000000000
#define TX_FIN 0b0000000000000001
#define N_TX_FIN 0b1111111111111110
#define RX_FIN 0b0000000000000100
#define N_RX_FIN 0b1111111111111011
#define RX_ERR 0b0000000000001000
#define N_RX_ERR 0b1111111111110111
/** Flag for the event type */
volatile uint16_t g_task_event_type = NO_EVENT;

// Set and clear event flags
#if defined ARDUINO_ARCH_ESP32
static portMUX_TYPE g_event_mux = portMUX_INITIALIZER_UNLOCKED;
#define EVENT_LOCK() portENTER_CRITICAL(&g_event_mux)
#define EVENT_UNLOCK() portEXIT_CRITICAL(&g_event_mux)
#elif defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
#define EVENT_LOCK() taskENTER_CRITICAL()
#define EVENT_UNLOCK() taskEXIT_CRITICAL()
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

// LoRa driver instance
WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

// Function declarations
void onTxDone(const WisBlockTxResult &result);
void onRxDone(const WisBlockRxResult &result);
void sendPingPong(bool sendPing);

// Define LoRa parameters
#define PP_FREQUENCY 916350000UL
#define PP_SPREADING_FACTOR 7
#define PP_BANDWIDTH WISBLOCK_BW_125
#define PP_CODING_RATE WISBLOCK_CR_4_5
#define PP_PREAMBLE_LENGTH 8
#define PP_TX_POWER 14
#define PP_IQ_INVERSION false
#define PP_SYNC_WORD 0x1424 // 0x1424 private (default), 0x3444 public (LoRaWAN)
#define RX_TIMEOUT_VALUE 30000

#define BUFFER_SIZE 16 // Define the payload size here

uint16_t BufferSize = BUFFER_SIZE;
volatile uint8_t TxdBuffer[BUFFER_SIZE];
const uint8_t PingMsg[] = "PING";
const uint8_t PongMsg[] = "PONG";

#define NO_STATE 0
#define MASTER 1
#define SLAVE 2
volatile uint8_t current_state = NO_STATE;

/** Semaphore used by events to wake up loop task */
SemaphoreHandle_t g_task_sem = NULL;

// RX data
int16_t rx_rssi;
int8_t rx_snr;
uint8_t rx_from;

/**
 * @brief Arduino setup function
 *
 * Starts the serial port, configures all LoRa P2P radio parameters (including IQ inversion and
 * sync word), registers the callbacks, starts the library background task and the AT command
 * interface, creates the wake-up semaphore and starts the first receive window.
 */
void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);

	// Initialize Serial for debug output
	Serial.begin(115200);

	time_t serial_timeout = millis();
	// On nRF52840 the USB serial is not available immediately
	while (!Serial)
	{
		if ((millis() - serial_timeout) < 5000)
		{
			delay(100);
			digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
		}
		else
		{
			break;
		}
	}

	Serial.println("=====================================");
	Serial.println("WisBlock PingPong test");
	Serial.println("=====================================");

	// Initialize the LoRa chip
	Serial.println("Starting lora_hardware_init");

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
	lora.setP2PFrequency(PP_FREQUENCY);
	lora.setP2PSpreadingFactor(PP_SPREADING_FACTOR);
	lora.setP2PBandwidth(PP_BANDWIDTH);
	lora.setP2PCodingRate(PP_CODING_RATE);
	lora.setP2PPreambleLength(PP_PREAMBLE_LENGTH);
	lora.setP2PTxPower(PP_TX_POWER);
	lora.setP2PIqInversion(PP_IQ_INVERSION);
	lora.setP2PSyncWord(PP_SYNC_WORD);
	lora.setP2PRxBoostedGain(true);
	lora.setP2PCad(false);

	// Initialize the Radio callbacks
	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);

	// Start LoRa background task
	lora.enableBackgroundTask();

	// Start AT command handling
	at_serial.begin(lora, Serial);
#if defined ARDUINO_ARCH_NRF52 || defined ESP32 || defined ARDUINO_ARCH_RP2040
	if (!at_serial.enableBackgroundRx())
	{
		Serial.println("[Setup] AT command USB RX hook failed");
	}
#endif
	// Create the task event semaphore
	g_task_sem = xSemaphoreCreateBinary();
	// Initialize semaphore
	xSemaphoreGive(g_task_sem);

	// Take the semaphore so the loop will go to sleep until an event happens
	xSemaphoreTake(g_task_sem, 10);

	// Start LoRa
	Serial.println("Starting Radio.Rx");
	lora.startP2PReceive(RX_TIMEOUT_VALUE);
}

/**
 * @brief Arduino loop function
 *
 * Sleeps on the semaphore until a LoRa event wakes it up. Handles the events TX finished (start
 * receiving), RX timeout and RX done (Master/Slave state machine, see file description).
 */
void loop()
{
	// Wait until semaphore is released (FreeRTOS)
	xSemaphoreTake(g_task_sem, portMAX_DELAY);

	// Handle event
	while (g_task_event_type != NO_EVENT)
	{
		if ((g_task_event_type & TX_FIN) == TX_FIN)
		{
			taskEventClear(N_TX_FIN);
			Serial.println("OnTxDone");
			lora.startP2PReceive(RX_TIMEOUT_VALUE);
		}
		if ((g_task_event_type & RX_ERR) == RX_ERR)
		{
			taskEventClear(N_RX_ERR);
			Serial.println("OnRxTimeout");

			digitalWrite(LED_BUILTIN, LOW);

			if (current_state == NO_STATE)
			{
				Serial.println("Switch NO_STATE ==> MASTER");
				current_state = MASTER;
				sendPingPong(true);
			}
			else
			{
				Serial.println("Reset status to NO_STATE");
				current_state = NO_STATE;
				lora.startP2PReceive(RX_TIMEOUT_VALUE);
			}
		}
		if ((g_task_event_type & RX_FIN) == RX_FIN)
		{
			taskEventClear(N_RX_FIN);
			Serial.println("OnRxDone");
			Serial.printf("RssiValue=%d dBm, SnrValue=%d\n", rx_rssi, rx_snr);

			digitalWrite(LED_BUILTIN, HIGH);

			if (current_state == NO_STATE)
			{
				if (rx_from == MASTER)
				{
					Serial.println("Switch NO_STATE ==> SLAVE");
					current_state = SLAVE;
					sendPingPong(false);
				}
				else
				{
					Serial.println("Switch NO_STATE ==> MASTER");
					current_state = MASTER;
					sendPingPong(true);
				}
			}
			else if (current_state == MASTER)
			{
				if (rx_from == SLAVE)
				{
					Serial.println("I am MASTER");
					current_state = MASTER;
					delay(15000);
					sendPingPong(true);
				}
				else if (rx_from == MASTER)
				{
					Serial.println("Switch MASTER ==> SLAVE");
					current_state = SLAVE;
					sendPingPong(false);
				}
				else
				{
					Serial.println("Unknown package, ignore");
					lora.startP2PReceive(RX_TIMEOUT_VALUE);
				}
			}
			else // SLAVE
			{
				if (rx_from == MASTER)
				{
					Serial.println("I am SLAVE");
					current_state = SLAVE;
					sendPingPong(false);
				}
				else if (rx_from == SLAVE)
				{
					Serial.println("Switch SLAVE ==> MASTER");
					current_state = MASTER;
					sendPingPong(true);
				}
				else
				{
					Serial.println("Unknown package, ignore and go to RX");
					lora.startP2PReceive(RX_TIMEOUT_VALUE);
				}
			}
		}
	}
}

/**
 * @brief Send a PING or a PONG
 *
 * Fills the transmit buffer with "PING" or "PONG" followed by a counter pattern and sends it.
 *
 * @param sendPing true: send a PING, false: send a PONG
 */
void sendPingPong(bool sendPing)
{
	if (sendPing)
	{
		Serial.println("Sending a PING");
		// Send the reply to the PONG string
		TxdBuffer[0] = 'P';
		TxdBuffer[1] = 'I';
		TxdBuffer[2] = 'N';
		TxdBuffer[3] = 'G';
	}
	else
	{
		Serial.println("Sending a PONG");
		// Send the reply to the PONG string
		TxdBuffer[0] = 'P';
		TxdBuffer[1] = 'O';
		TxdBuffer[2] = 'N';
		TxdBuffer[3] = 'G';
	}

	// We fill the buffer with numbers for the payload
	for (int i = 4; i < BufferSize; i++)
	{
		TxdBuffer[i] = i - 4;
	}

	// Serial.println("Sending:");
	// for (int idx = 0; idx < BufferSize; idx++)
	// {
	// 	Serial.printf("0x%X ", TxdBuffer[idx]);
	// }
	// Serial.println("");
	lora.sendP2P((uint8_t *)TxdBuffer, (uint8_t)BufferSize);
}

/**
 * @brief LoRa P2P TX finished callback
 *
 * Sets the TX_FIN event and wakes up the loop() task.
 *
 * @param result TX result, result.success is true if the packet was sent
 */
void onTxDone(const WisBlockTxResult &result)
{
	Serial.println("OnTxDone CB");
	taskEventSet(TX_FIN);
	// Wake up task to send initial packet
	xSemaphoreGive(g_task_sem);
}

/**
 * @brief LoRa P2P RX finished callback
 *
 * A packet with data: stores RSSI and SNR, checks if it is a PING or a PONG, sets the RX_FIN event
 * and wakes up the loop() task. An empty result is an RX timeout: sets the RX_ERR event instead.
 *
 * @param result RX result with payload, length, RSSI and SNR (length 0 on RX timeout)
 */
void onRxDone(const WisBlockRxResult &result)
{
	if (result.length > 0)
	{
		Serial.println("OnRxDone CB:");
		rx_rssi = result.rssi;
		rx_snr = result.snr;

		// Print RX for testing
		// for (int idx = 0; idx < result.length; idx++)
		// {
		// 	Serial.printf("0x%X ", result.data[idx]);
		// }
		// Serial.println("");

		if (strncmp((const char *)result.data, (const char *)PingMsg, 4) == 0)
		{
			Serial.println("RX PING");
			rx_from = MASTER;
		}
		else if (strncmp((const char *)result.data, (const char *)PongMsg, 4) == 0)
		{
			Serial.println("RX PONG");
			rx_from = SLAVE;
		}
		else
		{
			Serial.println("RX unknown");
			rx_from = NO_STATE;
		}
		// Wake up task to send initial packet
		taskEventSet(RX_FIN);
		xSemaphoreGive(g_task_sem);
	}
	else // RX timeout
	{
		Serial.println("OnRxError CB");
		taskEventSet(RX_ERR);
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
}
