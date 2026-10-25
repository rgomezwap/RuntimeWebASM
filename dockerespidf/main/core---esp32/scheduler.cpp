#include "pxt.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>

#ifdef __EMSCRIPTEN__
	// RIC para usar sleep
	#include <thread>
	#include <chrono>
#endif

#if !defined(PXT_ESP32) && !defined(__EMSCRIPTEN__)

	// __MINGW32__ is defined on both mingw32 and mingw64
	#ifdef __MINGW32__
		#include <windows.h>
	#else
		#include <sys/mman.h>
	#endif
	
#endif

#define HANDLER_RUNNING 0x0001

// should this be something like CXX11 or whatever?
#define THROW throw()
#define THREAD_DBG(...)

static uint8_t in_xmalloc_panic;

// RIC
#ifdef __EMSCRIPTEN__
	extern int ramfreeactual;
#endif
void *xmalloc(size_t sz)
{
	auto r = malloc(sz);
		
	if (r == NULL)
		{
		DMESG("failed to allocate %d bytes", sz);
		
		if (in_xmalloc_panic)
			{
			target_panic(PANIC_GC_OOM);
			}
		else
			{
			in_xmalloc_panic = 1;
			soft_panic(PANIC_GC_OOM); // this can happen on esp32 etc; shouldn't on linux/ios etc
			}
			
		}
	else
		{
		// RIC DEBUG
		//DMESG("allocate %d bytes", sz);
		#ifdef __EMSCRIPTEN__
			ramfreeactual-=sz;
		#endif
		}
	
	return r;
}

void *operator new(size_t size) {
    return xmalloc(size);
}
void *operator new[](size_t size) {
    return xmalloc(size);
}

void operator delete(void *p)THROW {
    xfree(p);
}
void operator delete[](void *p) THROW {
    xfree(p);
}

uint8_t *gcBase;

namespace pxt {
	
#if !defined(PXT_ESP32) && !defined(__EMSCRIPTEN__)
	static uint64_t startTime;
#endif

FiberContext *allFibers;
FiberContext *currentFiber;
	
// PENDIENTE - en EMSCRIPTEN esto no se usa ... mirar ESP32 que pasa !!!
static pthread_mutex_t eventMutex;
static pthread_cond_t newEventBroadcast;

static struct Event *eventHead, *eventTail;

struct Event {
    struct Event *next;
    int source;
    int value;
};

Event lastEvent;

Event *mkEvent(int source, int value)
{
	auto res = new Event();
	
    memset(res, 0, sizeof(Event));
	
    res->source = source;
	
    res->value = value;
	
    return res;
}

volatile int panicCode;
extern "C" void drawPanic(int code);

// RIC
void schedule()
{
	auto f = currentFiber;
	
    if (!f->wakeTime && !f->waitSource) oops(55);
	
    f->resumePC = f->pc;
	
    f->pc = NULL;				// this will break the exec_loop()
}

void dmesg_flush();

// RIC
static void panic_core(int error_code)
{
	int prevErr = errno;

    panicCode = error_code;

    drawPanic(error_code);

    DMESG("    PANIC code #%d , previues error #%d '%s'", error_code % 1000, prevErr, strerror(prevErr));
	
    dmesg_flush();
}

extern "C" void target_panic(int error_code)
{
	
	panic_core(error_code);
	
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
		// sleep_core_us(5 * 1000 * 1000);
		// abort();
		while (1) sleep_core_us(10000);
	#elif defined(PXT_VM)
		systemReset();
	#else
		while (1) sleep_core_us(10000);
	#endif
	
}

DLLEXPORT int pxt_get_panic_code() {
    return panicCode;
}

void ets_log_dmesg();

void soft_panic(int errorCode)
{
	DMESG("soft_panic(%d)",errorCode);
	
	if (errorCode >= 999) errorCode = 999;
	
	if (errorCode <= 0) errorCode = 1;
    
	vm_stack_trace();
	
    panic_core(1000 + errorCode);
	
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
		ets_log_dmesg();
		sleep_core_us(4000000);
		abort();
	#else
		systemReset();
	#endif
	
}

void sleep_core_us(uint64_t us)
{
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
		uint64_t endp = esp_timer_get_time() + us;
		while (esp_timer_get_time() < endp)
			{}
	#else
		struct timespec ts;
		ts.tv_sec = us / 1000000;
		ts.tv_nsec = (us % 1000000) * 1000;
		while (nanosleep(&ts, &ts))
			{}
	#endif
}

void target_yield()
{
	#if defined(PXT_ESP32)
		vTaskDelay(1);
	#elif defined(__EMSCRIPTEN__)
		// NO hacemos nada
	#else
		sleep_core_us(1000);
	#endif
}

// RIC
void sleep_ms(uint32_t ms)
{
	TRACE_LOOP("pxt::sleep_ms( %d )",ms);
	
    currentFiber->wakeTime = current_time_ms() + ms;
    schedule();
}

void sleep_us(uint64_t us) {
    if (us > 20000) {
        sleep_ms((uint32_t)(us / 1000));
    } else {
        sleep_core_us(us);
    }
}

#if !defined(PXT_ESP32) && !defined(__EMSCRIPTEN__)

