#ifndef __PXT_H
#define __PXT_H

#include "pxtbase.h"
#include "vm.h"
#include "pins.h"
#include "pintarget.h"

#define OUTPUT_BITS 12

#define DEVICE_EVT_ANY 0
#define DEVICE_ID_NOTIFY_ONE 1022
#define DEVICE_ID_NOTIFY 1023

#define CHK(call)                                                                                  \
    {                                                                                              \
        int __r = call;                                                                            \
        if (__r != 0) {                                                                            \
            DMESG("fail: %d at %d", __r, __LINE__);                                                \
            abort();                                                                               \
        }                                                                                          \
    }

namespace pxt
{
	
	
	void raiseEvent(int id, int event);
	int allocateNotifyEvent();
	void sleep_core_us(uint64_t us);

	class Button;
	typedef Button *Button_;

	extern volatile bool paniced;
	extern char **initialArgv;
	void target_exit();
	extern volatile int panicCode;

	// Buffer, Sound, and Image share representation.
	typedef Buffer Sound;

	typedef struct worker *worker_t;
	worker_t worker_alloc(const char *id, uint32_t stack_size);
	int worker_run(worker_t w, TaskFunction_t fn, void *arg);
	int worker_run_wait(worker_t w, TaskFunction_t fn, void *arg);
	void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);
	extern worker_t fg_worker;
	// RIC
	#ifdef __EMSCRIPTEN__
		void worker_main(void *arg);
	#endif

	void memInfo();

	extern void (*logJDFrame)(const uint8_t *data);
	extern void (*sendJDFrame)(const uint8_t *data);

	// solo la definimos, esta en otro fichero
	void *xmallocNamed(const char *label, uint32_t count, uint32_t elemSize);


	/*	Estructura global del CRC Game Engine
	*/
	#define MAXSPRITESXSCENE		40
	#define MAXINVALIDATERECTS		(MAXSPRITESXSCENE * 2)
	#define SCREEN_WIDTH			160
	#define SCREEN_HEIGHT			120
	typedef struct
		{
		int xi;
		int yi;
		int xf;
		int yf;
		}
		RECTANGULO;
	typedef struct									// es una version minimizada para PXT64, arreglar para multicompilacion
		{
		
		/*	Informacion de los invalidates Rectangles
		
				Array único:

				  Productor escribe/fusiona aquí ->                    <- Consumidor lee aquí
				+---------------------------------+------------------------------------------+
				| 0 | 1 | 2 | ...                 | ... | MAX-3 | MAX-2 | MAX-1              |
				+---------------------------------+------------------------------------------+
				  ^                                 ^
				  ir_front (0 a N)                  ir_back_count
		
		*/
		RECTANGULO invrects[MAXINVALIDATERECTS];	/*	typedef struct
														{
														int xi;
														int yi;
														int xf;
														int yf;
														} RECTANGULO;
													*/
		int ir_front = 0;                 			// Contador de escritura (Productor)
		int ir_back_start = MAXINVALIDATERECTS; 	// Inicio de los datos para el consumidor
		int ir_back_count = 0;            			// Cantidad de rectángulos para el consumidor
		bool ir_unsent = false;						// true = lo publicado por CommitInvalidateRectangles() aun NO se ha enviado a pantalla
		
		}
		ESTRUCTURA_PRECALCULATED_SCENE;


} // namespace pxt

extern "C" void target_disable_irq();
extern "C" void target_enable_irq();
extern "C" void target_init();
extern "C" void target_wait_us(uint32_t us);

#define DEVICE_ID_JACDAC 30
#define DEVICE_ID_JACDAC_PHYS 31

#undef PXT_MAIN
	// RIC por aqui pasa
#define PXT_MAIN                                                                                   \
    extern "C" int app_main() {                                                              	   \
		DMESG("FreeRTOS task app_main() prio=%d cpu=%d",uxTaskPriorityGet(NULL),xPortGetCoreID()); \
        pxt::vmStart();                                                                            \
		DMESG("RIC debug : END app_main()");													   \
        return 0;                                                                                  \
    }

#undef PXT_SHIMS_BEGIN
#define PXT_SHIMS_BEGIN                                                                            \
    namespace pxt {                                                                                \
    const OpcodeDesc staticOpcodes[] __attribute__((aligned(0x20))) = {

#undef PXT_SHIMS_END
#define PXT_SHIMS_END                                                                              \
    { 0, 0, 0 }                                                                                    \
    }                                                                                              \
    ;                                                                                              \
    }
	
// antes -> #define ALLOC_ARRAY(tp, sz) (tp *)xmalloc(sz == 0 ? 1 : sizeof(tp) * sz)
#define ALLOC_ARRAY(tp, sz, label) (tp *)xmallocNamed(label, (uint32_t)(sz), (uint32_t)sizeof(tp))

#endif
