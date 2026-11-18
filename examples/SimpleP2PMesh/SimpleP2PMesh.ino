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
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040 || defined ARDUINO_ARCH_ESP32
/** Timer to wakeup task frequently and send message */
TimerHandle_t g_task_wakeup_timer;
#endif

/** Uplink buffer in Cayenne LPP format */
WisCayenne g_solution_data(255);
/** Counter for packages */
uint32_t package_cnt = 0;

/** Counter for join attempts */
uint8_t join_attempt = 0;

/** Flag if OLED was found */
bool has_rak1921 = false;

/** Buffer for OLED output */
char line_str[256];

/** Buffer for BLE/Mesh data */
char data_buffer[512] = {0};

/** Master Node address */
uint32_t g_master_node_addr = 0x00000000;

/** Counter, only for testing */
uint64_t msg_cnt = 0;

/** Union to convert uint32_t into uint8_t array */
longlong_byte_u convert_value;

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
	Serial.println("WisBlock Basic Modem P2P MESH example application");
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

	// Initialize battery readings
	init_batt();

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
	MYLOG("SETUP", "Send interval %d s", (int) custom_settings.sendIntervalS);
	if (custom_settings.sendIntervalS == 0)
	{
		custom_settings.sendIntervalS = 30;
	}
	MYLOG("SETUP", "Define timer");

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040 || defined ARDUINO_ARCH_ESP32
	// Initialize the timer for frequent sending
	g_task_wakeup_timer = xTimerCreate(NULL, mypdMS_TO_TICKS(custom_settings.sendIntervalS * 1000), pdTRUE, (void *)0, periodic_wakeup);

	// Start the timer for frequent sending
	if (custom_settings.sendIntervalS != 0)
	{
		xTimerStart(g_task_wakeup_timer, 0);
	}