	static uint64_t currTime()
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return tv.tv_sec * 1000000LL + tv.tv_usec;
	}
	
	uint64_t current_time_us()
	{
		if (!startTime)
			startTime = currTime();
		return currTime() - startTime;
	}

#endif

int current_time_ms() {
    return (int)(current_time_us() / 1000);
}

void disposeFiber(FiberContext *t) {
    if (allFibers == t) {
        allFibers = t->next;
    } else {
        for (auto tt = allFibers; tt; tt = tt->next) {
            if (tt->next == t) {
                tt->next = t->next;
                break;
            }
        }
    }

    // DMESG("free: %p %p", t, t->stackCopy);

    xfree(t->stackCopy);
    xfree(t);

    if (currentFiber == t)
        currentFiber = NULL;
}

#define INITIAL_STACK_COPY_SIZE 16

// RIC
	// solamente definimos que esta en otro fichero
	int inGCAreaAndReturnBlockNumber(void *ptr);
FiberContext *setupThread(Action a, TValue arg = 0, HandlerBinding *hb = NULL)
{
	
    auto t = (FiberContext *)xmalloc(sizeof(FiberContext));
    memset(t, 0, sizeof(*t));
	
    if (!vmImg->stackBase)
		{
        vmImg->stackBase = (TValue *)xmalloc(VM_STACK_SIZE * sizeof(TValue));
        vmImg->stackTop = vmImg->stackBase + VM_STACK_SIZE;
        vmImg->stackLimit = vmImg->stackBase + VM_MAX_FUNCTION_STACK + 5;
		}
	
    t->stackCopy = (TValue *)xmalloc(sizeof(TValue) * INITIAL_STACK_COPY_SIZE);
    t->stackCopySize = INITIAL_STACK_COPY_SIZE;
    t->sp = vmImg->stackTop - 8;

    // DMESG("thr: %p %p", t, t->stackCopy);
	
	auto ptr = t->stackCopy;
    *ptr++ = TAG_STACK_BOTTOM;
    *ptr++ = 0;
    *ptr++ = arg;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = (TValue)0xf00df00df00df00d;

    t->handlerBinding = hb;
    auto ra = (RefAction *)a;
	
    // we only pass 1 argument, but can in fact handle up to 4
    if (ra->numArgs > 2) target_panic(PANIC_INVALID_IMAGE);
    
	t->currAction = ra;
		// RIC DEBUG
		t->initialAction = ra;
    t->resumePC = actionPC(ra);

    t->img = vmImg;
    t->imgbase = (uint16_t *)vmImg->dataStart;
	
	// RIC DEBUG
	FIBERS_DBG("            FiberContext %x",t->initialAction);
	{
		if ( (void *)t->currAction > (void *)vmImg->dataEnd )
			{
			auto obj = (RefObject *)t->currAction;
			
			// Validamos si su VTable es de tipo RefAction
			if (obj->vtable == &pxt::RefAction_vtable)
				{	
				TRACE_LOOP_MIN("    Function in GC bloque = %d of Fiber %x",inGCAreaAndReturnBlockNumber((void *)t->currAction),t->initialAction);
				}
			else
				{
				TRACE_LOOP("    ERROR esta fiber NO es Function dentro del GC");
				}
			}
		else
			{
		
			for (uint32_t i = 0; i < vmImg->numSections; ++i)
				{
				if (vmImg->pointerLiterals[i] == (TValue)t->currAction)
					{
					if ( vmImg->sections[i]->type == SectionType::Function)
						{
						
						int b=-1;
						
						#ifdef __EMSCRIPTEN__
						
							// definimos que esta en otro fichero
							extern std::vector<int> dbg_ignoredIDs;
							
							// miramos que NO este en la lista de IDs que ignoramos para DEBUG, para simplificar el LOG
							for (b=0 ; b < dbg_ignoredIDs.size(); ++b)
								{
								if (dbg_ignoredIDs[b] == i)
									{
									break;			// Encontrado
									}
								}
							
							// si no pertenece a la lista de ignorados
							if (b == dbg_ignoredIDs.size() ) b=-1;
							
						#endif
						
						if (b == -1) TRACE_LOOP_MIN("    Fiber %x run function vmImg ID = %d",t->initialAction,i);
						
						}
					else
						{
						TRACE_LOOP("    ERROR esta fiber NO es Function dentro de vmImg");
						}
					
					break;
					}
				}
				
			}
	}
	
    // add at the end
	if (allFibers)
		{
		
		for (auto p = allFibers; p; p = p->next)
			{
            
			if (!p->next) 
				{
                p->next = t;
                break;
				}
			}
		}
	else
		{
        allFibers = t;
		}
		
	// RIC DEBUG
	#if 1
		int numThreads = 0;
		for (auto p = allFibers; p; p = p->next) numThreads++;
		FIBERS_DBG("            new setup thread with %d threads",numThreads);
		if (numThreads > 10) abort();
	#endif
	
    return t;
}

