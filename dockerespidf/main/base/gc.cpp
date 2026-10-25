#include "pxtbase.h"

#ifdef __EMSCRIPTEN__
	#include "heapsnapshot.h"
#endif

// RIC DEBUG
#ifndef TRACE
	#define TRACE NOLOG
#endif

#ifndef GC_BLOCK_SIZE
	#define GC_BLOCK_SIZE (1024 * 16)
#endif

#ifndef GC_MAX_ALLOC_SIZE
#define GC_MAX_ALLOC_SIZE (GC_BLOCK_SIZE - 16)
#endif

#ifndef GC_ALLOC_BLOCK
	RIC por aqui NO entra
	#define GC_ALLOC_BLOCK xmalloc
#endif

#ifdef PXT64
	
	#define HIGH_SHIFT 48
	#define BYTES_TO_WORDS(x) ((x) >> 3)
	#define WORDS_TO_BYTES(x) ((x) << 3)
	#define ALIGN_TO_WORD(x) (((x) + 7) & (~7ULL))
	#define VAR_BLOCK_WORDS(vt) ((uint32_t)(uint64_t)(vt) >> 2)
	
#else
	
	#define HIGH_SHIFT 28
	#define BYTES_TO_WORDS(x) ((x) >> 2)
	#define WORDS_TO_BYTES(x) ((x) << 2)
	#define ALIGN_TO_WORD(x) (((x) + 3) & (~3U))
	#define VAR_BLOCK_WORDS(vt) (((uint32_t)(vt) << 4) >> (4 + 2))

#endif

#define FREE_MASK (1ULL << (HIGH_SHIFT + 3))
#define ARRAY_MASK (1ULL << (HIGH_SHIFT + 2))
#define PERMA_MASK (1ULL << (HIGH_SHIFT + 1))
#define MARKED_MASK 0x1
#define ANY_MARKED_MASK 0x3

// the bit operations should be faster than loading large constants
#define IS_FREE(vt) ((uintptr_t)(vt) >> (HIGH_SHIFT + 3))
#define IS_ARRAY(vt) (((uintptr_t)(vt) >> (HIGH_SHIFT + 2)) & 1)
#define IS_PERMA(vt) (((uintptr_t)(vt) >> (HIGH_SHIFT + 1)) & 1)
#define IS_VAR_BLOCK(vt) ((uintptr_t)(vt) >> (HIGH_SHIFT + 2))
#define IS_MARKED(vt) ((uintptr_t)(vt)&MARKED_MASK)
#define IS_LIVE(vt) (IS_MARKED(vt) || (((uintptr_t)(vt) >> (HIGH_SHIFT)) == 0x6))

//#define PXT_GC_DEBUG 1

//#define PXT_GC_CHECKS 1

//#define PXT_GC_STRESS 1

#define MARK(v)																\
	do																		\
		{                                                  					\
        GC_CHECK(inGCArea(v), 42);             								\
        *(uintptr_t *)(v) |= MARKED_MASK;                  					\
		} while (0)															\

#ifdef PXT_GC_DEBUG
	#define LOG DMESG
	#define VLOG DMESG
	#define VVLOG DMESG
#else
	#define LOG NOLOG
	#define VLOG NOLOG
	#define VVLOG NOLOG
#endif

#ifdef PXT_GC_CHECKS
	#define GC_CHECK(cond, code)						if ( !(cond) ) oops(code)	
#else
	#define GC_CHECK(cond, code)						( (void)0 )
#endif

