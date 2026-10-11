/**
 * @file FSKP2PPingPong.ino
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief FSK P2P PingPong example (Master/Slave)
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
 * The radio runs in FSK (GFSK) mode instead of LoRa. The FSK events wake the loop() task with a
 * FreeRTOS semaphore. The library background task and the AT command interface (also in the
 * background) are enabled, so the radio can be checked and changed with AT commands while the
 * PingPong runs, for example:
 *
 *   AT+NWM=?        work mode, 2 = FSK P2P
 *   AT+PFREQ=?      frequency in Hz
 *   AT+PBR=?        bit rate in bit/s
 *   AT+PFDEV=?      frequency deviation in Hz
 *   AT+PBW=?        RX bandwidth in Hz (in FSK mode)
 *   AT+PPL=?        preamble length in bytes (in FSK mode)
 *   AT+PTP=?        TX power in dBm
 *   AT+STATUS       all settings
 *
 * All nodes must use the same frequency, bit rate, deviation, bandwidth, preamble length and sync
 * word. Changing a value with an AT command on one node breaks the link until the other nodes
 * have the same value. The AT commands change the settings in RAM, they are only saved with
 * AT+SAVE, and setup() sets the values of this sketch again after every reset.
 *
 * FSK differences to the LoRa PingPong: there is no SNR (only the RSSI is reported), no CAD, and a
 * packet with a wrong CRC is reported like an RX timeout (the receive window is restarted).
 * With 100 kbit/s a 16 byte packet is on air for about 2 ms.
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

// Define FSK parameters, the same on all nodes
#define PP_FREQUENCY 916400000UL
#define PP_BITRATE 100000		// bit/s
#define PP_DEVIATION 50000		// Hz
#define PP_BANDWIDTH 225000		// Hz, RX bandwidth, at least bit rate + 2 * deviation
#define PP_PREAMBLE_LENGTH 8	// bytes in FSK mode
#define PP_TX_POWER 14			// dBm
static const uint8_t PP_SYNC_WORD[] = {0xC1, 0x94, 0xC1}; // default sync word, 1 to 8 bytes
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

// RX data (there is no SNR in FSK)
int16_t rx_rssi;
uint8_t rx_from;

/**
 * @brief Arduino setup function
 *
 * Starts the serial port, switches the radio to FSK P2P mode and sets all FSK parameters (bit rate,
 * deviation, bandwidth, preamble, TX power, sync word), registers the callbacks, starts the library
 * background task and the AT command interface, creates the wake-up semaphore and starts the first
 * receive window.
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
	Serial.println("WisBlock FSK PingPong test");
	Serial.println("=====================================");

	// Initialize the LoRa chip
	Serial.println("Starting radio init (FSK)");

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_FSK_P2P);
	lora.setP2PFrequency(PP_FREQUENCY);
	lora.setP2PFskBitrate(PP_BITRATE);
	lora.setP2PFskFrequencyDeviation(PP_DEVIATION);
	lora.setP2PFskBandwidth(PP_BANDWIDTH);
	lora.setP2PFskSyncWord(PP_SYNC_WORD, sizeof(PP_SYNC_WORD));
	lora.setP2PPreambleLength(PP_PREAMBLE_LENGTH);
	lora.setP2PTxPower(PP_TX_POWER);
	lora.setP2PRxBoostedGain(true);

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
			Serial.println("OnRxTimeout (or packet with a wrong CRC)");

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
			Serial.printf("RssiValue=%d dBm\n", rx_rssi);

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
 * @brief FSK P2P TX finished callback
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
 * @brief FSK P2P RX finished callback
 *
 * A packet with data: stores the RSSI, checks if it is a PING or a PONG, sets the RX_FIN event
 * and wakes up the loop() task. An empty result is an RX timeout or a packet with a wrong CRC:
 * sets the RX_ERR event instead.
 *
 * @param result RX result with payload, length and RSSI (length 0 on RX timeout or CRC error)
 */
void onRxDone(const WisBlockRxResult &result)
{
	if (result.length > 0)
	{
		Serial.println("OnRxDone CB:");
		rx_rssi = result.rssi;

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
		Serial.println("OnRxError CB (timeout or CRC error)");
		taskEventSet(RX_ERR);
		// Wake up task to send initial packet
		xSemaphoreGive(g_task_sem);
	}
}