void runInParallel(Action a)
{
	// RIC DEBUG
	FIBERS_DBG("        setupThread(...) -> runInParallel(Action a)");
	
    setupThread(a);
}

void runForever(Action a)
{
	// RIC DEBUG
	FIBERS_DBG("        setupThread(...) -> runForever(Action a)");

    auto f = setupThread(a);
    f->foreverPC = f->resumePC;
}

void waitForEvent(int source, int value) {
    currentFiber->waitSource = source;
    currentFiber->waitValue = value;
    schedule();
}

FiberContext *suspendFiber() {
    currentFiber->waitSource = PXT_WAIT_SOURCE_PROMISE;
    schedule();
    return currentFiber;
}

void resumeFiberWithFn(FiberContext *ctx, fiber_resume_t fn, void *arg) {
    if (ctx->waitSource != PXT_WAIT_SOURCE_PROMISE)
        oops(52);
    ctx->waitSource = 0;
    ctx->wakeFn = fn;
    ctx->wakeFnArg = arg;
}

void resumeFiber(FiberContext *ctx, TValue v) {
    if (ctx->waitSource != PXT_WAIT_SOURCE_PROMISE)
        oops(52);
    ctx->waitSource = 0;
    ctx->r0 = v;
}

static void startHandler(HandlerBinding *hb, Event &e)
{
	
	if (!hb) return;
	
	TECLAS_DBG("startHandler(...)");
	
    lastEvent = e;									// this is quite racy
	
    if (hb->flags & HANDLER_RUNNING)
		{
        
		auto tmp = mkEvent(e.source, e.value);
		
        if (hb->pending == NULL)
			{
            hb->pending = tmp;
			}
		else
			{
            int numev = 0;
            auto p = hb->pending;
			
            for (; p->next; p = p->next) numev++;
			
			if (numev >= 10)
				{
                xfree(tmp);
                return;
				}
			
			p->next = tmp;
			}
		
		}
	else
		{
        hb->flags |= HANDLER_RUNNING;
		
		// RIC DEBUG
		FIBERS_DBG("        setupThread(...) -> startHandler(...)");
        
		setupThread(hb->action, fromInt(e.value), hb);
		}
	
}