namespace pxt {

// RIC DEBUG solo para definir esta en otro fichero
#ifdef __EMSCRIPTEN__
	extern int contador_destroyVTPunterosInvalidos;
#endif

// keep in sync with base/control.ts, function gcStats()
struct GCStats {
    uint32_t numGC;
    uint32_t numBlocks;
    uint32_t totalBytes;
    uint32_t lastFreeBytes;
    uint32_t lastMaxBlockBytes;
    uint32_t minFreeBytes;
};

static GCStats gcStats;

//% expose
Buffer getGCStats() {
    return mkBuffer((uint8_t *)&gcStats, sizeof(gcStats));
}

//%
void popThreadContext(ThreadContext *ctx);
//%
ThreadContext *pushThreadContext(void *sp, void *endSP);

unsigned RefRecord_gcsize(RefRecord *r) {
    VTable *tbl = getVTable(r);
    return BYTES_TO_WORDS(tbl->numbytes);
}

#ifdef PXT_GC_THREAD_LIST
ThreadContext *threadContexts;
#endif

#define IN_GC_ALLOC 1
#define IN_GC_COLLECT 2
#define IN_GC_FREEZE 4
#define IN_GC_PREALLOC 8

#ifndef PXT_VM
static TValue *tempRoot;
static uint8_t tempRootLen;
#endif

uint8_t inGC;

void popThreadContext(ThreadContext *ctx) {
#ifndef PXT_VM
    VLOG("pop: %p", ctx);

    if (!ctx)
        return;

    auto n = ctx->stack.next;
    if (n) {
        VLOG("seg %p", n);
        ctx->stack.top = n->top;
        ctx->stack.bottom = n->bottom;
        ctx->stack.next = n->next;
        app_free(n);
    } else {
#ifdef PXT_GC_THREAD_LIST
        if (ctx->next)
            ctx->next->prev = ctx->prev;
        if (ctx->prev)
            ctx->prev->next = ctx->next;
        else {
            if (threadContexts != ctx)
                oops(41);
            threadContexts = ctx->next;
            if (threadContexts)
                threadContexts->prev = NULL;
        }
#endif
        app_free(ctx);
        setThreadContext(NULL);
    }
#endif
}

#define ALLOC(tp) (tp *)app_alloc(sizeof(tp))

ThreadContext *pushThreadContext(void *sp, void *endSP) {
#ifdef PXT_VM
    return NULL;
#else
	RIC por aqui NO pasa

    if (PXT_IN_ISR())
        target_panic(PANIC_CALLED_FROM_ISR);

    auto curr = getThreadContext();
    tempRoot = (TValue *)endSP;
    tempRootLen = (uintptr_t *)sp - (uintptr_t *)endSP;
    if (curr) {
#ifdef PXT_GC_THREAD_LIST
#ifdef PXT_GC_DEBUG
        auto ok = false;
        for (auto p = threadContexts; p; p = p->next)
            if (p == curr) {
                ok = true;
                break;
            }
        if (!ok)
            oops(49);
#endif
#endif
        auto seg = ALLOC(StackSegment);
        VLOG("stack %p / %p", seg, curr);
        seg->top = curr->stack.top;
        seg->bottom = curr->stack.bottom;
        seg->next = curr->stack.next;
        curr->stack.next = seg;
    } else {
        curr = ALLOC(ThreadContext);
        LOG("push: %p", curr);
        curr->globals = globals;
        curr->stack.next = NULL;
        curr->thrownValue = TAG_NON_VALUE;
        curr->tryFrame = NULL;

#ifdef PXT_GC_THREAD_LIST
        curr->next = threadContexts;
        curr->prev = NULL;
        if (curr->next)
            curr->next->prev = curr;
        threadContexts = curr;
#endif
        setThreadContext(curr);
    }
    tempRootLen = 0;
    curr->stack.bottom = sp;
    curr->stack.top = NULL;
    return curr;
#endif
}

class RefBlock : public RefObject {
  public:
    RefBlock *nextFree;
};

struct GCBlock
	{
    GCBlock *next;
    uint32_t blockSize;
    RefObject data[0];
	};

struct PendingArray {
    PendingArray *next;
    TValue *data;
    unsigned len;
};

#define PENDING_ARRAY_THR 100

static PendingArray *pendingArrays;
static LLSegment gcRoots;
LLSegment workQueue; // (ab)used by consString making
static GCBlock *firstBlock;
static RefBlock *firstFree;
static uint8_t *midPtr;

static bool inGCArea(void *ptr)
{
	
	// RIC DEBUG
	int contadorblocks=0;
	
    for (auto block = firstBlock; block; block = block->next)
		{
		
		contadorblocks++;
		
        if ((void *)block->data <= ptr && ptr < (void *)((uint8_t *)block->data + block->blockSize)) return true;
		
		}

	// RIC DEBUG
	DMESG("FAIL in inGCArea(...) ptr = %p , numblock = %d",ptr,contadorblocks);
	
    return false;
}

int inGCAreaAndReturnBlockNumber(void *ptr)
{
	
	// RIC DEBUG
	int contadorblocks=0;
	
    for (auto block = firstBlock; block; block = block->next)
		{
		
		contadorblocks++;
		
        if ((void *)block->data <= ptr && ptr < (void *)((uint8_t *)block->data + block->blockSize)) break;
		
		}
	
    return contadorblocks;
}

#define NO_MAGIC(vt)			((VTable *)vt)->magic != VTABLE_MAGIC
#define VT(p)					(*(uintptr_t *)(p))

#if defined(__EMSCRIPTEN__)
	
	#define SKIP_PROCESSING(p)		(isReadOnly(p) || (VT(p) & (ANY_MARKED_MASK | ARRAY_MASK)) || NO_MAGIC(VT(p)))

#else
	
	static inline bool ESP32DEBUG_vtOk(uintptr_t vt)
	{
		if (vt & 3) return false;
		
		if ((vt >> 22) == 0xfd) return true;						// imagen / rodata en flash mapeada
		
		return vt >= 0x3FFB0000 && vt < 0x40000000;					// DRAM, por si hay VTables en RAM
	}
	
	#define SKIP_PROCESSING(p)		(isReadOnly(p) || (VT(p) & (ANY_MARKED_MASK | ARRAY_MASK)) || !ESP32DEBUG_vtOk(VT(p)) || NO_MAGIC(VT(p)))
	
