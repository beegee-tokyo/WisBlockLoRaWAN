#include "wisblock_lbm_task.h"
#include <Arduino.h>

#if defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <semphr.h>
#include <timers.h>
#include <task.h>
#define WISBLOCK_LBM_TASK_HAS_FREERTOS 1
#elif defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <freertos/task.h>
#define WISBLOCK_LBM_TASK_HAS_FREERTOS 1
#elif defined(ARDUINO_ARCH_RP2040)
// Not available on the plain arduino-pico core out of the box - only
// compiles in if a FreeRTOS-Kernel port has been added to the project
// (e.g. arduino-pico's "FreeRTOS" core option, where available). See the
// header comment for details.
#if __has_include(<FreeRTOS.h>)
#include <FreeRTOS.h>
#include <semphr.h>
#include <timers.h>
#include <task.h>
#define WISBLOCK_LBM_TASK_HAS_FREERTOS 1
#endif
#endif

#if defined(WISBLOCK_LBM_TASK_HAS_FREERTOS)

namespace
{
constexpr uint32_t kStackSize = 4096; // matches SX126x-Arduino's own start_lora_task() default
constexpr UBaseType_t kTaskPriority = 1; // matches SX126x-Arduino's TASK_PRIO_NORMAL

// First wait after the task starts, before eventHandler has ever run once -
// bounded (not portMAX_DELAY) specifically so a join()/send()/etc. call
// made shortly after enableBackgroundTask() (before any external trigger
// has fired) still gets picked up promptly instead of stalling forever.
constexpr uint32_t kInitialWaitMs = 100;

// Safety ceiling on how long the task will ever wait, regardless of what
// eventHandler returns - keeps a periodic "still alive" check-in even if
// an engine misreports an enormous or bogus deadline.
constexpr uint32_t kMaxWaitMs = 60000;

SemaphoreHandle_t eventSemaphore = NULL;
TaskHandle_t eventTaskHandle = NULL;
TimerHandle_t swTimerHandle = NULL;
SemaphoreHandle_t lbmMutex = NULL;
uint32_t (*registeredEventHandler)() = nullptr;
void (*registeredTimerCallback)(void *context) = nullptr;
void *registeredTimerContext = nullptr;
bool active = false;

void eventTask(void *pvParameters)
{
	(void)pvParameters;
	TickType_t waitTicks = pdMS_TO_TICKS(kInitialWaitMs);
	while (true)
	{
		xSemaphoreTake(eventSemaphore, waitTicks);

		// Whether this wake came from an actual semaphore give (DIO1 IRQ,
		// timer expiry) or just from the bounded wait timing out, always
		// re-pump the handler - LBM's smtc_modem_run_engine() contract
		// requires being called again within its last reported budget
		// regardless of why we woke up.
		uint32_t nextWaitMs = kMaxWaitMs;
		if (registeredEventHandler)
		{
			xSemaphoreTake(lbmMutex, portMAX_DELAY);
			nextWaitMs = registeredEventHandler();
			xSemaphoreGive(lbmMutex);
		}
		if (nextWaitMs > kMaxWaitMs)
		{
			nextWaitMs = kMaxWaitMs;
		}
		waitTicks = pdMS_TO_TICKS(nextWaitMs);
		if (waitTicks == 0)
		{
			waitTicks = 1; // avoid a zero-tick busy-loop if the engine ever reports "call me back immediately"
		}
	}
}

// Runs in the FreeRTOS Timer Service Task context (a normal task, not an
// ISR) - safe to call the LBM-registered callback directly and to use the
// non-ISR semaphore give.
void swTimerExpired(TimerHandle_t handle)
{
	(void)handle;
	xSemaphoreTake(lbmMutex, portMAX_DELAY);
	if (registeredTimerCallback)
	{
		registeredTimerCallback(registeredTimerContext);
	}
	xSemaphoreGive(lbmMutex);
	if (eventSemaphore)
	{
		xSemaphoreGive(eventSemaphore);
	}
}
} // namespace

namespace WisBlockLbmTask
{
bool start(uint32_t (*eventHandler)())
{
	registeredEventHandler = eventHandler;

	eventSemaphore = xSemaphoreCreateBinary();
	if (eventSemaphore == NULL)
	{
		return false;
	}
	// Start "empty" - matches SX126x-Arduino's own start_lora_task()
	// pattern (give then immediately take), so the very first
	// xSemaphoreTake() in eventTask() blocks until real work arrives
	// instead of firing once immediately on a freshly-created semaphore.
	xSemaphoreGive(eventSemaphore);
	xSemaphoreTake(eventSemaphore, 0);

	lbmMutex = xSemaphoreCreateMutex();
	if (lbmMutex == NULL)
	{
		return false;
	}

	// One-shot, reused via xTimerChangePeriod()/xTimerStart() on every
	// scheduleTimer() call rather than creating a new timer each time.
	swTimerHandle = xTimerCreate("LBM_TMR", pdMS_TO_TICKS(1000), pdFALSE, NULL, swTimerExpired);
	if (swTimerHandle == NULL)
	{
		return false;
	}

	if (xTaskCreate(eventTask, "LBM_EVT", kStackSize, NULL, kTaskPriority, &eventTaskHandle) != pdPASS)
	{
		return false;
	}

	active = true;
	return true;
}

void lock()
{
	if (active && lbmMutex)
	{
		xSemaphoreTake(lbmMutex, portMAX_DELAY);
	}
}

void unlock()
{
	if (active && lbmMutex)
	{
		xSemaphoreGive(lbmMutex);
	}
}

bool isActive()
{
	return active;
}

void notifyFromISR()
{
	if (!active)
	{
		return;
	}
	BaseType_t higherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(eventSemaphore, &higherPriorityTaskWoken);
	portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

void notify()
{
	if (!active)
	{
		return;
	}
	xSemaphoreGive(eventSemaphore);
}

void scheduleTimer(uint32_t milliseconds, void (*callback)(void *context), void *context)
{
	if (!active)
	{
		return;
	}
	registeredTimerCallback = callback;
	registeredTimerContext = context;

	TickType_t ticks = pdMS_TO_TICKS(milliseconds);
	if (ticks == 0)
	{
		ticks = 1; // 0 would mean "fire immediately with no delay", not intended here
	}
	xTimerChangePeriod(swTimerHandle, ticks, 0);
	xTimerStart(swTimerHandle, 0);
}

void cancelTimer()
{
	if (!active)
	{
		return;
	}
	xTimerStop(swTimerHandle, 0);
}
} // namespace WisBlockLbmTask

#else // !WISBLOCK_LBM_TASK_HAS_FREERTOS

namespace WisBlockLbmTask
{
bool start(uint32_t (*eventHandler)())
{
	(void)eventHandler;
	return false; // caller should fall back to manual handleEvents() polling
}

bool isActive()
{
	return false;
}

void notifyFromISR()
{
	// no-op
}

void notify()
{
	// no-op - bare-metal build has no background task to wake; the
	// application's own loop()-polled handleEvents() calls are what drive
	// smtc_modem_run_engine() in that mode instead.
}

void scheduleTimer(uint32_t milliseconds, void (*callback)(void *context), void *context)
{
	(void)milliseconds;
	(void)callback;
	(void)context;
	// no-op - falls back to WisBlockLbmPort's own millis()-polled timer
}

void cancelTimer()
{
	// no-op
}

void lock()
{
	// no-op - single-threaded bare-metal build, nothing to serialize against
}

void unlock()
{
	// no-op
}
} // namespace WisBlockLbmTask

#endif // WISBLOCK_LBM_TASK_HAS_FREERTOS