// RIC
static void dispatchEvent(Event &e)
{
	//DMESG("pxt::dispatchEvent(...)");
	
    startHandler(findBinding(e.source, e.value), e);
	
    startHandler(findBinding(e.source, DEVICE_EVT_ANY), e);
}

static void wakeFibers()
{
	
    for (;;)
		{
        
		// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
		pthread_mutex_lock(&eventMutex);
		
        if (eventHead == NULL)
			{
			// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
            pthread_mutex_unlock(&eventMutex);
            return;
			}
		
        Event *ev = eventHead;
		
        eventHead = ev->next;
		
        if (eventHead == NULL) eventTail = NULL;
		
		// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
        pthread_mutex_unlock(&eventMutex);

        for (auto thr = allFibers; thr; thr = thr->next)
			{
            
			if (thr->waitSource == 0) continue;
			
			if (thr->waitValue != ev->value && thr->waitValue != DEVICE_EVT_ANY) continue;
			
			if (thr->waitSource == ev->source)
				{
                thr->waitSource = 0;
				}
			else if (thr->waitSource == DEVICE_ID_NOTIFY && ev->source == DEVICE_ID_NOTIFY_ONE)
				{
                thr->waitSource = 0;
                break; // do not wake up any other threads
				}
			
			}
			
		dispatchEvent(*ev);
		
        delete ev;
		
		}
}

static void saveStack() {
    auto f = currentFiber;
    if (!f)
        return;
    int sizeNeeded = vmImg->stackTop - f->sp;
    // DMESG("save %d %p", sizeNeeded, f);
    if (!f->stackCopy || sizeNeeded > f->stackCopySize) {
        xfree(f->stackCopy);
        f->stackCopySize = sizeNeeded + 10;
        f->stackCopy = (TValue *)xmalloc(f->stackCopySize * sizeof(TValue));
        // DMESG(" -> %p", f->stackCopy);
    }
    memcpy(f->stackCopy, f->sp, sizeNeeded * sizeof(TValue));
}

static void restoreStack() {
    auto f = currentFiber;
    memcpy(f->sp, f->stackCopy, (uint8_t *)vmImg->stackTop - (uint8_t *)f->sp);
}

// RIC
#ifdef __EMSCRIPTEN__
		// la definimos porque esta en otro fichero
		void st7735_poll_fake_dma();
	void mainRunLoop()
#else
	static void mainRunLoop()