	static inline bool ESP32DEBUG_validPtr(TValue v)
	{
		uintptr_t a = (uintptr_t)v;
		
		if (a & 3) return false;
		
		// rangos reales de tu build: heap GC, imagen VM (readonly), flash mapeada...
		return (a >= 0x3F400000 && a < 0x40000000);   				// ajusta a tus regiones
	}
	
#endif

void gcMarkArray(void *data) {
    auto segBl = (uintptr_t *)data - 1;
    GC_CHECK(!IS_MARKED(VT(segBl)), 47);
    MARK(segBl);
}

// RIC estoy intentando protegerme de un Guru Meditation en ESP32
void gcScan(TValue v)
{
	// parece que en esta linea me peta con alguna VT que se queda de basura ....
		// ESP32 DEBUG
		#ifndef __EMSCRIPTEN__
			if (isReadOnly(v)) return;
			if (!ESP32DEBUG_validPtr(v))
				{
				DMESG("ESP32 trap Guru Meditation -> gcScan BAD v=%p", v);
				return;
				}
		#endif
	if (SKIP_PROCESSING(v)) return;
	
	MARK(v);
	
    workQueue.push(v);
}

void gcScanMany(TValue *data, unsigned len) {
    // VLOG("scan: %p %d", data, len);
    for (unsigned i = 0; i < len; ++i) {
        auto v = data[i];
        // VLOG("psh: %p %d %d", v, isReadOnly(v), (*(uint32_t *)v & 1));
        if (SKIP_PROCESSING(v))
            continue;
        MARK(v);
        workQueue.push(v);
        if (workQueue.getLength() > PENDING_ARRAY_THR) {
            i++;
            // store rest of the work for later, when we have cleared the queue
            auto pa = (PendingArray *)xmalloc(sizeof(PendingArray));
            pa->next = pendingArrays;
            pa->data = data + i;
            pa->len = len - i;
            pendingArrays = pa;
            break;
        }
    }
}

void gcScanSegment(Segment &seg) {
    auto data = seg.getData();
    if (!data)
        return;
    VVLOG("seg %p %d", data, seg.getLength());
    gcMarkArray(data);
    gcScanMany(data, seg.getLength());
}

#define getScanMethod(vt) ((RefObjectMethod)(((VTable *)(vt))->methods[2]))
#define getSizeMethod(vt) ((RefObjectSizeMethod)(((VTable *)(vt))->methods[3]))




void printVTableInfo(RefObject *d) {
    uintptr_t rawVT = (uintptr_t)d->vtable;
    
    // 1. Extraer banderas bit a bit de la cabecera
    bool marked  = IS_MARKED(rawVT);
    bool isFree  = IS_FREE(rawVT);
    bool isArray = IS_ARRAY(rawVT);
    
    DMESG("=== VTABLE DIAGNOSTIC for Obj %p ===", (void*)d);
    DMESG("  Raw VT Pointer : %p", (void*)rawVT);
    DMESG("  Flags           : Marked=%d, Free=%d, Array=%d", marked, isFree, isArray);

    // 2. PROTECCIÓN: Si es Array o Bloque Libre, NO desreferenciar como VTable
    if (isFree) {
        DMESG("  [i] Es un bloque LIBRE.");
        return;
    }
    
    if (isArray) {
        DMESG("  [i] Es un ARRAY (no tiene estructura VTable ni métodos virtuales).");
        return;
    }

    // 3. Si NO es Array ni Libre, es un Objeto C++ válido con VTable real
    VTable *vt = (VTable *)(rawVT & ~ANY_MARKED_MASK);
    DMESG("  Clean VT Pointer: %p", (void*)vt);

    // Validar puntero antes de leer en memoria
    if (vt) {
        DMESG("  Magic Value     : 0x%X (Expected: 0x%X)", vt->magic, VTABLE_MAGIC);
        DMESG("  Object Size     : %d bytes (%d words)", vt->numbytes, BYTES_TO_WORDS(vt->numbytes));
        DMESG("  Destructor Ptr  : %p", (void*)vt->methods[0]);
    } else {
        DMESG("  [!] VTable NULL");
    }
}




// RIC
void gcProcess(TValue v)
{
	
	// aqui si lo que queremos procesar es de la zona de la vmImg (readonly) se salta ...
	if (SKIP_PROCESSING(v)) return;
    
	VVLOG("gcProcess: %p", v);
    
	MARK(v);
    
	auto scan = getScanMethod(VT(v) & ~ANY_MARKED_MASK);
    if (scan) scan((RefObject *)v);
	
    for (;;)
		{
        
		while (workQueue.getLength())
			{
            
			auto curr = (RefObject *)workQueue.pop();
            
			VVLOG(" - %p", curr);
            
			scan = getScanMethod(curr->vt() & ~ANY_MARKED_MASK);
            
			if (scan) scan(curr);
			
			}
			
		if (pendingArrays)
			{
            auto pa = pendingArrays;
            pendingArrays = pa->next;
            auto data = pa->data;
            auto len = pa->len;
            xfree(pa);
            gcScanMany(data, len);
			}
		else
			{
            break;
			}
		
		}
	
}

// RIC
void gcProcess_Debug(TValue v)
{
	// 1. Notificar si el objeto está en la zona vmImg (readonly) o no requiere procesamiento
	if (SKIP_PROCESSING(v)) 
    {
        if (isReadOnly(v))
        {
            //DMESG("  [gcProcess] Skip: %p es READONLY", v);
        }
		else
		{
			//DMESG("  [gcProcess] Skip");
		}
        return;
    }
    
	VVLOG("gcProcess: %p", v);
    
	MARK(v);
    
    // Guardamos el tamaño inicial de la cola para saber cuántos hijos introduce este objeto
    unsigned int initialQueueLen = workQueue.getLength();

	auto scan = getScanMethod(VT(v) & ~ANY_MARKED_MASK);
    if (scan) scan((RefObject *)v);

    // Calculamos cuántos hijos añadió el scan directo
    unsigned int addedChildren = workQueue.getLength() - initialQueueLen;
    if (addedChildren > 0)
    {
        DMESG("      [gcProcess] Objeto %p metio %d hijos directos en workQueue", v, addedChildren);
    }
	
    for (;;)
		{
		while (workQueue.getLength())
			{
			auto curr = (RefObject *)workQueue.pop();
            
			VVLOG(" - %p", curr);
            
			scan = getScanMethod(curr->vt() & ~ANY_MARKED_MASK);
            
            // Guardamos el tamaño de la cola antes de que scan(curr) añada sub-hijos
            unsigned int subQueueLenBefore = workQueue.getLength();

			if (scan) scan(curr);

            unsigned int subChildren = workQueue.getLength() - subQueueLenBefore;
            if (subChildren > 0)
            {
                DMESG("    - Sub-objeto %p metio %d hijos en workQueue", curr, subChildren);
            }
			}
			
		if (pendingArrays)
			{
            auto pa = pendingArrays;
            pendingArrays = pa->next;
            auto data = pa->data;
            auto len = pa->len;
            xfree(pa);

            unsigned int queueBeforeArray = workQueue.getLength();
            gcScanMany(data, len);

            DMESG("  [gcProcess] Arreglo pendiente procesado: metió %d elementos a workQueue", workQueue.getLength() - queueBeforeArray);
			}
		else
			{
            break;
			}
		}
}




static void mark(int flags)
{
	
	#ifdef PXT_GC_DEBUG
		flags |= 2;
	#endif
	
    auto data = gcRoots.getData();
    auto len = gcRoots.getLength();
	
    if (flags & 2)
		{
        DMESG("--MARK");
        DMESG("RP:%p/%d", data, len);
		}
	
	// bucle por todos los GC Roots
	for (unsigned i = 0; i < len; ++i)
		{
        
		auto d = data[i];
        
		if ((uintptr_t)d & 1)
			{
            d = *(TValue *)((uintptr_t)d & ~1);
			}
			
		// CORREGIDO: Imprimimos el elemento actual del GC Root en lugar de variables inexistentes 'f', 'ptr', 'end'
        if (flags & 2)
			{
            DMESG("  root slot [%d] = %p", i, (void*)d);
			}
		
		gcProcess(d);
		
		}

	#ifdef PXT_GC_THREAD_LIST
		RIC por aqui NO entra
	
		for (auto ctx = threadContexts; ctx; ctx = ctx->next)
			{
			
			gcProcess(ctx->thrownValue);
			
			for (auto seg = &ctx->stack; seg; seg = seg->next)
				{
				
				auto ptr = (TValue *)threadAddressFor(ctx, seg->top);
				
				auto end = (TValue *)threadAddressFor(ctx, seg->bottom);
				
				VLOG("mark: %p - %p", ptr, end);
				
				while (ptr < end)
					{
					// RIC DEBUG
					//DMESG("    stack slot %p = %p", ptr, *ptr);
					
					gcProcess(*ptr++);
					}
				
				}
			
			}
	#else
		// RIC por aqui entra
	
		// CORREGIDO: En esta rama no existen las variables 'f' o 'ptr'. Solo se indica el inicio del procesado de stacks.
		if (flags & 2) 
			{
			DMESG("  processing stacks (flags=%d)...", flags);
			}
		
		gcProcessStacks(flags);
		
	#endif

    if (globals)
		{
			
		#ifdef PXT_VM
			auto nonPtrs = vmImg->infoHeader->nonPointerGlobals;
		#else
			auto nonPtrs = bytecode[21];
		#endif
        
		len = getNumGlobals() - nonPtrs;
        
		data = globals + nonPtrs;
        
		if (flags & 2) DMESG("RG:%p/%d", data, len);
        
		VLOG("globals: %p %d", data, len);
        
		for (unsigned i = 0; i < len; ++i)
			{
            gcProcess(*data++);
			}
		
		}

	
	#ifndef PXT_VM
		RIC por aqui NO entra
		
		data = tempRoot;
		len = tempRootLen;
		for (unsigned i = 0; i < len; ++i)
			{
			gcProcess(*data++);
			}
		
	#endif
	
}

