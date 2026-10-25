// Configuration defines

#define PXT_UTF8						1

#define PXT_VM							1

// Ajuste velocidad del simulador EMSCRIPTEN
	#define MSEG_JS_CALL_WORKER			0			// define cada cuantos milisegundos es llamada periodicamente la rutina mainRunLoop()
	#define TFT_SIM_SPI_US				100			// cuantos microsegundos 'tarda' en completarse una transaccion 'DMA' simulada
	#define TFT_SIM_FULLFRAME_US		11000		// cuando hay qye enviar la TFT entera cuanto tarda
										// pongo un valor para que el DMESG de TFTsend 33ms
	
	#define VM_COST_SIM								// Simulacion del COSTE de CPU de la VM del ESP32, comentar para desactivarla
	#define VM_COST_SCALE_PCT			100			// 100 = tabla tal cual, 50 = ESP32 'virtual' el doble de rapido, 200 = el doble de lento

// Tamaño del framebuffer que entrega MakeCode Arcade (updateScreen()) - esto NO cambia con el hardware, siempre es 160x120.
#define TFT_SRC_WIDTH					160
#define TFT_SRC_HEIGHT					120

//#define EMSCRIPTEN_DEBUG_MAPPATHS		true
#define EMSCRIPTEN_DEBUG_MAPPATHS		false
	
// si queremos TRACE en el exec_loop del PC (Program counter) y los OpCodes
//#define TRACE DMESG

// si ponemos esta pausa, no llega a ceder el tiempo las Fibers de a VM a las otras tareas
#ifdef __EMSCRIPTEN__
	//#define TRACE_PAUSA					std::this_thread::sleep_for( std::chrono::milliseconds(1) );
#else
	//#define TRACE_PAUSA					vTaskDelay(pdMS_TO_TICKS(1));
#endif

// en EMSCRIPTEN con 5 es el limite, si no ya no se dibuja el framebuffer en en navegador
#ifdef __EMSCRIPTEN__
	//#define MAINRUNLOOP_PAUSA			std::this_thread::sleep_for( std::chrono::milliseconds(5) );
#else
	//#define MAINRUNLOOP_PAUSA			vTaskDelay(pdMS_TO_TICKS(1000));
#endif

//#define TRACE_MIN						DMESG
#define TRACE_MIN						NOLOG

//#define TRACE_MIN_MIN					DMESG
#define TRACE_MIN_MIN					NOLOG

//#define TRACE_MIN_MIN_CALLIFACE			DMESG
#define TRACE_MIN_MIN_CALLIFACE			NOLOG

//#define PXT_GC_DEBUG					1

//#define TRACE_LOOP						DMESG
#define TRACE_LOOP						NOLOG

//#define TRACE_LOOP_MIN					DMESG
#define TRACE_LOOP_MIN					NOLOG

#define MEMDBG							NOLOG
//#define MEMDBG							DMESG

#define PORCENTAJE_MIDPTR_FIRST			((curr->blockSize * 1) / 2)			// <--- 50%
//#define PORCENTAJE_MIDPTR_FIRST			((curr->blockSize * 3) / 4)			// <--- 75%
//#define PORCENTAJE_MIDPTR_FIRST			((curr->blockSize * 9) / 10)		// <--- 90%

//#define PORCENTAJE_MIDPTR_NEXTS			1 / 4								// <--- 25%
#define PORCENTAJE_MIDPTR_NEXTS			1 / 2								// <--- 50%

#define GC0_DBG							NOLOG
//#define GC0_DBG							DMESG

#define TECLAS_DBG						NOLOG
//#define TECLAS_DBG							DMESG

#define FIBERS_DBG							NOLOG
//#define FIBERS_DBG						DMESG

#define TFT_SIM_DBG						NOLOG
//#define TFT_SIM_DBG						DMESG

//#define VALIDATE_FUNCTIONS
#define VALIDATE_FUNCTIONS_STACK_LIMIT		2000			// si se comenta NO se comprobara !!!
#ifdef VALIDATE_FUNCTIONS
	#define VALIDATE_FUNCTIONS_DBG			DMESG
#else
	#define VALIDATE_FUNCTIONS_DBG			NOLOG
#endif

// deprecated
#define MSEG_TFT						40


/*	Valores especificos Hardware ESP32
*/

// Elegir el hardware conectado: dejar UNA sola de estas dos líneas descomentada (la otra debe quedar comentada)
#define TFT_ST7735_160x128				// hace zoom x1   (160x120)
//#define TFT_ILI9341_320x240				// hace zoom x2   (320x240)
//#define TFT_ILI9488_480x320				// hace zoom x2.5 (400x300)
//#define EMSCRIPTEN_SIN_EMULACION_TFT		// solo sirve para EMSCRIPTEN indica que saltamos la simulacion 'update_screen hardware' por SPI de WOKWI