#endif
{
    
	FiberContext *f = NULL;
	
    for (;;)
		{
		
		// RIC DEBUG
		//DMESG("");
		//DMESG("mainRunLoop() -> una vuelta");
		// RIC aqui hago una pausa de velocidad en mseg.		
		#ifdef MAINRUNLOOP_PAUSA
			MAINRUNLOOP_PAUSA
		#endif
		
		// sistema FAKE para simular el fin de DMA
		#ifdef __EMSCRIPTEN__
			
			/*	IDEA hacer como pxt::sleep_ms() no bloquea nada, solo marca wakeTime y llama a schedule(),
			que rompe exec_loop() (f->pc = NULL) dejando resumePC apuntando a la MISMA
			instrucción de llamada. La próxima vez que mainRunLoop() reactive esa fiber,
			se re-ejecuta la misma llamada nativa desde cero.
			*/
			
			st7735_poll_fake_dma();
			
		#endif
		
		if (panicCode) return;
		
		//DMESG("pxt::wakeFibers(...)");
		wakeFibers();
		//DMESG("     exit pxt::wakeFibers(...)");
		
		auto now = current_time_ms();
		auto fromBeg = false;
		
		if (!f)
			{
			f = allFibers;
			fromBeg = true;
			}
			
		while (f)
			{
			
			if (f->wakeTime && now >= (int)f->wakeTime) f->wakeTime = 0;
			
			if (!f->wakeTime && !f->waitSource)
				{
				break;
				}
			
			f = f->next;
			
			}
		
		if (f)
			{
			
			if (currentFiber != f)
				{
				saveStack();
				currentFiber = f;
				restoreStack();
				}
			
			f->pc = f->resumePC;
			f->resumePC = NULL;
			
			if (f->wakeFn)
				{
					
				auto fn = f->wakeFn;
				f->wakeFn = NULL;
				f->r0 = fn(f->wakeFnArg);
				
				if (f->wakeTime || f->waitSource) continue; // we got suspended again
				}
				
			exec_loop(f);
			
			if (panicCode) return;
			
			auto n = f->next;
			
			if (f->resumePC == NULL)
				{
				
				if (f->foreverPC)
					{
						
					f->resumePC = f->foreverPC;
					f->wakeTime = current_time_ms() + 20;
					
					// restore stack, as setupThread() does it
					for (int i = 0; i < 5; ++i)
						{
						if (*--f->sp == TAG_STACK_BOTTOM) break;
						}
					
					if (*f->sp != TAG_STACK_BOTTOM) target_panic(PANIC_INVALID_IMAGE);
					
					}
				else
					{
					
					auto hb = f->handlerBinding;
					
					if (hb)
						{
						
						auto pev = hb->pending;
						if (pev)
							{
							hb->pending = pev->next;
								// RIC DEBUG
								FIBERS_DBG("        setupThread(...) -> handlerBinding");
							setupThread(hb->action, fromInt(pev->value), hb);
							xfree(pev);
							}
						else
							{
							f->handlerBinding->flags &= ~HANDLER_RUNNING;
							}
						
						}
					
					disposeFiber(f);
					
					}
				
				}
			
			f = n;
			
			}
		else if (fromBeg)
			{
			
			// RIC aqui es cuando el bucle infinito cede tiempo, si no hay ninguna FIBER que le toque aun por mseg. hacer nada ...
			#ifdef __EMSCRIPTEN__
				// rompemos el bucle INFINITO
				//DMESG("mainRunLoop() -> break");
				break;
			#else
				//DMESG("mainRunLoop() -> target_yield()");
				target_yield();
			#endif
			
			}
		
		}
}

int allocateNotifyEvent() {
    static volatile int notifyId;
    return ++notifyId;
}

// RIC
void raiseEvent(int id, int event)
{
	//DMESG("pxt::raiseEvent(...)");
	
	auto e = mkEvent(id, event);
	
	// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
    pthread_mutex_lock(&eventMutex);
	
    if (eventTail == NULL)
		{
        
		if (eventHead != NULL) oops(51);
		
		eventHead = eventTail = e;
		
		}
	else
		{
        
		eventTail->next = e;
        
		eventTail = e;
		
		}
	
	// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
	pthread_cond_broadcast(&newEventBroadcast);
    
	// PENDIENTE - en EMSCRIPTEN esto esta anulado, no se bloquea... mirar ESP32 que pasa !!!
	pthread_mutex_unlock(&eventMutex);
	
}

DLLEXPORT void pxt_raise_event(int id, int event) {
    raiseEvent(id, event);
}

void registerWithDal(int id, int event, Action a, int flags) {
    // TODO support flags
    setBinding(id, event, a);
}

uint32_t afterProgramPage() {
    return 0;
}

char **initialArgv;

void screen_init();
void initKeys();
void target_startup();

// RIC
void initRuntime()
{
	DMESG("START initRuntime()");
	
		// RIC DEBUG
		int tmp;
	tmp=current_time_ms();
		DMESG("current_time_ms() -> %d",tmp);
	
	// crea una tarea de foregound (ejecuta una funcion periodica y/o eventos de una cola)
    target_startup();
	
		// RIC DEBUG
		DMESG("        setupThread(...) -> vmImg->entryPoint");
    setupThread((TValue)vmImg->entryPoint);
	
    target_init();
	
    screen_init();
	
	// de momento esta pensado para EMSCRIPTEN, PENDIENTE
    initKeys();

    DMESG("start main loop");

	// aqui ya es un buble infinito que va cediendo el tiempo a otro procesos cuando esta libre ...
	//for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); DMESG("vTaskDelay 1000 mseg."); }
    mainRunLoop();
	
	
	// aqui ya NO deberia llegarse, si se llega hace un RESET y se acabo todo
	
	
#ifndef __EMSCRIPTEN__
	systemReset();
#endif
	
	DMESG("    END initRuntime()");
}