	// solo para declaracion, esta mas abajo
	static uint32_t getObjectSize(RefObject *o);
static void mark_Debug(int flags)
{
	#ifdef PXT_GC_DEBUG
		flags |= 2;
	#endif

    // --- CONTADORES DE RAÍCES / STACKS / GLOBALES ---
    uint32_t countRoots = 0;
    uint32_t countGlobals = 0;
    // Nota: El recuento exacto de celdas de stack depende de gcProcessStacks, 
    // pero podemos contar los elementos de alto nivel procesados.

    auto data = gcRoots.getData();
    auto len = gcRoots.getLength();
	
    if (flags & 2)
		{
        DMESG("--MARK");
        DMESG("RP:%p/%d", data, len);
		}
	
	// bucle por todos los GC Roots
	for (unsigned i = 0; i < len; ++i)
		{
		
		// direccion de un root
		//	- si el bit 0 es 1 es que es un puntero a donde esta el valor real
		//	- si el bit 0 es 0 es que ya es el valor real
		auto d = data[i];
		
		//DMESG("  root slot %d [%x] ",i, d);
		
		if ((uintptr_t)d & 1)
			{
            d = *(TValue *)((uintptr_t)d & ~1);
			//DMESG("  root slot %d puntero a [%x] ",i, d);
			}
			
        if (flags & 2)
			{
            DMESG("  root slot [%d] = %p", i, (void*)d);
			}
			
		DMESG("  root slot [%d] = %x", i,d);

		gcProcess_Debug(d);
        
		countRoots++; // Contador de raíces procesadas
		}

	#ifdef PXT_GC_THREAD_LIST
		for (auto ctx = threadContexts; ctx; ctx = ctx->next)
			{
			gcProcess(ctx->thrownValue);
			
			for (auto seg = &ctx->stack; seg; seg = seg->next)
				{
				auto ptr = (TValue *)threadAddressFor(ctx, seg->top);
				auto end = (TValue *)threadAddressFor(ctx, seg->bottom);
				
				VLOG("mark: %p - %p", ptr, end);
				
				while (ptr < end)
					{
					gcProcess(*ptr++);
					}
				}
			}
	#else
		if (flags & 2) 
			{
			DMESG("  processing stacks (flags=%d)...", flags);
			}
		
		gcProcessStacks(flags);
	#endif

    if (globals)
		{
		#ifdef PXT_VM
			auto nonPtrs = vmImg->infoHeader->nonPointerGlobals;
		#else
			auto nonPtrs = bytecode[21];
		#endif
        
		len = getNumGlobals() - nonPtrs;
		data = globals + nonPtrs;
        
		if (flags & 2) DMESG("RG:%p/%d", data, len);
        
		VLOG("globals: %p %d", data, len);
        
		for (unsigned i = 0; i < len; ++i)
			{
			
			DMESG("  global [%d]",i);
            
			gcProcess_Debug(*data++);
            
			countGlobals++; // Contador de globales procesadas
			
			}
		}

	#ifndef PXT_VM
		data = tempRoot;
		len = tempRootLen;
		for (unsigned i = 0; i < len; ++i)
			{
            gcProcess(*data++);
			}
	#endif

    // --- RECUENTO DE OBJETOS TOTALES Y MARCADOS EN EL HEAP ---
    uint32_t totalObjetosHeap = 0;
    uint32_t marcadosObjetosHeap = 0;
	uint32_t sizeocupado = 0;
	uint32_t sizemarcado = 0;
	uint32_t sizelibre = 0;

    for (auto h = firstBlock; h; h = h->next)
    {
        auto d = h->data;
        auto words = BYTES_TO_WORDS(h->blockSize);
        auto end = d + words;

		DMESG("GC recorremos todo un bloque con %d elementos word (4bytes PXT32 o 8bytes PXT64)",words);
		
        while (d < end)
        {
            uint32_t objSize = getObjectSize(d);
            if (objSize == 0)
				{
				DMESG("GC objSize = 0 ERROR");
				
				break; // Protección por si acaso
				}
			
			printVTableInfo(d);
			//DMESG("  elem GC %x | size: %d words | free: %d | marked: %d",(void*)d,objSize,IS_FREE(d->vtable) ? 1 : 0,IS_MARKED(d->vtable) ? 1 : 0);
			
            // Si no es un bloque libre de memoria, es un objeto/array real
            if (!IS_FREE(d->vtable))
				{
                totalObjetosHeap++;
                if (IS_MARKED(d->vtable))
					{
                    marcadosObjetosHeap++;
					sizemarcado+=objSize;
					}
				else
					{
					sizeocupado+=objSize;
					}
				}
			else
				{
				sizelibre+=objSize;
				}
			
            d += objSize;
        }
		
		DMESG("GC block recorrido : ocupado %d marcado %d libre %d",sizeocupado,sizemarcado,sizelibre);
    }

    // --- IMPRESIÓN DEL RESUMEN POR DMESG ---
    DMESG("GC Mark Finished: Roots=%d, Globals=%d | Heap Objects: %d/%d marked",countRoots, countGlobals, marcadosObjetosHeap, totalObjetosHeap);
}




static uint32_t getObjectSize(RefObject *o) {
    auto vt = o->vt() & ~ANY_MARKED_MASK;
    uint32_t r;
    GC_CHECK(vt != 0, 49);
    if (IS_VAR_BLOCK(vt)) {
        r = VAR_BLOCK_WORDS(vt);
    } else {
        auto sz = getSizeMethod(vt);
        // GC_CHECK(0x2000 <= (intptr_t)sz && (intptr_t)sz <= 0x100000, 47);
        r = sz(o);
    }
    GC_CHECK(1 <= r && (r <= BYTES_TO_WORDS(GC_MAX_ALLOC_SIZE) || IS_FREE(vt)), 41);
    return r;
}

// RIC
static void setupFreeBlock(GCBlock *curr)
{
	gcStats.numBlocks++;
	
	// RIC DEBUG
	DMESG("    GC numBlocks = %d",gcStats.numBlocks);
	
    gcStats.totalBytes += curr->blockSize;
	
    curr->data[0].setVT(FREE_MASK | (TOWORDS(curr->blockSize) << 2));
	
    ((RefBlock *)curr->data)[0].nextFree = firstFree;
	
    firstFree = (RefBlock *)curr->data;
	
    // original , la primera vez un 25% de la memoria
	// midPtr = (uint8_t *)curr->data + curr->blockSize / 4;
	midPtr = (uint8_t *)curr->data + PORCENTAJE_MIDPTR_FIRST;
}

static void linkFreeBlock(GCBlock *curr) {
    // blocks need to be sorted by address for midPtr to work
    if (!firstBlock || curr < firstBlock) {
        curr->next = firstBlock;
        firstBlock = curr;
    } else {
        for (auto p = firstBlock; p; p = p->next) {
            if (!p->next || curr < p->next) {
                curr->next = p->next;
                p->next = curr;
                break;
            }
        }
    }
}

void gcPreAllocateBlock(uint32_t sz) {
    auto curr = (GCBlock *)GC_ALLOC_BLOCK(sz);
    curr->blockSize = sz - sizeof(GCBlock);
    LOG("GC pre-alloc: %p", curr);
    GC_CHECK((curr->blockSize & 3) == 0, 40);
    setupFreeBlock(curr);
    linkFreeBlock(curr);
}

// RIC
static GCBlock *allocateBlockCore()
{
	
	int sz = GC_BLOCK_SIZE;
	void *dummy = NULL;
	
	// RIC este IF NO entra
	#ifdef GC_GET_HEAP_SIZE
		RIC por aqui NO entra
		
		if (firstBlock)
			{

		#ifdef GC_STACK_BASE
			if (!firstBlock->next)
				{
				
				int memSize = getConfig(CFG_RAM_BYTES, 0);
				int codalEnd = GC_STACK_BASE;
				
				// round up to 1k - there is sometimes a few bytes below the stack
				codalEnd = (codalEnd + 1024) & ~1023;
				int codalSize = codalEnd & 0xffffff;
				sz = memSize - codalSize - 4;
				
				if (sz > 0)
					{
					auto curr = (GCBlock *)codalEnd;
					curr->blockSize = sz - sizeof(GCBlock);
					return curr;
					}
				
				}
		#endif
        
			gc(2); // dump roots
			
			soft_panic(PANIC_GC_OOM);
			
			}
		
		auto lowMem = getConfig(CFG_LOW_MEM_SIMULATION_KB, 0);
		auto sysHeapSize = getConfig(CFG_SYSTEM_HEAP_BYTES, 4 * 1024);
		auto heapSize = GC_GET_HEAP_SIZE();
		
		DMESG("heap: %d (minus %d sys bytes)", heapSize, sysHeapSize);
		sz = heapSize - sysHeapSize;
		
		if (lowMem)
			{
			
			auto memIncrement = 32 * 1024;
			// get the memory size - assume it's increment of 32k,
			// and we don't statically allocate more than 32k
			auto memSize = ((heapSize + memIncrement - 1) / memIncrement) * memIncrement;
			int fillerSize = memSize - lowMem * 1024;
			
			if (fillerSize > 0)
				{
				dummy = GC_ALLOC_BLOCK(fillerSize);
				sz -= fillerSize;
				}
			
			}
		
	#endif
    
	auto curr = (GCBlock *)GC_ALLOC_BLOCK(sz);
	
    curr->blockSize = sz - sizeof(GCBlock);
	
    // make sure reference to allocated block is stored somewhere, otherwise
    // GCC optimizes out the call to GC_ALLOC_BLOCK
    curr->data[4].setVT((uintptr_t)dummy);
	
    return curr;
}

// RIC
__attribute__((noinline)) static void allocateBlock()
{
	auto curr = allocateBlockCore();
	
    DMESG("    GC allocate block %d bytes from [%p to %p]",
		  curr->blockSize,(void *)curr->data,(void *)((uint8_t *)curr->data + curr->blockSize));
	
    GC_CHECK((curr->blockSize & 3) == 0, 40);
	
    setupFreeBlock(curr);
	
    linkFreeBlock(curr);
	
}

// RIC
static void sweep(int flags)
{
	RefBlock *prevFreePtr = NULL;
    uint32_t freeSize = 0;
    uint32_t totalSize = 0;
    uint32_t maxFreeBlock = 0;
    firstFree = NULL;

    gcStats.numGC++;
	
#if defined(__EMSCRIPTEN__)
	contador_destroyVTPunterosInvalidos=0;
#endif
	
    for (auto h = firstBlock; h; h = h->next)
		{
        
		auto d = h->data;
        auto words = BYTES_TO_WORDS(h->blockSize);
        auto end = d + words;
        totalSize += words;
        
		VLOG("sweep: %p - %p", d, end);
        
		while (d < end)
			{
			
			// RIC DEBUG
			// PROTECCIÓN ANTI-BUCLE: Asegurar que la pieza devuelta sea > 0
            uint32_t objSize = getObjectSize(d);
            if (objSize == 0)
				{
                DMESG("CRITICAL ERROR: getObjectSize(%p) returned 0! Heap corrupted.", d);
                target_panic(41); // O romper el bucle forzadamente: objSize = 1;
				}				
			
			if (IS_LIVE(d->vtable))
				{
				
				//VVLOG("Live %p", d);
				
                d->setVT(d->vt() & ~MARKED_MASK);
                d += getObjectSize(d);				
				
				}
			else
				{
				
				// RIC DEBUG
				//DMESG("NOT Live %p", d);
                
				auto start = (RefBlock *)d;
                
				while (d < end)
					{
						
					//DMESG("while vuelta");
					
					// RIC DEBUG
					uint32_t innerObjSize = getObjectSize(d);
                    if (innerObjSize == 0)
						{
                        DMESG("CRITICAL ERROR: getObjectSize(%p) inner returned 0!", d);
                        target_panic(41);
						}					
					
					if (IS_FREE(d->vtable))
						{
						VVLOG("Free %p", d);				// antes -> VVLOG
						}
					else if (IS_LIVE(d->vtable))
						{
						//DMESG("break");
                        break;
						}
					else if (IS_ARRAY(d->vtable))
						{
						VVLOG("Dead Arr %p", d);			// antes -> VVLOG
						}
					else
						{
                        VVLOG("Dead Obj %p", d);			// antes -> VVLOG

						// RIC DEBUG
						{
							// Imprimimos la VTable y el puntero al destructor
							//auto vt = d->vtable;
							//DMESG("CRASH_DIAG: Obj=%p VT=%p Magic=0x%x DestroyPtr=%p", d, vt, vt ? vt->magic : 0, vt ? (void*)vt->methods[0] : nullptr);
						}
						
                        GC_CHECK(d->vtable->magic == VTABLE_MAGIC, 41);
                        d->destroyVT();
                        //VVLOG("destroyed");					// antes -> VVLOG
						}
						
					
					// antes -> d += getObjectSize(d);
					d += innerObjSize;
					
					}
				
                auto sz = d - (RefObject *)start;
                freeSize += sz;
                
				if (sz > (int)maxFreeBlock) maxFreeBlock = sz;
				
			#ifdef PXT_GC_CHECKS
				//RIC por aqui entra
				
				memset((void *)start, 0xff, WORDS_TO_BYTES(sz));
				
			#endif
                
				start->setVT((sz << 2) | FREE_MASK);
				
                if (sz > 1)
					{
                    
					start->nextFree = NULL;
					
                    if (!prevFreePtr)
						{
                        firstFree = start;
						}
					else
						{
                        prevFreePtr->nextFree = start;
						}
						
					prevFreePtr = start;
					
					}
				
				}
			
			}
		
		}
	
	if (midPtr)
		{
        
		uint32_t currFree = 0;
		
	#if defined(PXT_ESP32) || defined(__EMSCRIPTEN__)
		auto limit = freeSize * PORCENTAJE_MIDPTR_NEXTS;		
	#else
        auto limit = freeSize * 1 / 2;
	#endif
	
		// PROTECCIÓN ANTI-BUCLE CIRCULAR EN firstFree
        uint32_t maxIterations = 100000; // Límite de seguridad
        for (auto p = firstFree; p && maxIterations > 0; p = p->nextFree, --maxIterations)
			{
			
			auto len = VAR_BLOCK_WORDS(p->vtable);
			
            currFree += len;

            if (currFree > limit)
				{
                midPtr = (uint8_t *)p + ((limit - currFree + len) << 2);
                break;
				}
				
			}
        
		// RIC DEBUG
        if (maxIterations == 0)
			{
            DMESG("CRITICAL ERROR: Circular loop detected in firstFree list!");
			}	
	
		}
	
    freeSize = WORDS_TO_BYTES(freeSize);
    totalSize = WORDS_TO_BYTES(totalSize);
    maxFreeBlock = WORDS_TO_BYTES(maxFreeBlock);

    gcStats.lastFreeBytes = freeSize;
    gcStats.lastMaxBlockBytes = maxFreeBlock;

    if (gcStats.minFreeBytes == 0 || gcStats.minFreeBytes > freeSize) gcStats.minFreeBytes = freeSize;

    if (flags & 1)		VLOG("    GC %d/%d free; %d maxBlock", freeSize, totalSize, maxFreeBlock);
	else				VLOG("    GC %d/%d free; %d maxBlock", freeSize, totalSize, maxFreeBlock);
	
#ifndef GC_GET_HEAP_SIZE
	// RIC por aqui entra

	// if the heap is 90% full, allocate a new block
	if (freeSize * 10 <= totalSize)
		{
		allocateBlock();
		}
	
#endif

	#if defined(__EMSCRIPTEN__)
		//DMESG("        sweep con %d punteros invalidos que evitamos",contador_destroyVTPunterosInvalidos);
	#endif
	
}

// RIC
void gc(int flags)
{
	// RIC DEBUG
	uint32_t libresAntes = gcStats.lastFreeBytes;
	
	// RIC DEBUG
	if (flags == 0)
		{
		//DMESG("gc(0) start");
		GC0_DBG("    Mem. ocupada %d bytes",gcStats.totalBytes - gcStats.lastFreeBytes);
		
		/*	RIC: punto de enganche del snapshot de heap para analisis externo.
			decide, segun contador/evento, si toca volcar la memoria a disco
		*/
		#ifdef __EMSCRIPTEN__
			maybeDumpHeapSnapshot();
		#endif
		}
	
    startPerfCounter(PerfCounters::GC);
    GC_CHECK(!(inGC & IN_GC_COLLECT), 40);
    inGC |= IN_GC_COLLECT;
	
		VLOG("    GC mark");
	
		//mark_Debug(flags);
		mark(flags);
	
		VLOG("    GC sweep");
	
		sweep(flags);

		if (flags == 0)
			{	
			GC0_DBG("gc(0) end");
			GC0_DBG("    Mem. ocupada %d bytes en %d block/s",gcStats.totalBytes - gcStats.lastFreeBytes,gcStats.numBlocks);
			GC0_DBG("");
			
			// RIC si hemos hecho el 1er. snapshoot al empezar, hacemos otro al finalizar la limpieza
			#ifdef __EMSCRIPTEN__
				if (snapshotSeq == 1)
					{
					dumpHeapSnapshotToDisk();
					}
			#endif
			}
	
    stopPerfCounter(PerfCounters::GC);
    inGC &= ~IN_GC_COLLECT;
}

#ifdef GC_GET_HEAP_SIZE
	RIC por aqui NO entra