// Pines hardware de conexion a la TFT ST7735 (CRCbit-pixel)
#ifdef TFT_ST7735_160x128
	#define TFT_PIN_DC						23
	#define TFT_PIN_CS						19
	#define TFT_PIN_RST						18
					
	#define TFT_PIN_SCK						14
	#define TFT_PIN_MOSI					13
	#define TFT_PIN_MISO					12							// aunque NO se usa
	
	#define TFT_PIN_BL						-1							// -1 si no se controla el backlight por software
	
	#define TFT_DMA_BUFFER_TOTAL_BYTES		(16 * 1024)
	
	#define TFT_BYTES_PER_PIXEL				2
#endif

// Pines hardware de conexion a la TFT ILI9341 (CRCbit-touch)
#ifdef TFT_ILI9341_320x240
	#define TFT_PIN_DC						23
	#define TFT_PIN_CS						19
	#define TFT_PIN_RST						18
	
	#define TFT_PIN_SCK						14
	#define TFT_PIN_MOSI					13
	#define TFT_PIN_MISO					12							// aunque NO se usa
	
	#define TFT_PIN_BL						-1							// -1 si no se controla el backlight por software
	
	#define TFT_DMA_BUFFER_TOTAL_BYTES		(16 * 1024)
	
	#define TFT_BYTES_PER_PIXEL				2
#endif

// Pines hardware de conexion a la TFT ILI9488 (CRCbit-com)
#ifdef TFT_ILI9488_480x320
	#define TFT_PIN_DC						23
	#define TFT_PIN_CS						19
	#define TFT_PIN_RST						18
	
	#define TFT_PIN_SCK						14
	#define TFT_PIN_MOSI					13
	#define TFT_PIN_MISO					12							// aunque NO se usa
	
	#define TFT_PIN_BL						-1							// -1 si no se controla el backlight por software
	
	#define TFT_DMA_BUFFER_TOTAL_BYTES		(32 * 1024)
	
	#define TFT_BYTES_PER_PIXEL				3
#endif

#ifdef EMSCRIPTEN_SIN_EMULACION_TFT
	// NO se usa en este caso, pero mantenemos por integridad
	
	#define TFT_PIN_DC						23
	#define TFT_PIN_CS						19
	#define TFT_PIN_RST						18
					
	#define TFT_PIN_SCK						14
	#define TFT_PIN_MOSI					13
	#define TFT_PIN_MISO					12							// aunque NO se usa
	
	#define TFT_PIN_BL						-1							// -1 si no se controla el backlight por software
	
	#define TFT_DMA_BUFFER_TOTAL_BYTES		(16 * 1024)
	
	#define TFT_BYTES_PER_PIXEL				2
#endif

#define TFT_SPI_HOST					SPI2_HOST					// = antiguo HSPI_HOST
//#define TFT_SPI_HOST					SPI3_HOST					// = antiguo VSPI_HOST
#define TFT_SPI_HZ						(40 * 1000 * 1000)			// 40MHz

/*	Tamaño total del bloque DMA-capable único, minimo full buffer 4bpp (9600) +
	dos buffers DMA (min 1280 bytes para dos lineas de zoom x2) */
// se define dentro de cada TFT hardware

// Constantes que definen las velocidades del Hardware para hacer calculos y estimaciones
#define TFT_CPU_FREQ_MHZ				240
#define TFT_CPU_FREQ_HZ					(TFT_CPU_FREQ_MHZ * 1000 * 1000)		// ajusta si tu sdkconfig usa otra frecuencia de CPU (ej. 160MHz)
#define SPI_BUS_HZ						TFT_SPI_HZ
#define BYTES_PER_PIXEL     			TFT_BYTES_PER_PIXEL
#define CPU_FREQ_HZ    					TFT_CPU_FREQ_HZ
#define USEG_SET_TFT_COMANDS			1							// lo he puesto a voleo , calcular !!! PENDIENTE
#define CICLOS_TFT_SET_COMANDS			( (CPU_FREQ_HZ / 1000000UL) * USEG_SET_TFT_COMANDS )

// RIC PENDIENTE ver como se activa este DEBUG y que hace ???
//#define PXT_PROFILE

// Estima el coste en tiempo de uns transaccion DMA de area TFT -> T_cmd + w*h*bytes/(SPI_HZ/8) en ciclos CPU
//#define THRESHOLD			3 ) / 4				// 75% es 3/4
#define THRESHOLD			1 ) / 2				// 50% es 1/2
#define CALCULO_DE_VALOR_CONSTANTE				( ( ( CICLOS_TFT_SET_COMANDS + ( ( (uint64_t)SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL * 8 * CPU_FREQ_HZ ) / SPI_BUS_HZ ) ) * THRESHOLD )

