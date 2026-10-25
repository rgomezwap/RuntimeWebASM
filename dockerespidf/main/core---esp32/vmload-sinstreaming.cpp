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

// memory mapped address of js_code partition in flash.
char *romdata_jscode=0;

// solo para definir la funcion que esta en otro fichero
#ifdef __EMSCRIPTEN__
	void dumpPXTExportDataAndvmImg();
	extern uint8_t *pxt_file_buffer;
#endif

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
	auto state = pxt::vm_alloc_patch_state();
	const char *err = pxt::vm_patch_image(state,data,len);
	if (err)
		{
		DMESG("    patch error: %s", err);
		}
	else
		{
		DMESG("    aplicado patch a la VImage OK !");
		}
	pxt::vm_finish_patch(state);
	
	
    auto img = loadVMImage(data, len);
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
		
		auto sect0	= (VMImageSection *)pxt_file_buffer;
		
	#else		
		
		const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"js_code");
		spi_flash_mmap_handle_t hrom;
		esp_err_t err = esp_partition_mmap(p,0,p->size,ESP_PARTITION_MMAP_DATA,(const void **)&romdata_jscode,&hrom);
		DMESG("CRC_boot: storage mapped address space [ 0x%x - 0x%x ]",romdata_jscode,romdata_jscode + p->size - 1);
		DMESG("CRC_boot: storage size %d bytes",p->size);
		
		auto sect0	= (VMImageSection *)romdata_jscode;
		
	#endif
    
	auto hd		= (VMImageHeader *)sect0->data;
    
	if (sect0->type != SectionType::InfoHeader || hd->magic0 != VM_MAGIC0 || hd->imageSize < 256)
		{
        DMESG("vmStart() invalid image");
		}
	else
		{
		DMESG("vmStart() valid image with %d bytes",hd->imageSize);
	
		vmStartCore((uint8_t *)sect0,hd->imageSize);
		}
	
	DMESG("END vmStart()");
}


} // namespace pxt