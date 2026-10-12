# Template for a WisBlock application with LoRaWAN communication
| <center><img src="./assets/rakstar.jpg" alt="RAKstar" width=50%></center>  | <center><img src="./assets/RAK-Whirls.png" alt="RAKWireless" width=50%></center> | <center><img src="./assets/WisBlock.png" alt="WisBlock" width=50%></center> | <center><img src="./assets/Yin_yang-48x48.png" alt="BeeGee" width=50%></center>  |
| -- | -- | -- | -- |    

# Overview

This template can be used as a starting point to write a low power application using the WisBlockLoRaWAN library.     
It provides     
- basic _**`setup()`**_ and _**`loop()`**_ functions for an easy start
- using semaphore technique to assure lowest power consumption
- all possible callbacks for LoRaWAN events
- an AT command interface to setup the device with its LoRaWAN credentials and settings like region, datarate, ...
- a timer for frequent data transmission
- custom AT commands to setup the transmission interval and check device settings
- LinkCheckReq and TimeReq every 100 transmissions (configurable in the code) to synchronize the device time and check connection to the LNS
- Cayenne LPP packet formatting for transmitted data packets for easier decoding of the data in the LNS and connected backends.

This template does not set any LoRaWAN credentials and settings in the code, instead it uses AT commands for the setup and stores the setup in the flash memory of the device.    
On a freshly programmed device without settings, it will not start any LoRaWAN actions, it waits until a valid setup is done with AT commands and the setup is saved.     

This template is supporting RAK4631 and RAK3312 WisBlock Core modules using PlatformIO. Device definitions are included in the project structure in the _**rakwireless**_ folder. The _**platformio.ini**_ is setup to automatically add the devices to the PlatformIO Platforms. This is required because some of the WisBlock Core modules are not included in the original PlatformIO Platforms for the nRF52 and ESP32.

# Application structure
The code is split into multiple CPP files for easier maintenance.

## main.cpp
This is the main application code with the Arduino. It includes the basic _**`setup()`**_ and _**`loop()`**_ functions with the minimum required commands for the device start, setup and handling of events.    

### _**`setup()`**_ 
Initialize the basics for the application, like Serial interface, initialization of the LoRaWAN stack, AT commands handling, custom AT commands, preparation of the send interval timer, initialization of the semaphore that is used for wakeup of the device on events.     
In the setup is as well a check for a valid configuration included, that can trigger a special event that forces the _**loop()**_ to just waits for a new reboot while the user (or a production automation) is setting up the device and reset's it. On the next boot, a valid configuration is detected and the device will start to work as designed.    

### _**`loop()`**_
Here all events are handled. Different to usual Arduino loops, it is not running all the time. Instead it waits for a semaphore to be released. While waiting, the MCU is in sleep mode to reduce power consumption.     
The semaphore _**g_task_sem**_ is released by LoRaWAN callbacks and by the timer that controls the interval for sending out data. If required additional code can be added to the template to release the semaphore on events, e.g. an acceleration sensor that is triggered by motion.     
In the template the following events are declared in _**main.h**_:

#### NO_CONFIG
This is a special event, triggered if during _**setup()**_ no valid device configuration was found. This event is just blinking the onboard LED's of the WisBlock Base Board while it keeps the Serial port active to receive AT commands that set the configuration of the device. This event should be only triggered once after a device was flashed from scratch.

#### LORA_JOIN_FIN
This event is triggered when the device could successful join the LoRaWAN network and received a JOIN ACCEPT message from the LNS.     
Here the timer for the interval to sending data packets is started, if the interval time is not set to 0 seconds.

