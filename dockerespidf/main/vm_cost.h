
#ifndef _PXT_VMCOST_H

#define _PXT_VMCOST_H

/*	Simulacion del COSTE DE CPU de la VM del ESP32 (solo EMSCRIPTEN)

	Idea: el navegador ejecuta la VM mucho mas rapido que el ESP32. Para que el
	simulador 'sienta' la misma carga, cada opcode / llamada nativa suma un coste
	(en nanosegundos) a un acumulador global (g_vmCostNs). Cuando el juego pide
	pintar la pantalla (updateScreen), se compara ese coste con el tiempo real que
	ha pasado desde el ultimo frame y, si el ESP32 habria tardado mas, se espera
	lo que falte SIN bloquear el navegador (wakeTime + schedule(), ver screen.cpp).

	El ritmo resultante por frame es: max(tiempo real, coste simulado).

	IMPORTANTE:
	  - Los indices de opcode NO son fijos: dependen de cada imagen (seccion
	    OpCodeMap). Por eso la tabla va por NOMBRE (el de pointers.cpp, p.ej.
	    "pxt::op_ldloc" o "ImageMethods::fill") y se traduce a un array por
	    indice UNA sola vez al cargar la imagen (verify.cpp -> opcodeCostNs[]).
	  - Los valores de la tabla de abajo son un EJEMPLO, NO estan medidos.
	    Para calibrar: en el ESP32 real mide con XTHAL_GET_CCOUNT() cuanto tarda
	    cada opcode/llamada y pon aqui el resultado (ns = ciclos * 1000 / 240).
	  - En ESP32 todo esto desaparece (macros vacias, cero coste en el bucle).

	Se activa con VM_COST_SIM en pxtconfig.h. VM_COST_SCALE_PCT escala toda la
	tabla (100 = tal cual, 50 = ESP32 'virtual' el doble de rapido, 200 = el doble
	de lento), util para ajustar sin tocar la tabla.
*/

#include <stdint.h>
#include <string.h>

#include "pxt.h"

