#include "pxt.h"

#ifndef __EMSCRIPTEN__
	#include "esp_partition.h"
	#include "spi_flash_mmap.h"
	
	#include "esp_rom_crc.h"
	#include "esp_app_desc.h"
#endif


/*	Sistema de CRC en la imagenes para ver si son validas con el Binario
*/

#define PXT_PATCH_MARK_MAGIC			0x504d524bUL		// 'KRMP'
#define PXT_PATCH_MARK_VERSION			1

typedef struct
	{
	uint32_t magic;
	uint32_t version;
	uint32_t srcLen;					// len de la imagen original (hd->imageSize)
	uint32_t realImageSize;
	uint32_t destOffset;
	uint32_t srcCrc;					// CRC32 de los 'len' bytes originales (sin parchear)
	uint32_t dstCrc;					// CRC32 de los 'len' bytes ya parcheados
	uint8_t  appId[16];					// sha256 del ELF truncado: identifica el binario C++
	uint32_t markCrc;					// CRC32 de todo lo anterior
	} pxt_patch_mark_t;

// memory mapped address of js_code partition in flash (o del buffer que hace de "flash fake" en el simulador EMSCRIPTEN)
char *romdata_jscode=0;

static uint32_t pxtCrc32(uint32_t crc, const uint8_t *p, uint32_t len)
{
	
#ifdef __EMSCRIPTEN__

	crc = ~crc;
	while (len--)
		{
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
		}
	return ~crc;
	
#else
	
	return esp_rom_crc32_le(crc, p, len);		// misma convencion -> encadenable
	
#endif
	
}

static void pxtGetAppId(uint8_t out[16])
{
	
#ifdef __EMSCRIPTEN__
	
	memset(out, 0, 16);
	const char *bid = __DATE__ __TIME__;
	uint32_t c = pxtCrc32(0, (const uint8_t *)bid, strlen(bid));
	memcpy(out, &c, 4);
	
#else
	
	const esp_app_desc_t *d = esp_app_get_description();
	memcpy(out, d->app_elf_sha256, 16);
	
#endif

}	

static bool patchedCopyIsValid(const uint8_t *src, uint32_t len,uint32_t realImageSize, uint32_t destOffset, uint32_t markOffset)
{
	// leemos la informacion que guardamos en la Flash de la validacion de imagenes
	pxt_patch_mark_t mk;
	memcpy(&mk, (const uint8_t *)romdata_jscode + markOffset, sizeof(mk));

	if (mk.magic != PXT_PATCH_MARK_MAGIC || mk.version != PXT_PATCH_MARK_VERSION)					return false;
	if (pxtCrc32(0, (const uint8_t *)&mk, sizeof(mk) - 4) != mk.markCrc)							return false;
	if (mk.srcLen != len || mk.realImageSize != realImageSize || mk.destOffset != destOffset)		return false;

	// Validacion del binario
	uint8_t appId[16];
	pxtGetAppId(appId);
	if (memcmp(appId, mk.appId, sizeof(appId)) != 0)
		{
		DMESG("    marca de otro binario C++ -> hay que reparchear");
		return false;
		}

	// Validacion de la imagen original sin patch
	if (pxtCrc32(0, src, len) != mk.srcCrc)
		{
		DMESG("    la imagen original ha cambiado -> hay que reparchear");
		return false;
		}

	// Validacion de la imagen con patch actual que hay en Flash
	if (pxtCrc32(0, (const uint8_t *)romdata_jscode + destOffset, len) != mk.dstCrc)
		{
		DMESG("    la copia parcheada esta dañada -> hay que reparchear");
		return false;
		}
	
	return true;
}

// RIC era originario de verify.cpp, pero aqui lo reaprovecho
#define ALIGNED(sz) (((sz)&7) == 0)

namespace pxt
{


VMImage *vmImg;
#ifndef PXT_VM
	TaskHandle_t userCodeTask;
#endif

// tamaño de la rafaga (buffer temporal en RAM) usada para ir leyendo la
// imagen original (solo lectura), parcheandola y volcandola a la nueva
// posicion en flash. Ajustable: mas grande = menos escrituras a flash
// (mas rapido, menos desgaste) a cambio de mas RAM usada temporalmente.
// Debe ser multiplo de 8 y mayor que 8.
#define PXT_PATCH_CHUNK_SIZE		(8 * 512)							// 4Kbytes

#define PXT_ALIGN_UP(x, a)			(((x) + ((a) - 1)) & ~((a) - 1))

// para EMSCRIPTEN hacemos un "Flash fake"
#ifdef __EMSCRIPTEN__

