#ifndef __PXT_PLATFORM_H
#define __PXT_PLATFORM_H

// RIC
#ifndef __EMSCRIPTEN__
	#include "esp_timer.h"
	#include "esp_system.h"
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
#endif

#define PXT_ESP32 1

#if CONFIG_IDF_TARGET_ESP32S2
	RIC por aqui NO entra
	#define PXT_USB 1
#else
	// RIC por aqui entra
	#define PXT_USB 0
#endif

#define PAGE_SIZE			1024 // not really

#define DEV_NUM_PINS		50

#define DEV_PWM_PINS		0xffff_ffff_ffffULL
#define DEV_AIN_PINS		0ULL

// Codal doesn't yet distinguish between PWM and AIN
#define DEV_ANALOG_PINS		(DEV_PWM_PINS | DEV_AIN_PINS)

namespace pxt {
class ZPin;
class AbstractButton;
class MultiButton;
class Button;
class CodalComponent;

typedef int8_t PinName;

typedef void (*reset_fn_t)();
void registerResetFunction(reset_fn_t fn);
void soft_panic(int errorCode);

#ifndef PXT_VM
	extern TaskHandle_t userCodeTask;
#endif

void install_gpio0_handler();

} // namespace pxt

#define IMAGE_BITS		4

// RIC esto lo elimine pero asegurarse , porque creo que esta en los malloc, aunque de ISR solo tengo las de GPIO creo ... !!!
#ifndef PXT_VM
	#define PXT_IN_ISR()	(xTaskGetCurrentTaskHandle() != userCodeTask)
#else
	#define PXT_IN_ISR()	(1 == 0)				// en EMSCRIPTEN y ESP32 devuelve false
#endif

// RIC cada ve que GC pide mas memoria lo hace en bloques de este tamaño ... por defecto eran 16Kb
#define GC_BLOCK_SIZE					(1024 * 12 * 1)

#define PXT_REGISTER_RESET(fn) pxt::registerResetFunction(fn)

/*	RIC para microcontroladores, saber si esta en la FLAH la imagen y no se puede escribir,
	se sabe por las direcciones del procesador
		
	cuando simulamos para EMSCRIPTEN esto cambia ...
*/
#ifdef CONFIG_IDF_TARGET_ESP32S2

	// 0x3f000000-... range of data SPI flash (we only support first 4M)
	#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)v >> 22) == 0xfc)
	
#elif defined(__EMSCRIPTEN__)
	// RIC entra por aqui en EMSCRIPTEN
	
	#define PXT_IS_READONLY(v) (isTagged(v) || EmscriptenIsReadOnlyImg((const void*)(v)))
	
#else
	
	// 0x3f400000-0x3f700000 range of data SPI flash
	#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)v >> 22) == 0xfd)
	
#endif

#define CODAL_PIN ::pxt::ZPin

extern "C" {
	extern const uintptr_t PXT_EXPORTData[];
}

#ifdef CONFIG_IDF_TARGET_ESP32S2

	#define WORKER_CPU		PRO_CPU_NUM
	#define DMA_CPU			PRO_CPU_NUM
	
#else
	/*	para ESP32
			
			El ESP32 dual-core tiene:
				PRO_CPU_NUM = 0
				APP_CPU_NUM = 1
	*/

	#define WORKER_CPU		APP_CPU_NUM
	#define DMA_CPU			PRO_CPU_NUM
	
#endif

#endif
