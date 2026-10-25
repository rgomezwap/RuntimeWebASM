#include "pxt.h"

#ifndef __EMSCRIPTEN__
	#include "esp_partition.h"
	#include "spi_flash_mmap.h"
#endif

namespace pxt
{


VMImage *vmImg;
#ifndef PXT_VM
	TaskHandle_t userCodeTask;
#endif

// memory mapped address of js_code partition in flash (o del buffer que hace de "flash fake" en el simulador EMSCRIPTEN)
char *romdata_jscode=0;

// tamaño de la rafaga (buffer temporal en RAM) usada para ir leyendo la
// imagen original (solo lectura), parcheandola y volcandola a la nueva
// posicion en flash. Ajustable: mas grande = menos escrituras a flash
// (mas rapido, menos desgaste) a cambio de mas RAM usada temporalmente.
// Debe ser multiplo de 8 y mayor que 8.
#define PXT_PATCH_CHUNK_SIZE		(8 * 512)							// 4Kbytes

#define PXT_ALIGN_UP(x, a)			(((x) + ((a) - 1)) & ~((a) - 1))

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
												uint32_t destOffset)
{
	uint8_t *chunkBuf = (uint8_t *)malloc(PXT_PATCH_CHUNK_SIZE + 8);
	if (!chunkBuf)
		return "no memory for patch chunk buffer";

	auto state = pxt::vm_alloc_patch_state();
	const char *err = nullptr;

	uint32_t srcOff = 0;
	uint32_t dstOff = destOffset;

	while (srcOff < imageSize)
		{

		uint32_t remaining = imageSize - srcOff;
		uint32_t thisLen = (remaining <= PXT_PATCH_CHUNK_SIZE) ? remaining : PXT_PATCH_CHUNK_SIZE;
		uint32_t after = remaining - thisLen;

		// vm_patch_image exige len > 8 y multiplo de 8; si el siguiente
		// trozo quedaria en exactamente 8 bytes (invalido), lo fusionamos
		// con la rafaga actual para evitarlo.
		if (after == 8)
			thisLen += 8;

		memcpy(chunkBuf, srcMapped + srcOff, thisLen);

		err = pxt::vm_patch_image(state, chunkBuf, thisLen);
		if (err)
			break;

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

	return err;
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
	uint32_t eraseLen   = PXT_ALIGN_UP(len, SPI_FLASH_SEC_SIZE);

	if (!jscode_partition || destOffset + eraseLen > jscode_partition->size)
		{
		DMESG("    ERROR: la particion js_code (o el buffer fake) no tiene espacio para la copia parcheada");
		return;
		}
	else
		{
		DMESG("PXT64 binario con %d bytes (%x) , addr. inicio imagen patch %x",realImageSize,realImageSize,destOffset);
		}

	DMESG("Erase Flash start %x offset y %d bytes (%d paginas de 4K)",destOffset,eraseLen,eraseLen/SPI_FLASH_SEC_SIZE);
	esp_err_t eerr = esp_partition_erase_range(jscode_partition, destOffset, eraseLen);
	if (eerr != ESP_OK)
		{
		DMESG("    ERROR esp_partition_erase_range: %d", eerr);
		return;
		}

	const char *err = patchImageStreamingToFlash(jscode_partition, data, len, destOffset);
	if (err)
		{
		DMESG("    patch error: %s", err);
		return;
		}
	else
		{
		DMESG("    aplicado patch a la VImage OK");
		}

	VMImage *img = loadVMImage((void *)(romdata_jscode + destOffset), len);

    if (img->errorCode)
		{
        DMESG("validation error %d at 0x%x", img->errorCode, img->errorOffset);
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
    globals = (TValue *)app_alloc(sizeof(TValue) * getNumGlobals());
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