#ifndef __HEAPSNAPSHOT_H
#define __HEAPSNAPSHOT_H

/*  RIC

    Modulo de volcado de memoria para analisis externo del heap del GC.

    Disparo CONDICIONAL, dos formas independientes de activarlo (se puede combinar):

      1) Por CONTADOR de llamadas a gc(0): setGCSnapshotInterval(N) vuelca
         cada N veces que se ejecuta gc(0).

      2) Por EVENTO puntual desde JavaScript (script.js del lado NW.js):
         requestHeapSnapshot() marca que el PROXIMO gc(0) que ocurra debe
         volcarse, sea cual sea el contador. Util para engancharlo a una
         tecla de depuracion o a un evento concreto del juego (p.ej. "cuando
         cambia de nivel", detectado desde JS leyendo el estado del juego,
         o simplemente a mano desde la consola de NW.js).

    El volcado se hace DESPUES de sweep() (heap ya compactado logicamente:
    lo que no esta marcado ha sido liberado/destruido), justo antes de que
    gc(0) retorne, tal y como pedia RIC.

    Se vuelca TODA la memoria lineal de WASM (no solo el area del GC) para
    que el parser Node.js no tenga que resolver relocaciones: cualquier
    puntero embebido en un objeto vivo (vtable, Segment.data, etc.) es
    directamente un OFFSET valido dentro del propio fichero de snapshot,
    porque en Emscripten todo el proceso comparte un unico espacio de
    direcciones de 32 bits.
*/

namespace pxt {

extern uint32_t snapshotSeq;

// Cada cuantas llamadas a gc(0) se vuelca un snapshot. 0 = desactivado (solo por evento).
void setGCSnapshotInterval(uint32_t n);

// Marca que el PROXIMO gc(0) debe volcar un snapshot, independientemente del contador.
void requestHeapSnapshot();

// Si true (por defecto), tras volcar un snapshot se detiene el bucle principal
// (worker_main deja de reprogramarse a si mismo) para poder inspeccionar el
// fichero con la VM congelada en ese instante exacto.
void setHaltAfterSnapshot(bool halt);

// true si ya se ha detenido la ejecucion tras un snapshot.
bool isHeapSnapshotHalted();

void dumpHeapSnapshotToDisk();

// Llamado desde gc.cpp al final de gc(0). Decide si toca volcar y, si toca, lo hace.
void maybeDumpHeapSnapshot();

} // namespace pxt

#endif