#if defined(__EMSCRIPTEN__) && defined(VM_COST_SIM)

	#define VM_COST_ENABLED				1

	#ifndef VM_COST_SCALE_PCT
		#define VM_COST_SCALE_PCT		100
	#endif
	
	/*	COSTE POR UNIDAD DE TRABAJO de las funciones de dibujo (image.cpp), en nanosegundos
	
		Se suman con VM_COST_N(cantidad, VMC_xxx) desde dentro de cada funcion, una vez calculado el
		trabajo REAL (ya recortado a la pantalla): coste = cantidad * constante. Se suma ademas al coste
		FIJO de la llamada que hay en la tabla de vmcost_lookup().
	
		EJEMPLO, NO MEDIDO. Entre parentesis una estimacion en ciclos a 240 MHz (1 ciclo = 4,17 ns).
		Para calibrar: mide cada funcion en el ESP32 real con XTHAL_GET_CCOUNT() sobre un tamaño conocido
		y deja aqui (ciclos * 1000 / 240) / cantidad.
	*/
	#define VMC_MEMSET_PER_BYTE				2						// fill(), scroll()                  (~0,5 ciclo/byte)
	#define VMC_MEMCPY_PER_BYTE				3						// copyFrom(), clone(), scroll()     (~0,75 ciclo/byte)
	#define VMC_FLIPX_PER_BYTE				5						// flipX(): 3 memcpy por cada par de columnas
	#define VMC_DOUBLE_PER_BYTE				5						// doubledX(), doubledY(): por byte de la imagen resultante
	#define VMC_REPLACE_PER_BYTE			6						// replace()
	#define VMC_FLIPY_PER_PIXEL				90						// flipY(): 2 getCore + 2 setCore por pareja de pixeles
	#define VMC_PIXELWISE_PER_PIXEL			100						// transposed(), replace() con padding: getCore + setCore (~24 ciclos)
				
	#define VMC_FILLRECT_PER_COL			40						// fillRect(), mapRect(): bucle por columna       (~10 ciclos)
	#define VMC_FILLRECT_PER_PIXEL			6						// fillRect()                                      (~1,5 ciclos/pixel)
	#define VMC_MAPRECT_PER_PIXEL			25						// mapRect(): consulta de tabla + escritura        (~6 ciclos/pixel)
				
	#define VMC_DRAWIMG_PER_COL				40						// drawImageCore(): por columna visible
	#define VMC_DRAWIMG_OPAQUE_PER_PIXEL	8						// 4bpp -> 4bpp opaco (drawImage)                  (~2 ciclos/pixel)
	#define VMC_DRAWIMG_TRANSP_PER_PIXEL	18						// 4bpp -> 4bpp con transparencia / overlapsWith   (~4,5 ciclos/pixel)
	#define VMC_DRAWIMG_MONO_PER_PIXEL		4						// 1bpp -> 1bpp                                    (~1 ciclo/pixel)
	#define VMC_DRAWIMG_ICON_PER_PIXEL		35						// 1bpp -> 4bpp (iconos)                           (~8 ciclos/pixel)
				
	#define VMC_LINE_PER_STEP				30						// drawLine(): un paso de Bresenham con setCore    (~7 ciclos)
	#define VMC_BLITROW_PER_PIXEL			25						// blitRow()                                       (~6 ciclos)
	#define VMC_BLIT_PER_PIXEL				50						// blit(): getCore + setCore                       (~12 ciclos)
				
	#define VMC_VLINE_PER_CALL				30						// drawVLineCore(): una linea vertical (triangulos/poligonos)
	#define VMC_VLINE_PER_PIXEL				6						// drawVLineCore()
	#define VMC_POLY_PER_COLUMN				150						// fillTriangle()/fillPolygon4(): avance de los generadores de bordes por columna X
				
	#define VMC_ROTATED_PER_PIXEL			1200					// _drawScaledRotatedImage(): por pixel ORIGEN. (~290 ciclos: 2 divisiones de 64 bits
																	// por software + shear + getPixel/setPixel). EL MAS INCIERTO: mide este primero
	#define VMC_ROTATED2_PER_PIXEL			1800					// _checkOverlapsTwoScaledRotatedImages(): ademas deshace el shear y 2 divisiones mas
	
	
	namespace pxt
		{
		// coste acumulado (ns) desde el ultimo updateScreen(), definida en vm.cpp
		extern uint64_t g_vmCostNs;
		}

	// suma el coste del opcode 'idx' (mismo indice que img->opcodes[])
	#define VM_COST_ADD(costArr, idx)	(::pxt::g_vmCostNs += (costArr)[(idx)])

	// para sumar a mano un coste extra desde una funcion nativa cuyo tiempo depende de sus argumentos
	// (p.ej. drawImage segun el tamaño del sprite)
	#define VM_COST_ADD_NS(ns)			(::pxt::g_vmCostNs += (uint64_t)(ns))

	// coste = cantidad * ns por unidad (con VM_COST_SCALE_PCT aplicado). Una cantidad negativa o cero no suma nada
	inline uint64_t vmcost_n(int64_t n, uint32_t perUnitNs)
	{
		return (n > 0) ? (((uint64_t)n * perUnitNs * VM_COST_SCALE_PCT) / 100) : 0;
	}
	
	#define VM_COST_N(n, perUnitNs)		VM_COST_ADD_NS(::vmcost_n((int64_t)(n), (uint32_t)(perUnitNs)))
	
	struct VmCostEntry
		{
		const char *name;
		uint32_t    ns;
		};

	/*	Devuelve el coste en ns de un opcode a partir de su nombre.
		Orden de busqueda: nombre exacto -> prefijo -> valor por defecto.
	*/
	inline uint32_t vmcost_lookup(const char *name, bool isBaseOpcode)
	{

		// ---- NOMBRE EXACTO (EJEMPLO, no medido) ----
		static const VmCostEntry exact[] =
			{
			// opcodes base de la VM
			{ "pxt::op_ldloc",					250 },
			{ "pxt::op_stloc",					250 },
			{ "pxt::op_ldcap",					300 },
			{ "pxt::op_ldglb",					300 },
			{ "pxt::op_stglb",					300 },
			{ "pxt::op_ldlit",					250 },
			{ "pxt::op_ldnumber",				300 },
			{ "pxt::op_ldint",					200 },
			{ "pxt::op_ldintneg",				200 },
			{ "pxt::op_ldspecial",				200 },
			{ "pxt::op_push",					150 },
			{ "pxt::op_pop",					150 },
			{ "pxt::op_popmany",				200 },
			{ "pxt::op_pushmany",				200 },
			{ "pxt::op_bitconv",				300 },
			{ "pxt::op_jmp",					250 },
			{ "pxt::op_jmpz",					300 },
			{ "pxt::op_jmpnz",					300 },
			{ "pxt::op_ldfld",					450 },
			{ "pxt::op_stfld",					450 },
			{ "pxt::op_newobj",					4000 },		// reserva de memoria (GC)
			{ "pxt::op_callproc",				1500 },
			{ "pxt::op_callind",				2000 },
			{ "pxt::op_ret",					1000 },
			{ "pxt::op_try",					1500 },
			{ "pxt::op_mapget",					3000 },
			{ "pxt::op_mapset",					3000 },
			{ "pxt::op_checkinst",				800 },
			{ "pxt::op_calliface",				1200 },
			{ "pxt::op_callget",				1200 },
			{ "pxt::op_callset",				1200 },

			// IMPORTANTE: updateScreen cuesta 0 aqui a proposito. Su llamada se re-ejecuta tras
			// cada espera (schedule()), asi que sumarle coste aqui se contaria varias veces.
			{ "pxt::updateScreen",				0 },
			
			/*	dibujo de imagenes. OJO: aqui va SOLO el coste FIJO de la llamada (despachar el shim, convertir
				argumentos, reservar memoria...). La parte que depende del tamaño se suma desde image.cpp con
				VM_COST_N(...) usando las constantes VMC_* de arriba, asi que NO se debe repetir aqui.
			*/
			{ "ImageMethods::setPixel",			500 },								// fijo, no depende del tamaño
			{ "ImageMethods::getPixel",			400 },						
			{ "ImageMethods::fill",				400 },								// + VMC_MEMSET_PER_BYTE * bytes
			{ "ImageMethods::_fillRect",		500 },								// + columnas y pixeles
			{ "ImageMethods::_mapRect",			500 },								// + columnas y pixeles
			{ "ImageMethods::_drawLine",		500 },								// + pasos de Bresenham (o fillRect si es recta)
			{ "ImageMethods::_blitRow",			400 },								// + pixeles
			{ "ImageMethods::_blit",			1500 },								// 10 argumentos de una RefCollection; + pixeles
			{ "ImageMethods::_drawIcon",		3000 },								// convertAndWrap() reserva un objeto; + drawImageCore
			{ "ImageMethods::drawImage",		500 },								// + columnas y pixeles (drawImageCore)
			{ "ImageMethods::drawTransparentImage",	500 },							// + columnas y pixeles (drawImageCore)
			{ "ImageMethods::overlapsWith",		500 },								// + columnas y pixeles (drawImageCore, cota superior)
			{ "ImageMethods::_drawScaledRotatedImage",	30000 },					// parseShearArgs(): fmod/tan/sin en DOUBLE (por software en ESP32); + pixeles
			{ "ImageMethods::_checkOverlapsScaledRotatedImage",	30000 },
			{ "ImageMethods::_checkOverlapsTwoScaledRotatedImages",	60000 },		// 2 x parseShearArgs()
			{ "ImageMethods::_fillCircle",		500 },								// + los fillRect() internos
			{ "ImageMethods::_fillTriangle",	1500 },								// 7 argumentos; + columnas y vlines
			{ "ImageMethods::_fillPolygon4",	2000 },								// 9 argumentos; + columnas y vlines
			{ "ImageMethods::scroll",			500 },								// + memmove/memset, o clone+fill+drawImage si hay dy
			{ "ImageMethods::copyFrom",			300 },								// + VMC_MEMCPY_PER_BYTE * bytes
			{ "ImageMethods::clone",			3000 },								// reserva de memoria (GC); + VMC_MEMCPY_PER_BYTE * bytes
			{ "ImageMethods::flipX",			300 },						
			{ "ImageMethods::flipY",			300 },						
			{ "ImageMethods::replace",			300 },						
			{ "ImageMethods::transposed",		3000 },								// reserva de memoria; + pixeles
			{ "ImageMethods::doubledX",			3000 },								// reserva de memoria; + bytes
			{ "ImageMethods::doubledY",			3000 },						
			{ "ImageMethods::doubled",			6000 },								// doubledX + doubledY, que ya suman lo suyo
			
			{ NULL, 0 }
			};

		// ---- POR PREFIJO (EJEMPLO, no medido) ----
		static const VmCostEntry prefix[] =
			{
			{ "numops::",						600 },
			{ "Math_::",						3000 },
			{ "Array_::",						700 },
			{ "String_::",						1500 },
			{ "BufferMethods::",				1000 },
			{ "ImageMethods::",					3000 },
			{ "image::",						8000 },
			{ "control::",						1500 },
			{ "pxt::op_",						300 },			// opcodes base que no esten en la tabla exacta (debe ir ANTES de "pxt::")
			{ "pxt::",							1500 },
			{ "pxtrt::",						1500 },
			{ "music::",						2000 },
			{ "pins::",							1500 },
			{ "DigitalInOutPinMethods::",		1500 },

			{ NULL, 0 }
			};

		uint32_t ns = isBaseOpcode ? 300 : 2000;				// valor por defecto: opcode base / llamada nativa
		bool found = false;

		for (const VmCostEntry *e = exact; e->name; ++e)
			{
			if (strcmp(e->name, name) == 0)
				{
				ns = e->ns;
				found = true;
				break;
				}
			}

		if (!found)
			{
			for (const VmCostEntry *e = prefix; e->name; ++e)
				{
				if (strncmp(e->name, name, strlen(e->name)) == 0)
					{
					ns = e->ns;
					break;
					}
				}
			}

		return (uint32_t)(((uint64_t)ns * VM_COST_SCALE_PCT) / 100);
	}

#else
	
	// ESP32 (o simulacion desactivada): no hace NADA, no añade ni una instruccion al bucle de la VM
	#define VM_COST_ADD(costArr, idx)	do { } while (0)
	#define VM_COST_ADD_NS(ns)			do { } while (0)	
	#define VM_COST_N(n, perUnitNs)		do { } while (0)			// ni siquiera evalua 'n'
	
#endif

#endif