	// ---------------------------------------------------------------------
	// "Flash fake" para el simulador.
	//
	// En ESP32 la imagen original vive en una particion de flash mapeada en
	// solo lectura, y el parcheo en streaming escribe la copia parcheada en
	// otra zona de esa misma particion via esp_partition_erase_range() /
	// esp_partition_write(). Para poder compartir literalmente ese mismo
	// codigo (computeRealImageSize / patchImageStreamingToFlash) en
	// EMSCRIPTEN, emulamos aqui una "particion" respaldada por un buffer
	// normal de RAM: 'romdata_jscode' hace de direccion base mapeada, y las
	// funciones esp_partition_* de abajo simplemente memset/memcpy sobre ese
	// buffer en vez de tocar flash de verdad.

	typedef int esp_err_t;
	#define ESP_OK 0

	// mismo valor que SPI_FLASH_SEC_SIZE en ESP-IDF (tamaño de sector de flash)
	#define SPI_FLASH_SEC_SIZE 4096

	struct esp_partition_t
	{
		uint32_t size; // tamaño total del buffer fake, en bytes
	};

	static esp_partition_t fakePartition;
	static const esp_partition_t *jscode_partition = &fakePartition;

	// esto esta en otro fichero
	extern unsigned pxt_file_buffer_len;

	static esp_err_t esp_partition_erase_range(const esp_partition_t *part, uint32_t offset, uint32_t size)
	{
		memset(romdata_jscode + offset, 0xFF, size); // 0xFF = "flash borrada"
		return ESP_OK;
	}

	static esp_err_t esp_partition_write(const esp_partition_t *part, uint32_t offset, const void *data, uint32_t size)
	{
		memcpy(romdata_jscode + offset, data, size);
		return ESP_OK;
	}

#else

