
#include "pxt.h"

#ifdef __EMSCRIPTEN__

	#include <iostream>
	#include <queue>
	#include <functional>
	#include <cstdint>
	
	#include <emscripten.h>	
	
	#include "heapsnapshot.h"
	
	typedef void (*fn_t)(void *arg);
	
	struct Task
		{
		fn_t fn;
		void *arg;
		};
	
	
namespace pxt
	{
		
	// solo definimos que esta en otro fichero
	#ifdef __EMSCRIPTEN__
		void mainRunLoop();
	#endif
	
	// Estructura del Worker simulado
	struct worker
		{
		std::queue<Task> task_queue;				// Cola de trabajo diferido
		fn_t fn;
		void *arg;
		
		// extra
		const char *name;
		size_t stack_size;
		};
	
	void worker_main(void *arg)
	{
		TRACE_LOOP("");
		
		/*	RIC: si un snapshot de heap ha pedido detener la ejecucion, dejamos
			de reprogramar este bucle. La memoria WASM (y por tanto el fichero
			ya volcado a disco) queda congelada tal cual en ese instante.
		*/
		if (pxt::isHeapSnapshotHalted())
			{
			DMESG("worker_main() -> halted por heapsnapshot, no se reprograma mas");
			return;
			}
		
		emscripten_async_call(worker_main,arg,MSEG_JS_CALL_WORKER);
		
		if (!fg_worker)
			{
			DMESG("worker_main() -> No hay 'fg_worker'");
			return;
			}
		else
			{
			//DMESG("worker_main()");
			}
		
		if (!fg_worker->task_queue.empty())				// 1. Si hay tareas pendientes en la cola, las procesamos
			{
				
			// esto no se bien que es !!!!
			
			DMESG("que hay que hacer por aqui !!!");
        
			/*
			Task t = fg_worker->task_queue.front();
			
			fg_worker->task_queue.pop();
			
			if (t.fn) t.fn(t.arg);
			*/
			
			} 
		else if (fg_worker->fn)							// 2. Si no hay tareas pendientes, ejecutamos la función de reposo (Idle)
			{
			//DMESG("worker_main() -> IDLE");
			
			fg_worker->fn(fg_worker->arg);
			
			// volvemos al Runtime de Arcade Makecode
			mainRunLoop();
			}
		
	}
	
	worker_t worker_alloc(const char *id, uint32_t stack_size)
	{
		DMESG("worker_alloc -> %s",id);
		
		worker_t w = new struct worker();
		w->name = id;
		w->stack_size = stack_size;
		w->fn = NULL;
		w->arg = NULL;
		
		// falta -> xTaskCreatePinnedToCore(worker_main, id, stack_size, w, 2, &w->task, WORKER_CPU);
			// pero ya se hace simulando el worker_main con -> emscripten_async_call
		
		return w;
	}
		
	void worker_set_idle(worker_t w, fn_t fn, void *arg)
	{
		w->fn = fn;
		w->arg = arg;
	}
	
	// Función para encolar tareas desde la VM de MakeCode	
	/*
	void worker_post_task(worker_t *w, fn_t fn, void *arg)
	{
		w->task_queue.push( {fn, arg} );
	}
	*/
	
	}
	
#else
	
	#include "freertos/queue.h"
	#include "rom/ets_sys.h"
	
namespace pxt
	{

	struct worker {
		TaskHandle_t task;
		QueueHandle_t queue;					// antes -> xQueueHandle queue;
		TaskFunction_t fn;
		void *arg;
	};

	typedef struct qitem {
		TaskFunction_t fn;
		void *arg;
	} qitem_t;

	static void worker_main(void *arg)
	{
		DMESG("FreeRTOS task worker_main() prio=%d cpu=%d",uxTaskPriorityGet(NULL),xPortGetCoreID());
		
		worker_t w = (worker_t)arg;
		
		while (1)
			{
			//DMESG("worker_main tic ... stack free %d bytes",uxTaskGetStackHighWaterMark(NULL));
			
			qitem_t evt;
			
			// hay algun evento en la cola ?
			if (xQueueReceive(w->queue, &evt, w->fn ? 20 : 1)) evt.fn(evt.arg);
			
			// hay una funcion periodica para ejecutar ?
			if (w->fn) w->fn(w->arg);
			
			// RIC temporal
			//vTaskDelay(pdMS_TO_TICKS(1000));
			}
	}

	worker_t worker_alloc(const char *id, uint32_t stack_size)
	{
		worker_t w = (worker_t)calloc(1, sizeof(struct worker));
		
		w->queue = xQueueCreate(20, sizeof(qitem_t));
		
		// The main task is at priority 1, so we're higher priority (run "more often").
		// Timer task runs at much higher priority (~20).
			// RIC se pone prioridad 2 que es superior al main que es prioridad 1
		xTaskCreatePinnedToCore(worker_main, id, stack_size, w, 2, &w->task, WORKER_CPU);
		
		return w;
	}

	void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg) {
		w->fn = fn;
		w->arg = arg;
	}

	int worker_run(worker_t w, TaskFunction_t fn, void *arg) {
		qitem_t evt = {fn, arg};
		if (xQueueSendFromISR(w->queue, &evt, NULL) == pdPASS)
			return 0;
		return -1;
	}

	int worker_run_wait(worker_t w, TaskFunction_t fn, void *arg) {
		qitem_t evt = {fn, arg};
		if (xQueueSend(w->queue, &evt, 100) == pdPASS)
			return 0;
		return -1;
	}


	}

#endif

