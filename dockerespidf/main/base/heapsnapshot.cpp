#include "pxtbase.h"
#include "vm.h"
#include "heapsnapshot.h"

#include <stdio.h>

#ifdef __EMSCRIPTEN__
	#include <emscripten.h>
	#include <emscripten/heap.h>
#endif

namespace pxt {

static uint32_t gcZeroCallCount = 0;
static uint32_t gcSnapshotEveryN = 0;    // 0 = desactivado por contador
static bool gcSnapshotRequested = false;
static bool haltAfterSnapshot = true;    // por defecto congelamos la VM tras el 1er volcado
static bool halted = false;
uint32_t snapshotSeq = 0;

void setGCSnapshotInterval(uint32_t n) {
    gcSnapshotEveryN = n;
    DMESG("heapsnapshot: intervalo por contador de gc(0) = %u", n);
}

void requestHeapSnapshot() {
    gcSnapshotRequested = true;
    DMESG("heapsnapshot: snapshot solicitado para el proximo gc(0)");
}

void setHaltAfterSnapshot(bool halt) {
    haltAfterSnapshot = halt;
}

bool isHeapSnapshotHalted() {
    return halted;
}

// Cabecera fija al principio del fichero de snapshot. Va ANTES del volcado
// crudo de memoria, para que el parser Node.js pueda localizar sin
// heuristicas la tabla de globals y la imagen .pxt64 cargada, ambas dentro
// del mismo volcado (son offsets validos en ese mismo fichero, ver
// heapsnapshot.h).
#pragma pack(push,1)
struct HeapSnapshotHeader
	{
    uint64_t magic;         			// 8 bytes ASCII: "PXHEAPSN"
    uint32_t version;           		// version de este formato de cabecera (1)
    uint32_t headerSize;       			// sizeof(HeapSnapshotHeader), para poder crecerla en el futuro
    uint32_t seq;             			// numero de snapshot (0,1,2,...) dentro de esta ejecucion
    uint32_t gcCallCount;      			// valor de gcZeroCallCount en el momento del volcado
    uint32_t wordSize;         			// sizeof(TValue) REAL de este build (4 en WASM32 -> PXT32)
    uint32_t globalsPtr;      			// direccion de `globals` = offset dentro de este mismo fichero
    uint32_t numGlobals;      			// getNumGlobals(), en "words" de wordSize bytes
    uint32_t nonPointerGlobals;			// vmImg->infoHeader->nonPointerGlobals
    uint32_t vmImageHeaderPtr; 			// direccion del InfoHeader (VM_MAGIC0/1) de la imagen .pxt64 cargada
    uint32_t vmImageSize;      			// infoHeader->imageSize
    uint32_t heapMemOffset;    			// a partir de que byte de ESTE fichero empieza el volcado de memoria
    uint32_t heapMemSize;      			// tamano en bytes del volcado de memoria que sigue a la cabecera
	};
#pragma pack(pop)

// construido byte a byte para que, al escribirse little-endian (nativo en
// WASM/x86), el fichero contenga literalmente los 8 bytes ASCII "PXHEAPSN"
// y sea reconocible a simple vista con `xxd`/`hexdump`.
#define HEAP_SNAPSHOT_MAGIC   			((uint64_t)'P' | ((uint64_t)'X' << 8) | ((uint64_t)'H' << 16) | ((uint64_t)'E' << 24) | ((uint64_t)'A' << 32) | ((uint64_t)'P' << 40) | ((uint64_t)'S' << 48) | ((uint64_t)'N' << 56))
#define HEAP_SNAPSHOT_VERSION			1

void dumpHeapSnapshotToDisk() {

    char filename[64];
    snprintf(filename, sizeof(filename), "Storage/heapsnap_%03u.bin", (unsigned)snapshotSeq);

    FILE *f = fopen(filename, "wb");
    if (!f) {
        DMESG("heapsnapshot: ERROR no se pudo abrir '%s' para escritura", filename);
        return;
    }

#ifdef __EMSCRIPTEN__
	size_t memSize = (size_t)emscripten_get_heap_size();
	DMESG("emscripten_get_heap_size() = %x",memSize);
#else
    // fuera de Emscripten no tenemos forma generica de saber el tamano de la
    // memoria lineal; este modulo esta pensado para el build WASM.
    size_t memSize = 0;
#endif

    HeapSnapshotHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = HEAP_SNAPSHOT_MAGIC;
    hdr.version = HEAP_SNAPSHOT_VERSION;
    hdr.headerSize = sizeof(HeapSnapshotHeader);
    hdr.seq = snapshotSeq;
    hdr.gcCallCount = gcZeroCallCount;
    hdr.wordSize = (uint32_t)sizeof(TValue);
    hdr.globalsPtr = (uint32_t)(uintptr_t)globals;
    hdr.numGlobals = (uint32_t)getNumGlobals();
    hdr.nonPointerGlobals = vmImg ? vmImg->infoHeader->nonPointerGlobals : 0;
    hdr.vmImageHeaderPtr = vmImg ? (uint32_t)(uintptr_t)vmImg->infoHeader : 0;
    hdr.vmImageSize = vmImg ? vmImg->infoHeader->imageSize : 0;
    hdr.heapMemOffset = sizeof(HeapSnapshotHeader);
    hdr.heapMemSize = (uint32_t)memSize;

    fwrite(&hdr, sizeof(hdr), 1, f);

    if (memSize > 0) {
        // RIC: volcamos la memoria lineal completa desde la direccion 0.
        // Es un truco deliberado (leer a traves de un puntero NULL es UB en
        // C++ "puro", pero aqui no hay OS que proteja la pagina 0: es memoria
        // WASM normal) para que CUALQUIER puntero de 32 bits que aparezca
        // dentro de un objeto vivo (vtable, Segment.data, etc.) sea
        // directamente un offset valido dentro de este mismo fichero.
        const uint8_t *base = (const uint8_t *)0;
        fwrite(base, 1, memSize, f);
    }

    fclose(f);

    DMESG("heapsnapshot: volcado '%s' OK (%d bytes totales, gcCallCount=%d, globals=%x ( #%d )",
          filename, (unsigned)(sizeof(hdr) + memSize), (unsigned)gcZeroCallCount,
          (unsigned)hdr.globalsPtr, (unsigned)hdr.numGlobals);

    snapshotSeq++;
    gcSnapshotRequested = false;

    if (haltAfterSnapshot) {
        halted = true;
        DMESG("heapsnapshot: ejecucion detenida (halted) tras el volcado");
    }
}

void maybeDumpHeapSnapshot() {

    // si ya estamos congelados por un snapshot previo, no seguimos contando
    // ni volcando de nuevo
    if (halted) return;

    gcZeroCallCount++;

    bool trigger = gcSnapshotRequested;

    if (!trigger && gcSnapshotEveryN > 0 && (gcZeroCallCount % gcSnapshotEveryN) == 0) trigger = true;

    if (trigger) dumpHeapSnapshotToDisk();
}

} // namespace pxt