	// particion js_code, guardada aqui para poder usarla desde vmStartCore()
	static const esp_partition_t *jscode_partition = nullptr;

#endif

// solo para definir la funcion que esta en otro fichero
#ifdef __EMSCRIPTEN__
	void dumpPXTExportDataAndvmImg();
	extern uint8_t *pxt_file_buffer;
#endif

// magic de 12 bytes fijos que abre el bloque de source map:
// "SRCM" + 8 bytes fijos (ver encodeSourceMap en backvm.ts)
static const uint8_t SRCM_MAGIC[12] = { 0x53,0x52,0x43,0x4d, 0x88,0x15,0x4e,0x2d, 0xaa,0x86,0x99,0x71 };
#define SRCM_MAGIC_LEN 12

// firma (4) + 8 bytes fijos (ver encodeSourceMap en backvm.ts) + 4 reservados/0
#define SRCM_HEADER_LEN 16

// Busca la primera aparicion de SRCM_MAGIC en [searchFrom, limit), al
// estilo de buf.indexOf(SRCM_MAGIC). Puede haber (o no) un hueco entre el
// final de las secciones y el inicio del source map, asi que no asumimos
// contiguidad: buscamos la firma en vez de leerla directamente en
// 'base + imageSize'. Devuelve NULL si no se encuentra dentro del rango.
static const uint8_t *findSourceMap(const uint8_t *searchFrom, const uint8_t *limit)
{
	if (searchFrom + SRCM_MAGIC_LEN > limit)
		return nullptr;

	const uint8_t *searchLimit = limit - SRCM_MAGIC_LEN; // ultima posicion valida para iniciar la comparacion
	const uint8_t *p = searchFrom;

	while (p <= searchLimit)
		{

		// memchr del primer byte para no comparar los 12 bytes en cada posicion
		const uint8_t *hit = (const uint8_t *)memchr(p, SRCM_MAGIC[0], (size_t)(searchLimit - p) + 1);
		if (!hit)
			return nullptr;

		if (memcmp(hit, SRCM_MAGIC, SRCM_MAGIC_LEN) == 0)
			return hit;

		p = hit + 1;

		}

	return nullptr;
}

// Devuelve cuantos bytes ocupa un varint tipo 'decode_num' de vm.cpp en 'p'.
// No necesitamos el valor decodificado aqui, solo saber cuanto avanzar.
static inline uint32_t srcmVarintLen(const uint8_t *p)
{
	uint8_t v = p[0];
	if (v < 0xf0)
		return 1;
	return 1 + (v & 0x07);
}

/*	Tras el bloque de secciones (a partir de offset = imageSize del
	InfoHeader, permitiendo que haya un hueco/padding en medio) puede venir
	el source map: firma de SRCM_HEADER_LEN bytes, seguida de una lista de
	ficheros fuente (nombre \0-terminado + triples en varint + 0xFF de fin
	de fichero), terminada con un 0x00 extra, y con un byte de relleno mas
	si la longitud resultante es impar.

	Devuelve:
	  - el tamaño TOTAL real de la imagen en bytes (secciones + hueco si lo
	    hay + source map + relleno), si se encuentra y parsea la firma
	    correctamente.
	  - 'imageSize' tal cual, si no se encuentra la firma (no hay source map).
	  - 0, si se encuentra la firma pero el source map parece corrupto (no
	    cierra dentro del limite de la particion mapeada) - en ese caso el
	    llamante NO debe seguir.

	'maxScan' es el limite absoluto de bytes desde 'base' hasta el que esta
	permitido leer (normalmente el tamaño de la particion mapeada), para no
	leer nunca fuera de la region mmap'd ni quedarnos en un bucle infinito
	sobre flash borrada (0xFF) si el formato viniera corrupto.
*/
static uint32_t computeRealImageSize(const uint8_t *base, uint32_t imageSize, uint32_t maxScan)
{
	const uint8_t *searchFrom = base + imageSize;
	const uint8_t *limit	  = base + maxScan;

	const uint8_t *magicPos = findSourceMap(searchFrom, limit);
	if (!magicPos)
		return imageSize; // no encontramos la firma 'SRCM...': no hay source map

	const uint8_t *p = magicPos + SRCM_HEADER_LEN;

	for (;;)
		{

		if (p >= limit)
			{
			DMESG("computeRealImageSize(): source map no termina dentro del limite de escaneo");
			return 0;
			}

		if (*p == 0x00)
			{
			p += 1; // fin de toda la lista de ficheros
			break;
			}

		// nombre de fichero, \0-terminado
		while (p < limit && *p != 0x00)
			p++;
		if (p >= limit)
			{
			DMESG("computeRealImageSize(): nombre de fichero sin terminar en el source map");
			return 0;
			}
		p++; // saltamos el \0 del nombre

		// triples (deltaLinea, deltaOffset>>1, longitud>>1) hasta 0xFF
		while (p < limit && *p != 0xFF)
			{
			p += srcmVarintLen(p); // deltaLinea
			if (p >= limit) break;
			p += srcmVarintLen(p); // deltaOffset>>1
			if (p >= limit) break;
			p += srcmVarintLen(p); // longitud>>1
			}
		if (p >= limit || *p != 0xFF)
			{
			DMESG("computeRealImageSize(): triples de un fichero sin terminador 0xFF en el source map");
			return 0;
			}
		p++; // saltamos el 0xFF de fin de fichero

		}

	// si la longitud del source map (desde el magic) resulta impar, hay un byte de relleno mas
	uint32_t sourceMapLen = (uint32_t)(p - magicPos);
	if (sourceMapLen & 1)
		p++;

	return (uint32_t)(p - base);
}

/*	Recorre cabeceras de sección desde 'srcMapped + srcOff' (que es memoria
	legible de punta a punta, tanto en el mmap real de ESP32 como en el
	buffer fake de Emscripten) acumulando secciones ENTERAS sin pasarse de
	'targetLen'. Garantiza que el corte del chunk caiga siempre justo al
	final de una sección, nunca a la mitad.

	Esto es necesario porque, a diferencia de Literal/Function/VTable (que
	se parchean con el mecanismo genérico de 8 en 8 bytes, tolerante a que
	una sección quede repartida entre dos llamadas), NumberBoxes se procesa
	entero de una sola vez dentro de vm_patch_image() y necesita tener toda
	la sección disponible en el buffer que se le pasa.
*/
static uint32_t computeAlignedChunkLen(const uint8_t *srcMapped, uint32_t srcOff,
										uint32_t remaining, uint32_t targetLen)
{
	uint32_t off = 0;

	while (off < remaining)
		{
		VMImageSection sect;
		memcpy(&sect, srcMapped + srcOff + off, sizeof(sect));

		uint32_t nextOff = off + sect.size;

		// si ya tenemos al menos una sección completa y añadir la siguiente
		// se pasaría del objetivo, cortamos aquí
		if (off > 0 && nextOff > targetLen)
			break;

		off = nextOff;

		if (off >= targetLen)
			break;
		}

	return off; // siempre múltiplo de 8 y en límite de sección
}

/*	Parchea la imagen leyendo de 'srcMapped' (mmap de solo lectura de la
	particion, donde esta la imagen ORIGINAL sin parchear) en rafagas de
	PXT_PATCH_CHUNK_SIZE bytes, aplicando vm_patch_image() sobre un buffer
	temporal en RAM (ese si escribible), y volcando cada rafaga ya parcheada
	a 'destOffset' dentro de la misma particion, justo despues de donde
	acaba la imagen original.

	El estado del patch (VMPatchState) se mantiene entre rafagas, asi que
	no importa que una seccion de la imagen quede a caballo entre dos
	rafagas: se sigue parcheando correctamente.

	Devuelve NULL si OK, o un mensaje de error si algo fallo.
*/
static const char *patchImageStreamingToFlash(const esp_partition_t *part,
											  const uint8_t *srcMapped,
											  uint32_t imageSize,
											  uint32_t destOffset,
											  uint32_t *outSrcCrc,
											  uint32_t *outDstCrc)												
{
	uint32_t chunkBufSize = PXT_PATCH_CHUNK_SIZE + 8;
	uint8_t *chunkBuf = (uint8_t *)malloc(chunkBufSize);
	if (!chunkBuf)
		return "no memory for patch chunk buffer";

	auto state = pxt::vm_alloc_patch_state();
	const char *err = nullptr;

	uint32_t srcOff = 0;
	uint32_t dstOff = destOffset;

	// CRC32 - como se van calculando dentro del bucle, los CRC32 se van encadenando y da el final ...
	uint32_t srcCrc = 0, dstCrc = 0;
	
	while (srcOff < imageSize)
		{

		uint32_t remaining = imageSize - srcOff;
		uint32_t targetLen = (remaining <= PXT_PATCH_CHUNK_SIZE) ? remaining : PXT_PATCH_CHUNK_SIZE;

		// cortamos siempre en límite de sección, nunca a medias
		uint32_t thisLen = computeAlignedChunkLen(srcMapped, srcOff, remaining, targetLen);

		if (thisLen == 0 || !ALIGNED(thisLen))
			{
			err = "invalid section length while chunking";
			break;
			}

		// evitamos dejar un último trozo de exactamente 8 bytes (invalido para vm_patch_image)
		if (remaining - thisLen == 8)
			thisLen = remaining;

		// si una sola sección (p.ej. un NumberBoxes muy grande) no cabe en el
		// buffer actual, lo ampliamos en tiempo de ejecución
		if (thisLen + 8 > chunkBufSize)
			{
			uint8_t *grown = (uint8_t *)realloc(chunkBuf, thisLen + 8);
			if (!grown)
				{
				err = "no memory to grow patch chunk buffer";
				break;
				}
			chunkBuf = grown;
			chunkBufSize = thisLen + 8;

			// RIC DEBUG
			DMESG("    chunk buffer ampliado a %d bytes (seccion grande)", chunkBufSize);
			}

		memcpy(chunkBuf, srcMapped + srcOff, thisLen);
		
		// CRC32 antes
		srcCrc = pxtCrc32(srcCrc, chunkBuf, thisLen);		// antes del patch

		err = pxt::vm_patch_image(state, chunkBuf, thisLen);
		if (err) break;
		
		// CRC32 despues
		dstCrc = pxtCrc32(dstCrc, chunkBuf, thisLen);		// despues del patch
		
		esp_err_t werr = esp_partition_write(part, dstOff, chunkBuf, thisLen);
		if (werr != ESP_OK)
			{
			DMESG("    esp_partition_write failed: %d", werr);
			err = "esp_partition_write failed";
			break;
			}

		srcOff += thisLen;
		dstOff += thisLen;

		}

	pxt::vm_finish_patch(state);
	free(chunkBuf);
	
	// CRC32
	*outSrcCrc = srcCrc;
	*outDstCrc = dstCrc;
	
	return err;
}

// RIC DEBUG: variante de app_alloc() que loguea con el mismo formato que
// xmallocNamed() en verify.cpp, pero para memoria del heap del GC (no de
// xmalloc/RAM fija). app_alloc() marca el bloque como PERMA_MASK, asi que
// esta memoria vive hasta que se descarga la imagen (unloadVMImage / nueva
// carga), igual que las tablas de verify.cpp. Sirve para ver cuanta RAM del
// heap del GC se reserva ya en el arranque, antes de que el programa del
// usuario empiece a pedir memoria por su cuenta.
static void *appAllocNamed(const char *label, uint32_t count, uint32_t elemSize)
{
	uint32_t bytes = elemSize * count;

	DMESG("    [MEM-GC]  %s: %d elementos x %d bytes = %d bytes (heap del GC, PERMA)",label, count, elemSize, bytes);

	return app_alloc(bytes);
}

// RIC
static void vmStartCore(uint8_t *data, unsigned len)
{
	
	unloadVMImage(vmImg);
    vmImg = NULL;
	
	// RIC no se usa
	#ifndef PXT_VM
		userCodeTask = xTaskGetCurrentTaskHandle();
	#endif

    gcPreStartup();
	
	
	/*	una vez leido en memoria lo tenemos que parchear

			reescribe los primeros bytes (cabeceras) de ciertas secciones con las direcciones
			de memoria de las tablas VTable y métodos nativos del propio binario C++ compilado
		
	*/
	
	uint32_t realImageSize = computeRealImageSize(data, len, jscode_partition->size);
	if (realImageSize == 0)
		{
		DMESG("    ERROR: no se pudo determinar el tamaño real de la imagen (source map corrupto)");
		return;
		}

	uint32_t destOffset = PXT_ALIGN_UP(realImageSize, SPI_FLASH_SEC_SIZE);
	uint32_t copyLen    = PXT_ALIGN_UP(len, SPI_FLASH_SEC_SIZE);
	uint32_t markOffset = destOffset + copyLen;
	uint32_t eraseLen   = copyLen + SPI_FLASH_SEC_SIZE;					// copia + sector de marca
	
	if (!jscode_partition || destOffset + eraseLen > jscode_partition->size)
		{
		DMESG("    ERROR: la particion js_code (o el buffer fake) no tiene espacio para la copia parcheada");
		return;
		}
	else
		{
		DMESG("PXT64 binario con %d bytes (%x) , addr. inicio imagen patch %x",realImageSize,realImageSize,destOffset);
		}
	
	bool needPatch = !patchedCopyIsValid(data, len, realImageSize, destOffset, markOffset);
	
	if (!needPatch)
		{
		
		DMESG("PXT64 copia parcheada ya presente en Flash");
		
		}
	else
		{
		
		DMESG("Erase Flash start %x offset y %d bytes (%d paginas de 4K)",destOffset,eraseLen,eraseLen/SPI_FLASH_SEC_SIZE);
		esp_err_t eerr = esp_partition_erase_range(jscode_partition, destOffset, eraseLen);		
		if (eerr != ESP_OK)
			{
			DMESG("    ERROR esp_partition_erase_range: %d", eerr);
			return;
			}
			
		uint32_t srcCrc = 0, dstCrc = 0;
		const char *err = patchImageStreamingToFlash(jscode_partition, data, len, destOffset, &srcCrc, &dstCrc);
		if (err)
			{
			DMESG("    patch error: %s", err);
			return;
			}
		else
			{
			DMESG("    aplicado patch a la VImage OK");
			}
		
		// la marca SIEMPRE lo ultimo: si se corta la luz a medio patch, no hay marca valida
		pxt_patch_mark_t mk;
		memset(&mk, 0, sizeof(mk));
		mk.magic         = PXT_PATCH_MARK_MAGIC;
		mk.version       = PXT_PATCH_MARK_VERSION;
		mk.srcLen        = len;
		mk.realImageSize = realImageSize;
		mk.destOffset    = destOffset;
		mk.srcCrc        = srcCrc;
		mk.dstCrc        = dstCrc;
		pxtGetAppId(mk.appId);
		mk.markCrc       = pxtCrc32(0, (const uint8_t *)&mk, sizeof(mk) - 4);

		// guardamos en este momento en la flash, los CRC de el binario, imagen original e imagen patch, ara la proxima ejecucion
		esp_partition_write(jscode_partition, markOffset, &mk, sizeof(mk));
		
		}
	
	VMImage *img = loadVMImage((void *)(romdata_jscode + destOffset), len);

    if (img->errorCode)
		{
        DMESG("validation error %d at 0x%x", img->errorCode, img->errorOffset);
		target_panic(PANIC_VM_ERROR);
        return;
		}
	else
		{
        DMESG("      image validate OK");
		DMESG("");
		}
    vmImg = img;
	
	// RIC DEBUG de PXTExportData
	#ifdef __EMSCRIPTEN__
		DMESG("");
		pxt::dumpPXTExportDataAndvmImg();
		DMESG("");
	#endif

    gcStartup();

		// RIC DEBUG
		DMESG("Inicializamos memoria para globals %d bytes",sizeof(TValue) * getNumGlobals());
	globals = (TValue *)appAllocNamed("globals", getNumGlobals(), sizeof(TValue));
    memset(globals, 0, sizeof(TValue) * getNumGlobals());
	
    initRuntime();
	
	DMESG("    END vmStartCore()");
}

// RIC
void vmStart()
{
	DMESG("START vmStart()");
	
	memInfo();
    
	install_gpio0_handler();
	
	/*	Inicio de la imagen y comprobacion basica de integridad
	*/
	
	#ifdef __EMSCRIPTEN__
		
		// Preparamos la "flash fake": copiamos el fichero ya cargado en
		// pxt_file_buffer a un buffer propio que hace de particion simulada
		// (romdata_jscode / jscode_partition), para poder compartir el mismo
		// codigo de parcheo en streaming que usa el build de ESP32.
		
		uint32_t fileLen	  = pxt_file_buffer_len;
		uint32_t fakeFlashSize = PXT_ALIGN_UP(fileLen, SPI_FLASH_SEC_SIZE) * 2 + SPI_FLASH_SEC_SIZE;

		romdata_jscode = (char *)malloc(fakeFlashSize);
		memset(romdata_jscode, 0xFF, fakeFlashSize); // como flash recien borrada
		memcpy(romdata_jscode, pxt_file_buffer, fileLen);

		fakePartition.size = fakeFlashSize;

		DMESG("CRC_boot: fake flash simulada [ %x - %x ]",romdata_jscode,romdata_jscode + fakeFlashSize - 1);
		DMESG("CRC_boot: fake flash size %d bytes",fakeFlashSize);

		auto sect0	= (VMImageSection *)romdata_jscode;
		
	#else		
		
		jscode_partition= esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"js_code");
		spi_flash_mmap_handle_t hrom;
		esp_err_t err = esp_partition_mmap(jscode_partition,0,jscode_partition->size,ESP_PARTITION_MMAP_DATA,(const void **)&romdata_jscode,&hrom);
		DMESG("CRC_boot: storage mapped address space [ 0x%x - 0x%x ]",romdata_jscode,romdata_jscode + jscode_partition->size - 1);
		DMESG("CRC_boot: storage size %d bytes",jscode_partition->size);
		
		auto sect0	= (VMImageSection *)romdata_jscode;
		
	#endif
    
	auto hd		= (VMImageHeader *)sect0->data;
    
	if (sect0->type != SectionType::InfoHeader || hd->magic0 != VM_MAGIC0 || hd->imageSize < 256)
		{
        DMESG("vmStart() invalid image");
		}
	else
		{
		DMESG("vmStart() valid image with %d bytes in sections",hd->imageSize);
	
		vmStartCore((uint8_t *)sect0,hd->imageSize);
		}
	
	DMESG("END vmStart()");
}


} // namespace pxt