#### LORA_JOIN_FAIL
This event is triggered when the device fails to join the LoRaWAN network after using the number of reattempts to retry to join. The number of reattempts and the interval between join requests can be set with the AT command _**AT+JOIN=**_. Check the [AT command manual](https://github.com/beegee-tokyo/WisBlockLoRaWAN/blob/main/WisBlockLoRaWAN-AT-Commands.md) for details.     
If a network join fails, strategies can be added to retry to join without draining the battery. In this template, the device will sleep for some time before retrying another network join. The sleep time starts with one hour and increases by one hour for each failed attempt to a maximum of six hours between join attempts. _**This is just an example strategy and might not fit with the applications requirements!**_

#### STATUS
This event is triggered by the timer that controls the interval of sending out data. In the template, a "dummy" payload is created and sent out to the LNS. For connection checks, every 100 uplinks it triggers as well a _**LinkCheckRequest **_ and a _**TimeReq**_ to the LNS. The responses to these requests can be used to start actions if the connection to the server is lost. It can as well be used to synchronize the time on the device with the LNS time.

#### LORA_TX_FIN
This event is triggered after a complete transmission cycle of an uplink (from the device to the LNS) is finished. In the template no specific actions are taken, but it could be useful for other use cases.

#### LORA_DATA
This event is triggered if a downlink packet from the LNS was received. In the template the received data is just printed over Serial and no processing of the received data is done. It could be used to send commands from the LNS to the device, e.g. changes of the send interval. Such a decoding and processing of the received data is not included in the code.
Important is to keep the processing of the received data short or save them in a queue for later processing which can be triggered by custom event. If multiple downlinks are received from the LNS they will trigger this event within a short time interval and information might get lost if the MCU is still busy processing the data.

#### LINK_CHECK
This event is triggered by the response to a _**LinkCheckRequest**_ sent to the LNS. The success of the request can be checked with _**last_LinkCheckSuccess**_. If the request was unsuccessful, the connection to the LNS could be interrupted and actions to handle such an interruption can be started here. E.g. a new network join request could be started. In the template, the result is only reported over Serial and no specific actions are taken.

#### LORA_TIME
This event is triggered by the response to a _**TimeReq**_ sent to the LNS. It can be used to update the internal time of the device or to update the time on an RTC chip if time management is required on the device. In the template, the result is only reported over Serial and no specific actions are taken.

#### USB_DATA
This event is triggered if data was received over USB that is _**not**_ an AT or ATC command. The received data is available in _**`char usb_data[256];`**_ as a zero terminated array.

## main.h
This header file combines all required #define and #include commands for definitions and function includes for the application. Forward declarations for variables and functions are as well added here.     
If the application template is extended by additional functions, e.g. reading of sensor data, it is recommended to add required information in the main.h

## callback.cpp
This file contains all callbacks that can trigger events to wakeup the _**loop()**_ with a semaphore.

### _**void periodic_wakeup(TimerHandle_t unused)**_
This is the callback for the timer that triggers the interval sending of uplinks through LoRaWAN.     
It only sets the event flag _**STATUS**_ and releases the semaphore that will wake up the _**loop()**_.

### _**void onUnhandledLine(const char *line)**_
This callback is used when the USB data handler detects a non AT command input. The received data is available in _**const char *line**_. The callback is copying the data into a temporary buffer _**usb_data**_, sets the event flag _**USB_DATA**_ and releases the semaphore that will wake up the _**loop()**_.

### _**`void onJoined(void)`**_
This callback is activated after a successful LoRaWAN network join.    
It only sets the event flag _**LORA_JOIN_FIN**_ and releases the semaphore that will wake up the _**loop()**_.

### _**`void onJoinFailed(void)`**_
This callback is activated after a failed LoRaWAN network join.     
It checks if all retry attempts are used up and sets the event flag _**LORA_JOIN_FAIL**_ and releases the semaphore that will wake up the _**loop()**_.

### _**`onTxDone`**_
This callback is activated after a transmission cycle is finished.     
It only sets the event flag _**LORA_TX_FIN**_ and releases the semaphore that will wake up the _**loop()**_.

### _**`onRxDone`**_
This callback is activated after a downlink packet was received.     
It copies the received data packet and related information into a WisBlockRxResult structure _**`rx_buffered`**_ for processing in the main loop. Then it sets the event flag _**LORA_DATA**_ and releases the semaphore that will wake up the _**loop()**_.

### _**`onTimeAnswer`**_
This callback is activated after the LNS responded to a time request.     
If a time was received, it copies the received unix time into _**`last_unixTime`**_ for processing in the main loop. Then it sets the event flag _**LORA_TIME**_ and releases the semaphore that will wake up the _**loop()**_.     
In case the LNS did not provide a time stamp, the request is silently dropped. If a failed time request needs to be processed, this callback needs to be extended to handle it.     

### _**`onLinkCheck`**_
This callback is activated after the LNS responded to a link check request.     
It copies the received info into a WisBlockLinkCheckResult structure _**`last_linkCheck`**_ and sets the _**`last_LinkCheckSuccess`**_ flag.for processing in the main loop. Then it sets the event flag _**LINK_CHECK**_ and releases the semaphore that will wake up the _**loop()**_.     
The callback does not check whether the link check was successful or not.     

## custom_at.cpp
In this file two custom AT commands are defined and can be activated by calling _**`registerCustomATCommands()`**_.      
Custom settings are saved independent of the WisBlockLoRaWAN configuration.     
It provides the following functions:

### _**`registerCustomATCommands()`**_
Adds two custom AT commands to the AT command list. This function is called from the application. 
- ATC+SENDINT is used the set the send interval used by the application to send uplink packets
- ATC+STATUS is a command that lists all relevant settings for the current LoRa mode.

### _**`WisBlockAtStatus handleSendInt(Stream &port, const char *cmd, char *args)`**_
This function is automatically called by the AT command handler and must not be called directly from the application.     
This is the handler for query or setter calls for the application send interval. In case the send interval as changed, the related timer is stopped and restarted with the new value. This allows as well to stop the automatic sending by setting the send interval to 0 seconds.

### _**`WisBlockAtStatus status_handler(Stream &port, const char *cmd, char *args)`**_
This function is automatically called by the AT command handler and must not be called directly from the application.     
This is a query only function that lists all current device settings.

## wisblock__cayenne.cpp and wisblock_cayenne.h
These two files are extending the _**`CayenneLPP`**_ class of the [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) library provided by ElectronicCats. CayenneLPP implements basic sensor types specified by [OMA SpecWorks](https://www.omaspecworks.org/), formerly IPSO Alliance.    
The class is extended with additional sensor types and functions to add these sensor types to the the payload formatted in CayenneLPP format.

The additonal sensor types can be found in [wisblock_cayenne.h](/src/wisblock_cayenne.h).    
The additional functions to add dataypes are listed below.

### uint8_t addGNSS_4(uint8_t channel, int32_t latitude, int32_t longitude, int32_t altitude);
Add GNSS data in Cayenne LPP standard format with a 0.0001 ° resolution.

### uint8_t addGNSS_6(uint8_t channel, int32_t latitude, int32_t longitude, int32_t altitude);
Add GNSS data in custom Cayenne LPP format with a 0.000001 ° resolution.

### uint8_t addGNSS_H(int32_t latitude, int32_t longitude, int16_t altitude, int16_t accuracy, int16_t battery);
Add GNSS data in Helium Mapper format with a 0.00001 ° resolution

### uint8_t addGNSS_T(int32_t latitude, int32_t longitude, int16_t altitude, float accuracy, int8_t sats);
Add GNSS data in Field Tester format ([RAK10701](https://store.rakwireless.com/products/field-tester-plus-for-lorawan-rak10701-plus) [RAK10706](https://store.rakwireless.com/products/wisblock-signal-meter-for-lora))

### uint8_t addVoc_index(uint8_t channel, uint32_t voc_index);
Add the VOC index as a 2 byte integer value

### uint8_t addSH_2_value(uint8_t channel, uint8_t data_type, float value);
A generic setter to add data in RAKwireless SensorHub format 
Supports 
- SensorHub Soil Moisture data
- SensorHub Soil PH data
- SensorHub Precise Humidity data
- SensorHub Soil EC data
- SensorHub Precision Soil PH data
- SensorHub Windspeed data
- SensorHub Pyranometer data
- SensorHub Wind direction data

### uint8_t addDevID(uint8_t channel, uint8_t *dev_id);
This is a special setter to add a four byte device ID to the payload. 