	extern "C" void free(void *ptr)
	{
		if (!ptr)
			return;
		if (inGCArea(ptr))
			app_free(ptr);
		else
			xfree(ptr);
	}

	extern "C" void *malloc(size_t sz) {
		if (PXT_IN_ISR() || inGC)
			return xmalloc(sz);
		else
			return app_alloc(sz);
	}

	extern "C" void *realloc(void *ptr, size_t size)
	{
		if (inGCArea(ptr)) {
			void *mem = malloc(size);

			if (ptr != NULL && mem != NULL) {
				auto r = (uintptr_t *)ptr;
				GC_CHECK((r[-1] >> (HIGH_SHIFT + 1)) == 3, 41);
				size_t blockSize = VAR_BLOCK_WORDS(r[-1]);
				memcpy(mem, ptr, min(blockSize * sizeof(void *), size));
				free(ptr);
			}

			return mem;
		} else {
			return device_realloc(ptr, size);
		}
	}

#endif

void *gcAllocateArray(int numbytes) {
    numbytes = ALIGN_TO_WORD(numbytes);
    numbytes += sizeof(void *);
    auto r = (uintptr_t *)gcAllocate(numbytes);
    *r = ARRAY_MASK | (TOWORDS(numbytes) << 2);
    return r + 1;
}

static void *gcAllocAt(void *hint, int numbytes) {
    gc(0);
    size_t numwords = BYTES_TO_WORDS(ALIGN_TO_WORD(numbytes));

    for (auto p = firstFree; p; p = p->nextFree) {
        GC_CHECK(!isReadOnly((TValue)p), 49);
        auto vt = p->vtable;
        GC_CHECK(IS_FREE(vt), 43);
        int offset = BYTES_TO_WORDS((uint8_t *)hint - (uint8_t *)p);
        int left = (int)(VAR_BLOCK_WORDS(vt) - numwords - offset);
        // we give ourselves some space here, so we don't get some strange overlaps
        if (offset >= 8 && left >= 8) {
            auto nf = (RefBlock *)((void **)p + numwords + offset);
            nf->setVT((left << 2) | FREE_MASK);
            nf->nextFree = p->nextFree;
            p->nextFree = nf;
            p->setVT((offset << 2) | FREE_MASK);
            p = (RefBlock *)((void **)p + offset);
            p->setVT(0);
            return p;
        }
    }

    return NULL;
}

void *app_alloc_at(void *at, int numbytes) {
    if (numbytes < 8)
        return NULL;
    if (!at)
        return NULL;

    numbytes = ALIGN_TO_WORD(numbytes) + sizeof(void *);
    auto r = (uintptr_t *)gcAllocAt((uintptr_t *)at - 1, numbytes);
    if (!r)
        return NULL;
    *r = ARRAY_MASK | PERMA_MASK | (TOWORDS(numbytes) << 2);
    gc(0);
    return r + 1;
}

void *app_alloc(int numbytes) {
    if (!numbytes)
        return NULL;

    // gc(0);
    auto r = (uintptr_t *)gcAllocateArray(numbytes);
    r[-1] |= PERMA_MASK;
    return r;
}

void *app_free(void *ptr) {
    auto r = (uintptr_t *)ptr;
    GC_CHECK((r[-1] >> (HIGH_SHIFT + 1)) == 3, 41);
    r[-1] |= FREE_MASK;
    return r;
}

void gcFreeze() {
    inGC |= IN_GC_FREEZE;
}

void gcReset() {
    inGC &= ~IN_GC_FREEZE;

    gcRoots.setLength(0);

    if (inGC)
        oops(41);

    if (workQueue.getLength())
        oops(41);

    memset(&gcStats, 0, sizeof(gcStats));
    firstFree = NULL;
    for (auto h = firstBlock; h; h = h->next) {
        setupFreeBlock(h);
    }
}

#ifdef PXT_VM
void gcPreStartup() {
    inGC |= IN_GC_PREALLOC;
}

void gcStartup() {
    inGC &= ~IN_GC_PREALLOC;
}
#endif

// RIC
void *gcAllocate(int numbytes)
{
	
	size_t numwords = BYTES_TO_WORDS(ALIGN_TO_WORD(numbytes));
    //VVLOG("GC alloc %d bytes que seran %d words", numbytes, numwords);

    if (numbytes > GC_MAX_ALLOC_SIZE) soft_panic(PANIC_GC_TOO_BIG_ALLOCATION);

    if (PXT_IN_ISR() || (inGC & (IN_GC_PREALLOC | IN_GC_ALLOC | IN_GC_COLLECT | IN_GC_FREEZE))) target_panic(PANIC_CALLED_FROM_ISR);

    inGC |= IN_GC_ALLOC;

	#if defined(PXT_GC_CHECKS) && !defined(PXT_VM)
		RIC por aqui NO entra
		{
			auto curr = getThreadContext();
			if (curr && !curr->stack.top)
				oops(46);
		}
	#endif

	#ifdef PXT_GC_STRESS
		RIC por aqui NO entra
		gc(0);
	#endif
	
    for (int i = 0;; ++i)
		{
        
		RefBlock *prev = NULL;
		
        for (auto p = firstFree; p; p = p->nextFree)
			{
			
			// RIC DEBUG
			if (p == firstFree)	
				{
				//VVLOG("GC First free %p", p);
				}
			else
				{
				VVLOG("GC %p", p);
				}
			
            if (i == 0 && (uint8_t *)p > midPtr)
				{
                VLOG("GC past midptr %p", midPtr);
                break;
				}
			
            GC_CHECK(!isReadOnly((TValue)p), 49);
			
            auto vt = p->vtable;
            if (!IS_FREE(vt)) oops(43);
			
            int left = (int)(VAR_BLOCK_WORDS(vt) - numwords);
			
            //VVLOG("GC stats -> %p %d - %d = %d", (void *)vt, (int)VAR_BLOCK_WORDS(vt), (int)numwords, left);
			//VVLOG("GC stats -> %d - %d = %d words quedaran",(int)VAR_BLOCK_WORDS(vt), (int)numwords, left);
			
            if (left >= 0)
				{
                
				auto nf = (RefBlock *)((void **)p + numwords);
                auto nextFree = p->nextFree; // p and nf can overlap when allocating 4 bytes
                
				// VVLOG("nf=%p nef=%p", nf, nextFree);
                
				if (left) nf->setVT((left << 2) | FREE_MASK);
				
                if (left >= 2)
					{
                    nf->nextFree = nextFree;
					}
				else
					{
                    nf = nextFree;
					}
                
				if (prev)	prev->nextFree = nf;
                else		firstFree = nf;
                
				p->setVT(0);
                
				//VVLOG("GC found space -> %p %d words, next free %p -> %p,%p", p, numwords, nf, nf ? nf->nextFree : 0,nf ? (void *)nf->vtable : 0);
				//VVLOG("GC found space -> %p %d words, next free %p", p, numwords, nf);
                
				GC_CHECK(!nf || !nf->nextFree || !isReadOnly((TValue)nf->nextFree), 48);
                
				inGC &= ~IN_GC_ALLOC;
                
				return p;
				
				}
			
			prev = p;
			
			}

        
        if (i == 0)						// we didn't find anything, try GC
			{
			GC0_DBG("");
			GC0_DBG("GC estamos por encima de midptr");
			gc(0);
			}
        else if (i == 1)				// GC didn't help, try new block
			{
            DMESG("gcAlloc(%d) (%d/%d free; %d max block) -> new block", numbytes,gcStats.lastFreeBytes, gcStats.totalBytes, gcStats.lastMaxBlockBytes);
            allocateBlock();
			}
		else							// the block allocated was apparently too small
			{
            soft_panic(PANIC_GC_OOM);
			}
			
		}
	
}

static void removePtr(TValue v) {
    int len = gcRoots.getLength();
    auto data = gcRoots.getData();
    // scan from the back, as this is often used as a stack
    for (int i = len - 1; i >= 0; --i) {
        if (data[i] == v) {
            if (i == len - 1) {
                gcRoots.pop();
            } else {
                data[i] = gcRoots.pop();
            }
            return;
        }
    }
    oops(40);
}

void registerGC(TValue *root, int numwords) {
    if (!numwords)
        return;

    if (numwords > 1) {
        while (numwords-- > 0) {
            registerGC(root++, 1);
        }
        return;
    }

    gcRoots.push((TValue)((uintptr_t)root | 1));
}

void unregisterGC(TValue *root, int numwords) {
    if (!numwords)
        return;
    if (numwords > 1) {
        while (numwords-- > 0) {
            unregisterGC(root++, 1);
        }
        return;
    }

    removePtr((TValue)((uintptr_t)root | 1));
}

void registerGCPtr(TValue ptr) {
    if (isReadOnly(ptr))
        return;
    gcRoots.push(ptr);
}

void unregisterGCPtr(TValue ptr) {
    if (isReadOnly(ptr))
        return;
    removePtr(ptr);
}

void RefImage::scan(RefImage *t) {
    gcScan((TValue)t->buffer);
}

void RefCollection::scan(RefCollection *t) {
    gcScanSegment(t->head);
}

void RefAction::scan(RefAction *t) {
    gcScanMany(t->fields, t->len);
}

void RefRefLocal::scan(RefRefLocal *t) {
    gcScan(t->v);
}

void RefMap::scan(RefMap *t) {
    gcScanSegment(t->keys);
    gcScanSegment(t->values);
}

void RefRecord_scan(RefRecord *r) {
    VTable *tbl = getVTable(r);
    gcScanMany(r->fields, BYTES_TO_WORDS(tbl->numbytes - sizeof(RefRecord)));
}

#define SIZE(off) TOWORDS(sizeof(*t) + (off))

unsigned RefImage::gcsize(RefImage *t) {
    return SIZE(0);
}

unsigned RefCollection::gcsize(RefCollection *t) {
    return SIZE(0);
}

unsigned RefAction::gcsize(RefAction *t) {
    return SIZE(WORDS_TO_BYTES(t->len));
}

unsigned RefRefLocal::gcsize(RefRefLocal *t) {
    return SIZE(0);
}

unsigned RefMap::gcsize(RefMap *t) {
    return SIZE(0);
}

// RIC
// bajo emscripten la imagen .PXT64 se carga en un buffer normal del heap,
// no hay mapeo de flash con bits fijos: comparamos contra el rango real
inline bool EmscriptenIsReadOnlyImg(const void *v)
{
	// en EMSCRIPTEN miramos si el puntero esta entre el espacio de memoria donde esta alojada la imagen que hemos leido de disco
	auto p = (const uint8_t *)v;
	
	if ( p >= (const uint8_t *)vmImg->dataStart && p <  (const uint8_t *)vmImg->dataEnd )
		{
		// RIC DEBUG
		TRACE("Addr. %p inside vmImg readonly",p);
		return true;
		}
	else
		{
		//DMESG("Addr. %p NOT inside vmImg readonly",p);
		return false;
		}
}


} // namespace pxt