#else
#warning MCU not supported
#endif
	MYLOG("SETUP", "Send interval %d s", (int) custom_settings.sendIntervalS);
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

	// Prepare Mesh functions and display
	has_rak1921 = init_rak1921();
	float batt = read_batt();
	for (int rd_lp = 0; rd_lp < 10; rd_lp++)
	{
		batt += read_batt();
		batt = batt / 2;
	}
	if (has_rak1921)
	{
		sprintf(line_str, "RUI3 Mesh - B %.2fV", batt / 1000);
		rak1921_write_header(line_str);
	}
	g_mesh_events.data_avail_cb = on_mesh_data;
	g_mesh_events.map_changed_cb = map_changed_cb;

	// Start LoRa RX continuius with TX enabled
	lora.startP2PReceive(WISBLOCK_P2P_RX_CONTINUOUS_TX);

	// Start LoRa Mesh
	init_mesh(&g_mesh_events);

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

			print_mesh_map_oled();

			// Create a dummy data packet with 3x 64bit counter value
			msg_cnt++;

			convert_value.l_value = msg_cnt;
			memcpy(&data_buffer[0], convert_value.b_values, 8);
			memcpy(&data_buffer[8], convert_value.b_values, 8);
			memcpy(&data_buffer[16], convert_value.b_values, 8);

			// Select a random node from the map
			uint8_t selected_node_idx = 0;
			// Select broadcast as default
			bool use_broadcast = true;
			// Target node address;
			uint32_t node_addr = g_master_node_addr;

			if ((g_master_node_addr != 0) && (g_master_node_addr != 0xFFFFFFFF))
			{
				node_addr = g_master_node_addr;
				g_nodes_list_s route;
				// Check if we have a route to the master
				if (get_route(g_master_node_addr, &route))
				{
					MYLOG("APP", "Send to master node %08lX", g_master_node_addr);
					use_broadcast = false;
				}
				else
				{
					// No route, send as broadcast
					MYLOG("APP", "No route to master node %08lX", g_master_node_addr);
					use_broadcast = true;
				}
			}
			else
			{
				// Get the number of nodes in the map
				uint8_t node_index = nodes_in_map() + 1;
				MYLOG("APP", "%d nodes in the map", node_index);

				// Check how many nodes are in the map
				if (node_index > 2)
				{
					// Multiple nodes, select a random one
					selected_node_idx = (uint8_t)random(1, (long)node_index - 1);
					use_broadcast = false;
					MYLOG("APP", "Using node %d from the map", selected_node_idx);
				}
				else if (node_index == 2)
				{
					// Only 2 nodes in the map, send to the other one
					selected_node_idx = 1;
					use_broadcast = false;
					MYLOG("APP", "Using node 1 from the map");
				}
				else
				{
					// No other node, lets send a broadcast
					selected_node_idx = 0;
					use_broadcast = false;
					MYLOG("APP", "Empty map, using broadcast");
				}
				node_addr = get_node_addr(selected_node_idx);
				MYLOG("APP", "Got receiver address %08lX", node_addr);
			}

			// Enqueue the data package for sending
			uint8_t data_size = 24;
			if (!send_to_mesh(use_broadcast, node_addr, (uint8_t *)data_buffer, data_size))
			{
				MYLOG("APP", "Could not enqueue packet for sending");
			}
		}
		// Mesh Map changed
		if ((g_task_event_type & MESH_MAP_CHANGED) == MESH_MAP_CHANGED)
		{
			taskEventClear(N_MESH_MAP_CHANGED);
			MYLOG("APP", "Mesh Map has changed!");
			// Print the node map
			print_mesh_map();
		}
		// TX finished
		if ((g_task_event_type & LORA_TX_FIN) == LORA_TX_FIN)
		{
			taskEventClear(N_LORA_TX_FIN);
			// Call Mesh RX callback to analyze the received package
			MYLOG("APP", "TX finished");
			// Call Mesh TX callback to check if further actions are required
			mesh_check_tx();
			print_mesh_map_oled();
		}

		// Downlink data received
		if ((g_task_event_type & LORA_DATA) == LORA_DATA)
		{
			taskEventClear(N_LORA_DATA);
			// Call Mesh RX callback to analyze the received package
			mesh_check_rx(rx_buffered.data, rx_buffered.length, rx_buffered.rssi, rx_buffered.snr);

			print_mesh_map_oled();
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

/**
 * @brief Callback after a LoRa Mesh data package was received
 *
 * @param fromID		Sender Node address
 * @param rxPayload		Pointer to the received data
 * @param rxSize		Length of the received package
 * @param rxRssi		Signal strength while the package was received
 * @param rxSnr			Signal to noise ratio while the package was received
 */
void on_mesh_data(uint32_t fromID, uint8_t *rxPayload, uint16_t rxSize, int16_t rxRssi, int8_t rxSnr)
{
	Serial.println("-------------------------------------");
	Serial.printf("Got data from node %08lX\n", fromID);

	longlong_byte_u new_counter;

	// Demo output of the received data packet
	memcpy(new_counter.b_values, &rxPayload[0], 8);
	Serial.printf("Got counter %Ld\n", new_counter.l_value);
	memcpy(new_counter.b_values, &rxPayload[8], 8);
	Serial.printf("Sensor Value 1 %Ld\n", new_counter.l_value);
	memcpy(new_counter.b_values, &rxPayload[16], 8);
	Serial.printf("Sensor Value 2 %Ld\n", new_counter.l_value);

	Serial.println("-------------------------------------");
}

/**
 * @brief Callback after the nodes list changed
 *
 */
void map_changed_cb(void)
{
	// No actions required, but can be used to react to a change in the Mesh Map
	// Do not use long code here, just set a flag and handle the event in the app_event_handler()
	if (g_task_sem != NULL)
	{
		taskEventSet(MESH_MAP_CHANGED);
		// Wake up task to report CAD fail
		xSemaphoreGive(g_task_sem);
	}
}