// RIC
void *gcAllocBlock(size_t sz)
{
	
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
		// RIC por aqui entra
		
		void *r = xmalloc(sz);
		
	#else
		RIC por aqui NO entra
	
		static uint8_t *currPtr = (uint8_t *)GC_BASE;
		sz = (sz + GC_PAGE_SIZE - 1) & ~(GC_PAGE_SIZE - 1);
		
		#if defined(PXT64) || defined(__MINGW32__)
			if (!gcBase) {
				gcBase = (uint8_t *)xmalloc(1 << PXT_VM_HEAP_ALLOC_BITS);
				currPtr = gcBase;
			}
			void *r = currPtr;
			if ((uint8_t *)currPtr - gcBase > (1 << PXT_VM_HEAP_ALLOC_BITS) - (int)sz)
				soft_panic(20);
		#else
			void *r = mmap(currPtr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
			if (r == MAP_FAILED) {
				DMESG("mmap %p failed; err=%d", currPtr, errno);
				target_panic(PANIC_INTERNAL_ERROR);
			}
		#endif
		
		currPtr = (uint8_t *)r + sz;
	#endif
	
	if (isReadOnly((TValue)r))
		{
		DMESG("mmap returned read-only address: %p", r);
		target_panic(PANIC_INTERNAL_ERROR);
		}
	
	return r;
}

// RIC
void gcProcessStacks(int flags)
{
	int cnt = 0;
	
    for (auto f = allFibers; f; f = f->next)
		{
        
		TValue *end, *ptr;
		
        if (f == currentFiber)
			{
            end = vmImg->stackTop;
            ptr = f->sp;
			}
		else
			{
            end = f->stackCopy + (vmImg->stackTop - f->sp);
            ptr = f->stackCopy;
			}
		
		//DMESG("        scan fiber=%p action=%p r0=%p sp=%p end=%p", f, f->currAction, f->r0, ptr, end);
        gcProcess((TValue)f->currAction);
        gcProcess((TValue)f->r0);
		
        if (flags & 2)
			{
            DMESG("RS%d:%p/%d", cnt++, ptr, end - ptr);
			DMESG("mark: %p - %p", ptr, end);
			}
		
		while (ptr < end)
			{
			//DMESG("    stack slot %p = %p", ptr, *ptr);
            gcProcess(*ptr++);
			}
		
		}
	
}

#define MAX_RESET_FN 32
static reset_fn_t resetFunctions[MAX_RESET_FN];

void registerResetFunction(reset_fn_t fn) {
    for (int i = 0; i < MAX_RESET_FN; ++i) {
        if (!resetFunctions[i]) {
            resetFunctions[i] = fn;
            return;
        }
    }

    target_panic(PANIC_INTERNAL_ERROR);
}

void systemReset()
{
	
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
	
		dmesg("systemReset()");
		
		esp_restart();
		
	#else
		RIC por aqui NO pasa
	
		if (!panicCode)
			panicCode = -1;

		dmesg("TARGET RESET");

		gcFreeze();

		for (int i = 0; i < MAX_RESET_FN; ++i) {
			auto fn = resetFunctions[i];
			if (fn)
				fn();
		}

		coreReset(); // clears handler bindings

		currentFiber = NULL;
		while (allFibers) {
			disposeFiber(allFibers);
		}

		// this will consume all events, but won't dispatch anything, since all listener maps are empty
		wakeFibers();

		// mark all GC memory as free
		gcReset();

		pthread_exit(NULL);
	
	#endif
	
}

} // namespace pxt
