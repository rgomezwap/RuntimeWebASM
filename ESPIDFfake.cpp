
#include "ESPIDFfake.h"

#define ALWAYS_INLINE			__attribute__((always_inline))

#include <iostream>												// para usar -> std::cout << ...

#include <emscripten.h>	
#include <emscripten/val.h>										// usaremos emscripten::val -> y hay que hacer el build con la libreria -lmbind, y es necesario un .js que genera con las funciones que enlazan C++ y Js
#include <emscripten/bind.h>

#include "pxt.h"

#include "heapsnapshot.h"


/*	Inicio codigo simular ESP32 ESP-IDF specific
*/

TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
	return 0;
}

int64_t esp_timer_get_time(void)
{
	// En Emscripten usamos performance.now() a través de emscripten_get_now() que devuelve milisegundos con decimales
	double ms=emscripten_get_now();
	
	return (int64_t)(ms * 1000.0);     							// convertimos a microsegundos
}


void vPortEnterCritical( portMUX_TYPE *mux )
{}

void vPortExitCritical( portMUX_TYPE *mux )
{}


int ets_printf(const char *fmt, ...)
{
	//printf("RIC debug : ets_log_dmesg() -> ets_printf()\n");
	return 0;
}
	
uint32_t esp_log_timestamp(void)
{
	// emscripten_get_now() devuelve milisegundos en double
    return static_cast<uint32_t>(emscripten_get_now());
}

void ESP_LOGW(char *str,char *format,void *)
{}


void mbedtls_sha256_init(mbedtls_sha256_context *ctx)
{}
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{ return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *input,size_t ilen)
{ return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,unsigned char output[32])
{ return 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *ctx)
{}


esp_err_t esp_task_wdt_init(uint32_t timeout_s, bool panic)
{ return 0; }
esp_err_t esp_task_wdt_add(TaskHandle_t handle)
{ return 0; }
esp_err_t esp_task_wdt_delete(TaskHandle_t handle)
{ return 0; }
esp_err_t esp_task_wdt_reset(void)
{ return 0; }


esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{ return 0; }
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
	// se lo pasamos a WOKWI
	
	// como es WOKWI el gpio_num NO corresponde con los PINs de ESP32 , si no con los pin_init de WOKWI
	wokwi_set_pin(g_pins[gpio_num].name,level);
	
	return ESP_OK;
}
int gpio_get_level(gpio_num_t gpio_num)
{ return 0; }
esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
{ return 0; }

/*	Hacemos un FAKE para controlar la memoria consumida por GC
*/
int rammax=532480L;								// 520Kb
int ramfreeatstart=204800L + 9620 + 184320;		// 	200Kb ESP32 ram free + screenbuffer + PXT64 binario en memoria
int ramfreeactual=ramfreeatstart;

int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{ return 0; }
int gpio_install_isr_service(int arg)
{
	printf("RIC debug : WebASM : gpio_install_isr_service()\n");
	return 0;
}
size_t heap_caps_get_free_size(uint32_t caps)
{
	// simulamos ESP32 memoria tipica despues de cargar libreria
	return ramfreeactual;
}
size_t heap_caps_get_total_size(uint32_t caps)
{
	// simulamos ESP32 con memoria libre al maximo
	return rammax;
}
void *heap_caps_malloc(size_t size, uint32_t caps)
{
	// en el simulador no existe memoria "DMA capable" de verdad: un malloc() normal vale
	(void)caps;
	return malloc(size);
}
void esp_restart(void)
{ }
uint32_t esp_random(void)
{
	// rand() genera un entero hasta RAND_MAX (suele ser 32767 o 2147483647 según la plataforma).
    // Combinamos dos llamadas para asegurarnos de llenar los 32 bits de forma distribuida:
    uint32_t r1 = (uint32_t)rand();
    uint32_t r2 = (uint32_t)rand();
    
    return (r1 << 16) ^ r2;
}


esp_err_t nvs_flash_init(void)
{ return 0; }
esp_err_t nvs_flash_erase(void)
{ return 0; }
esp_err_t nvs_open(const char* name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{ return 0; }
void nvs_close(nvs_handle_t handle)
{}
esp_err_t nvs_commit(nvs_handle_t handle)
{ return 0; }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key)
{ return 0; }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length)
{ return 0; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length)
{
	// RIC de momento con esto anulo que se puedan leer key/name tipo base de datos, que en
	// ESP32 usa el NVS
	return ESP_ERR_NVS_NOT_FOUND;
}
nvs_iterator_t nvs_entry_find(const char* part_name, const char* namespace_name, nvs_type_t type)
{ return 0; }
nvs_iterator_t nvs_entry_next(nvs_iterator_t iterator)
{ return 0; }
void nvs_entry_info(nvs_iterator_t iterator, nvs_entry_info_t* out_info)
{}
void nvs_release_iterator(nvs_iterator_t iterator)
{}


void vTaskDelay(const TickType_t xTicksToDelay)
{
	// Anulamos que usa ASYNCIFY y colisona con el worker_main !!! 
	//emscripten_sleep(1); // Libera el hilo de JS por 1 milisegundo
}


esp_err_t esp_efuse_mac_get_default(uint8_t mac[6])
{ return 0; }


QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize)
{ return 0; }
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{ return 0; }
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue, BaseType_t *pxHigherPriorityTaskWoken)
{ return 0; }
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{ return 0; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,const char * const pcName,const uint32_t usStackDepth,void * const pvParameters,UBaseType_t uxPriority,TaskHandle_t * const pvCreatedTask,const BaseType_t xCoreID)
{
	/*	En el simulador NO se lanza ninguna tarea de verdad: st7735_dma_worker_task()
		(el 'pvTaskCode' que nos pasa st7735_hal_init()) es un for(;;) que solo tendria
		sentido con un thread/tarea real detras. Aqui el mismo efecto se consigue
		ejecutando el paso de trabajo directamente desde vTaskNotifyGiveFromISR()
		(ver mas abajo), asi que aqui basta con devolver un handle valido para que
		el resto del codigo compartido (st7735_hal_init, dma_trans_done_cb...) no
		necesite saber que aqui no hay tarea real. */
	(void)pvTaskCode; (void)pcName; (void)usStackDepth; (void)pvParameters; (void)uxPriority; (void)xCoreID;
	if (pvCreatedTask) *pvCreatedTask = (TaskHandle_t)1;
	return pdPASS;
}


/*	--- fake de semaforo binario (xSemaphoreCreateBinary/Take/Give) ---
	Ver comentario en ESPIDFfake.h. xSemaphoreTake() cede el hilo de JS con
	emscripten_sleep() (requiere -sASYNCIFY, ya activado en buildems.js) mientras
	el semaforo no este "dado", exactamente igual que ya hace vTaskDelay() de arriba.
*/
struct tskSemaphoreFake
	{
	volatile bool given;
	};

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	tskSemaphoreFake *s = new tskSemaphoreFake();
	s->given = true;			// arranca "dado", igual que xSemaphoreCreateBinary() + xSemaphoreGive() en st7735_hal_init()
	return (SemaphoreHandle_t)s;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
	if (!sem) return pdFAIL;
	((tskSemaphoreFake *)sem)->given = true;
	return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t xTicksToWait)
{
	(void)xTicksToWait;			// el simulador solo soporta la espera "indefinida" (portMAX_DELAY) que usa el codigo compartido
	
	if (!sem) return pdFAIL;
	
	tskSemaphoreFake *s = (tskSemaphoreFake *)sem;
	
	/*	OJO: ya NO deberiamos entrar nunca aqui con !s->given en el camino de
		updateScreen(), porque ahora updateScreen() comprueba xSemaphorePeek()
		ANTES de llegar a st7735_core_update_screen()/xSemaphoreTake() (ver
		screen.cpp). Este bucle con emscripten_sleep()/ASYNCIFY se deja como
		red de seguridad para otras rutas (o para no romper compat.), pero
		si se entra en el bucle activamente durante una partida es sintoma
		de que algo esta llamando a xSemaphoreTake() sin pasar por el peek. */
	while (!s->given)
		{
		DMESG("ERROR GRAVE - NO DEBERIA ENTRAR");
		emscripten_sleep(1);
		}
	
	s->given = false;
	
	return pdTRUE;
}

bool xSemaphorePeek(SemaphoreHandle_t sem)
{
	if (!sem) return false;
	return ((tskSemaphoreFake *)sem)->given;
}


/*	--- fake de notificacion de tarea (vTaskNotifyGiveFromISR/ulTaskNotifyTake) ---
	Ver comentario en ESPIDFfake.h.
*/

// definida en screen---ext/screen.cpp (namespace pxt, pero con enlace "C" para
// no depender del namespace ni del mangling); es el 'cuerpo' de
// st7735_dma_worker_task(), extraido para poder llamarlo directamente aqui
extern "C" void st7735_dma_worker_task_step(void);

void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken)
{
	(void)xTaskToNotify;
	
	if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
	
	// no hay tarea real que despertar: ejecutamos ya mismo su paso de trabajo
	st7735_dma_worker_task_step();
}

uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
	// no la usa ningun camino del simulador (ver vTaskNotifyGiveFromISR() arriba);
	// se deja definida solo para que el codigo compartido enlace igual en ambas plataformas
	(void)xClearCountOnExit; (void)xTicksToWait;
	return 0;
}


UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask)
{ (void)xTask; return 0; }
int xPortGetCoreID(void)
{ return 0; }


// XTHAL_GET_CCOUNT() fake -> ver comentario en ESPIDFfake.h
uint32_t fake_xthal_get_ccount(void)
{
	uint64_t us = (uint64_t)esp_timer_get_time();				// microsegundos (fake, via emscripten_get_now())
	return (uint32_t)((us * (uint64_t)TFT_CPU_FREQ_HZ) / 1000000ULL);
}




/*	Inicio codifgo del simulador ST7735
*/

#include "wokwi-api.h"

#define TAM_FRAME_PIXELS		(g_fb_width * g_fb_height)
#define TAM_BYTE_PIXEL			4
#define TAM_FRAME_BUFFER		(TAM_FRAME_PIXELS * TAM_BYTE_PIXEL)			// RGBA8888

/* ---------------------------------------------------------------- */
/* Pines                                                              */
/* ---------------------------------------------------------------- */

#define MAX_PINS 16

pin_record_t g_pins[MAX_PINS];
static int g_pin_count = 0;

// RIC
int find_pin(const char *name)
{
	for (int i = 0; i < g_pin_count; i++)
		{
		if (strcmp(g_pins[i].name, name) == 0) return i;
		}
	return -1;
}

pin_t pin_init(const char *name, uint32_t mode)
{
	if (g_pin_count >= MAX_PINS) return NO_PIN;
	
	int idx = g_pin_count++;
	
	strncpy(g_pins[idx].name, name, sizeof(g_pins[idx].name) - 1);
	
	g_pins[idx].name[sizeof(g_pins[idx].name) - 1] = 0;
	g_pins[idx].mode = mode;
	g_pins[idx].watched = false;

	switch (mode)
		{
		case INPUT_PULLUP:
			g_pins[idx].value = HIGH;
			printf("RIC debug : WOKWI Pin %s configured INPUT_PULLUP starts HIGH\n",name);
			break;
		
		case OUTPUT_HIGH:
			g_pins[idx].value = HIGH;
			printf("RIC debug : WOKWI Pin %s configured OUTPUT_HIGH starts HIGH\n",name);
			break;
			
		default:
			g_pins[idx].value = LOW;
			printf("RIC debug : WOKWI Pin %s configured default starts LOW\n",name);
			break;
		}
	
	return (pin_t)idx;
}

uint32_t pin_read(pin_t pin) {
  if (pin < 0 || pin >= g_pin_count) return 0;
  return g_pins[pin].value;
}

void pin_write(pin_t pin, uint32_t value) {
  if (pin < 0 || pin >= g_pin_count) return;
  g_pins[pin].value = value;
}

bool pin_watch(pin_t pin, const pin_watch_config_t *config) {
  if (pin < 0 || pin >= g_pin_count || !config) return false;
  g_pins[pin].watch = *config;
  g_pins[pin].watched = true;
  return true;
}

void pin_watch_stop(pin_t pin) {
  if (pin >= 0 && pin < g_pin_count) g_pins[pin].watched = false;
}

void pin_mode(pin_t pin, uint32_t value) {
  if (pin >= 0 && pin < g_pin_count) g_pins[pin].mode = value;
}

float pin_adc_read(pin_t pin) { (void)pin; return 0.0f; }
float pin_dac_write(pin_t pin, float voltage) { (void)pin; return voltage; }

/* attr_*, i2c_*, uart_* no los usa el chip ST7735: sin implementar. */

/* ---------------------------------------------------------------- */
/* SPI (modelo lógico, por ráfagas de bytes)                         */
/* ---------------------------------------------------------------- */

typedef struct {
  void *user_data;
  void (*done)(void *user_data, uint8_t *buffer, uint32_t count);
  uint8_t *buffer;
  uint32_t buffer_size;
  bool active;
  bool used;
} spi_record_t;

#define MAX_SPI 2
static spi_record_t g_spi_devs[MAX_SPI];
static int g_spi_count = 0;

spi_dev_t spi_init(const spi_config_t *spi_config)
{
	printf("RIC debug : WOKWI spi_init\n");
  
	if (g_spi_count >= MAX_SPI) return (spi_dev_t)-1;
	int idx = g_spi_count++;
	g_spi_devs[idx].user_data = spi_config->user_data;
	g_spi_devs[idx].done = spi_config->done;
	g_spi_devs[idx].buffer = NULL;
	g_spi_devs[idx].buffer_size = 0;
	g_spi_devs[idx].active = false;
	g_spi_devs[idx].used = true;
	
	return (spi_dev_t)idx;
}

void spi_start(const spi_dev_t spi, uint8_t *buffer, uint32_t count)
{
	TFT_SIM_DBG("RIC debug : spi_start\n");
	
	if (spi >= MAX_SPI || !g_spi_devs[spi].used) return;
	g_spi_devs[spi].buffer = buffer;
	g_spi_devs[spi].buffer_size = count;
	g_spi_devs[spi].active = true;
}

void spi_stop(const spi_dev_t spi)
{
	TFT_SIM_DBG("RIC debug : spi_stop\n");
	
	if (spi >= MAX_SPI || !g_spi_devs[spi].used) return;
	
	if (g_spi_devs[spi].active)
		{
	
		g_spi_devs[spi].active = false;
	
		if (g_spi_devs[spi].done)
			{
			/* count == 0: "llegamos aquí por spi_stop, sin datos", tal
			y como el propio chip_spi_done espera e ignora. */
			g_spi_devs[spi].done(g_spi_devs[spi].user_data, g_spi_devs[spi].buffer, 0);
			}
		
		}
	
}

/* Sólo hay un dispositivo SPI en este chip; lo guardamos aparte
   para que el puente JS no tenga que conocer el id. */
static spi_dev_t g_chip_spi = (spi_dev_t)-1;

/* ---------------------------------------------------------------- */
/* Framebuffer                                                       */
/* ---------------------------------------------------------------- */

static uint8_t *g_framebuffer = NULL;
uint32_t g_fb_width = 128;
uint32_t g_fb_height = 160;
static const buffer_t g_fb_id = 1;

buffer_t framebuffer_init(uint32_t *pixel_width, uint32_t *pixel_height)
{
	printf("RIC debug : WOKWI framebuffer_init\n");	
	
	free(g_framebuffer);
	
	g_framebuffer = (uint8_t*)calloc((size_t)TAM_FRAME_PIXELS,TAM_BYTE_PIXEL * sizeof(uint8_t));		// RGBA8888
	
	*pixel_width = g_fb_width;
	*pixel_height = g_fb_height;
	
	return g_fb_id;
}

void buffer_write(buffer_t buffer, uint32_t offset, uint32_t *data, uint32_t data_len) {
  (void)buffer;
  if (!g_framebuffer) return;
  uint32_t fb_bytes = g_fb_width * g_fb_height * 4;
  if (offset >= fb_bytes) return;
  uint32_t n = data_len;
  if (offset + n > fb_bytes) n = fb_bytes - offset;
  memcpy(g_framebuffer + offset, data, n);
}

void buffer_read(buffer_t buffer, uint32_t offset, uint32_t *data, uint32_t data_len) {
  (void)buffer;
  if (!g_framebuffer) return;
  uint32_t fb_bytes = g_fb_width * g_fb_height * 4;
  if (offset >= fb_bytes) return;
  uint32_t n = data_len;
  if (offset + n > fb_bytes) n = fb_bytes - offset;
  memcpy(data, g_framebuffer + offset, n);
}

/* ---------------------------------------------------------------- */
/* Puente hacia JavaScript                                           */
/* ---------------------------------------------------------------- */

/*	Debe llamarse ANTES de wokwi_chip_init() si se quiere un tamaño
	distinto al de 128x160 por defecto (0 = mantener el valor por
	defecto).
*/
EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_configure(uint32_t width, uint32_t height)
{
	if (width) g_fb_width = width;
	if (height) g_fb_height = height;
}

EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_chip_init(void) {
  chip_init();
  g_chip_spi = (spi_dev_t)(g_spi_count - 1); /* el último spi_init() fue el del chip */
}

// Pone un pin a un valor y dispara el watcher si cambia (igual que haría el simulador real de Wokwi ante un flanco)
EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_set_pin(const char *name, uint32_t value)
{
	int idx = find_pin(name);
	
	if (idx < 0) return;
	if (g_pins[idx].value == value) return;
	
	g_pins[idx].value = value;
	
	if (g_pins[idx].watched && g_pins[idx].watch.pin_change)
		{
		g_pins[idx].watch.pin_change(g_pins[idx].watch.user_data, (pin_t)idx, value);
		}
	
}

// Entrega una ráfaga de bytes SPI ya recibida (equivalente al callback `done` que Wokwi dispararía tras SCK/MOSI)
EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_spi_transfer(uint8_t *data, uint32_t len)
{
	TFT_SIM_DBG("RIC debug : spi_transfer %d bytes\n",len);
	
	if (g_chip_spi == (spi_dev_t)-1) return;
	
	spi_record_t *spi = &g_spi_devs[g_chip_spi];
	
	if (!spi->active || !spi->buffer) return;
	
	// RIC DEBUG
	if (len > spi->buffer_size) printf("RIC debug :     ERROR send more SPI bytes than internal buffer size\n");
	
	uint32_t n = len < spi->buffer_size ? len : spi->buffer_size;
	memcpy(spi->buffer, data, n);
	
	if (spi->done) spi->done(spi->user_data, spi->buffer, n);
	
}


/*	Helpers de alto nivel: un comando de 1 byte, o un bloque de datos
	(se trocea automáticamente al tamaño del buffer SPI interno del
	chip, típicamente 1024 bytes).
*/
EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_write_command(uint8_t cmd)
{
	wokwi_set_pin("CS", LOW);
	
	wokwi_set_pin("DC", LOW);
	
	wokwi_spi_transfer(&cmd, 1);
	
	wokwi_set_pin("CS", HIGH);
}

EMSCRIPTEN_KEEPALIVE
void wokwi_write_data(emscripten::val jsvar)
{
	TFT_SIM_DBG("RIC debug : wokwi_write_data\n");
	
	/*	Recibimos una variable de Javascript que es Uint8Array
		y primero lo pasamos a una copia en memoria de C++
	*/
	
	int longitudbytes=jsvar["length"].as<int>();
	
	uint8_t *tmpPtr=new uint8_t[longitudbytes];
	emscripten::val memoryView{ emscripten::typed_memory_view( longitudbytes , tmpPtr ) };
	
	// rellenamos la nueva variable array de bytes con los datos originales del array de Javascript
	memoryView.call<void>("set",jsvar);
	
	
	/*	Ahora lo procesamos normal en C++
	*/
	
	uint8_t *data=tmpPtr;
	
	wokwi_set_pin("CS", LOW);
	wokwi_set_pin("DC", HIGH);
	
	uint32_t offset = 0;
	while (offset < longitudbytes)
		{
		
		spi_record_t *spi = (g_chip_spi != (spi_dev_t)-1) ? &g_spi_devs[g_chip_spi] : NULL;
		
		uint32_t cap = (spi && spi->buffer_size) ? spi->buffer_size : longitudbytes;
		uint32_t chunk = (longitudbytes - offset) < cap ? (longitudbytes - offset) : cap;
		
		wokwi_spi_transfer(data + offset, chunk);
		
		offset += chunk;
		
		}
	
	wokwi_set_pin("CS", HIGH);
	
}

void wokwi_write_data_raw(const uint8_t *data, uint32_t len)
{
	wokwi_set_pin("CS", LOW);
	wokwi_set_pin("DC", HIGH);
	
	uint32_t offset = 0;
	while (offset < len)
		{
		
		spi_record_t *spi = (g_chip_spi != (spi_dev_t)-1) ? &g_spi_devs[g_chip_spi] : NULL;

		uint32_t cap	= (spi && spi->buffer_size) ? spi->buffer_size : len;
		uint32_t chunk	= (len - offset) < cap ? (len - offset) : cap;

		wokwi_spi_transfer((uint8_t *)data + offset, chunk);

		offset += chunk;

		}
	
	wokwi_set_pin("CS", HIGH);
}

EMSCRIPTEN_KEEPALIVE
cppexport
void wokwi_reset(void)
{
	/* Pulso de reset (activo en bajo, como el pin real). */
	wokwi_set_pin("RST", LOW);
	wokwi_set_pin("RST", HIGH);
}


/*	Acceso al framebuffer desde JS: puntero + tamaño en bytes RGBA8888,
	listo para volcar directo en un ImageData de canvas.
*/
EMSCRIPTEN_KEEPALIVE
emscripten::val wokwi_get_framebuffer(void)
{
	// Crear una variable Uint8Array de JavaScript
    emscripten::val jsuint8array = emscripten::val::global("Uint8Array").new_(TAM_FRAME_BUFFER);
	
    // Utilizar memory_view para copiar todos los datos de una vez
    jsuint8array.call<void>("set",emscripten::memory_view<uint8_t>(TAM_FRAME_BUFFER,g_framebuffer));
	
	// esta variable que ocupa memoria ya es gestionada desde javascript , el garbage ...
    return jsuint8array;
}

EMSCRIPTEN_KEEPALIVE
cppexport
uint32_t wokwi_get_width(void) { return g_fb_width; }

EMSCRIPTEN_KEEPALIVE
cppexport
uint32_t wokwi_get_height(void) { return g_fb_height; }




/*	Inicio codigo RIC especifico
*/

// Buffer RGBA de la pantalla de Arcade Makecode en WebASM
#define DISPLAY_WIDTH		160
#define DISPLAY_HEIGHT		120
uint32_t wasmScreenBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];

namespace pxt
{
	
	// Búfer global para mantener en RAM el archivo cargado desde disco
	uint8_t *pxt_file_buffer=NULL;
	unsigned pxt_file_buffer_len=0;
	
	uint8_t *screenBuf;
	uint32_t currPalette[16];
	
	// para DEBUG -> Pila para rastrear los punteros de inicio de cada función invocada
	std::vector<uint16_t*> startfunctionpc;
	bool despuesRET1=false;
	int contador_destroyVTPunterosInvalidos=0;
	
	// personalizamos el DEBUG filtrando los #ID de las funciones de la imagen VM
	std::vector<int> dbg_ignoredIDs;
	
	// Lee un archivo .PXT64 de disco y actualiza PXT_EXPORTData[4] para que apunte a esta nueva imagen en memoria.
	bool CargarFilePXT64(const char *filepath)
	{
		FILE *f = fopen(filepath, "rb");
		if (!f)
			{
			DMESG("Error al abrir archivo: %s",filepath);
			return false;
			}

		// Obtener tamaño del archivo
		fseek(f, 0, SEEK_END);
		long fileSize = ftell(f);
		fseek(f, 0, SEEK_SET);

		if (fileSize < 256)
			{
			DMESG("Archivo .PXT64 inválido o demasiado pequeño (%ld bytes)", fileSize);
			fclose(f);
			return false;
			}

		// Si ya teníamos un búfer previo cargado, lo liberamos
		if (pxt_file_buffer != NULL)
			{
			free(pxt_file_buffer);
			pxt_file_buffer = NULL;
			}

		// Reservar memoria RAM para alojar la imagen .PXT64
		pxt_file_buffer = (uint8_t *)xmalloc(fileSize);
		if (!pxt_file_buffer)
			{
			DMESG("Sin memoria RAM suficiente para la imagen .PXT64");
			fclose(f);
			return false;
			}
		DMESG("Mem. para pxt_file_buffer %d bytes",fileSize);
		
		// Leer el archivo en el búfer
		size_t bytesRead = fread(pxt_file_buffer, 1, fileSize, f);
		fclose(f);

		if (bytesRead != (size_t)fileSize)
			{
			DMESG("Error de lectura del archivo .PXT64");
			free(pxt_file_buffer);
			pxt_file_buffer = NULL;
			return false;
			}
			
		pxt_file_buffer_len=fileSize;
			
		DMESG("Cargada imagen binaria .PXT64 de disco a memoria");
		
		return true;
	}
	
	//	debug de PXT_EXPORTData
	void dumpPXTExportDataAndvmImg()
	{
		
		DMESG("========== Punteros mem. ==========");
		
		DMESG("*wasmScreenBuffer                  = %x", (void *)wasmScreenBuffer);
		DMESG("*screenBuf                         = %x", (void *)screenBuf);
		DMESG("*pxt_file_buffer                   = %x", (void *)pxt_file_buffer);
		DMESG("vmImg->dataStart                   = %x", (void *)vmImg->dataStart);
		DMESG("vmImg->dataEnd                     = %x", (void *)vmImg->dataEnd);		
		DMESG("Lista punteros a vmImg Funciones   = %x", (void *)vmImg->pointerLiterals);
		DMESG("Lista punteros a secciones         = %x", (void *)vmImg->sections);
		DMESG("Lista punteros a Literales         = %x", (void *)vmImg->numberLiterals);
		DMESG("Array punteros a opcodeDescs       = %x", (void *)vmImg->opcodeDescs);
		
		DMESG("========== PXT_EXPORTData ==========");

		DMESG("[0 ] Magic1                        = %p", (void *)(uint32_t)PXT_EXPORTData[0]);
		DMESG("[1 ] Magic2                        = %p", (void *)(uint32_t)PXT_EXPORTData[1]);
		DMESG("[2 ] Magic3                        = %p", (void *)(uint32_t)PXT_EXPORTData[2]);
		DMESG("[3 ] Magic4                        = %p", (void *)(uint32_t)PXT_EXPORTData[3]);
												  
		DMESG("[4 ] buffer_vt                     = %x", (void *)PXT_EXPORTData[4]);
		DMESG("[5 ] number_vt                     = %x", (void *)PXT_EXPORTData[5]);
		DMESG("[6 ] RefAction_vtable              = %x", (void *)PXT_EXPORTData[6]);
		DMESG("[7 ] string_inline_ascii_vt        = %x", (void *)PXT_EXPORTData[7]);
		DMESG("[8 ] string_skiplist16_packed_vt   = %x", (void *)PXT_EXPORTData[8]);
		DMESG("[9 ] string_inline_utf8_vt         = %x", (void *)PXT_EXPORTData[9]);
												  
		DMESG("[10] RefRecord_destroy             = %x", (void *)PXT_EXPORTData[10]);
		DMESG("[11] RefRecord_print               = %x", (void *)PXT_EXPORTData[11]);
		DMESG("[12] RefRecord_scan                = %x", (void *)PXT_EXPORTData[12]);
		DMESG("[13] RefRecord_gcsize              = %x", (void *)PXT_EXPORTData[13]);
		DMESG("[14] End                           = %p", (void *)PXT_EXPORTData[14]);
		
		DMESG("========== vmImg         ==========");

		DMESG("vmImg->infoHeader                  = %x", (void *)vmImg->infoHeader);
		DMESG("vmImg->entryPoint                  = %x", (void *)vmImg->entryPoint);
		
		DMESG("vmImg->numSections                 = %d", (uint16_t)vmImg->numSections);
		DMESG("vmImg->numNumberLiterals           = %d", (uint16_t)vmImg->numNumberLiterals);
		DMESG("vmImg->numConfigDataEntries        = %d", (uint16_t)vmImg->numConfigDataEntries);
		DMESG("vmImg->numOpcodes                  = %d", (uint16_t)vmImg->numOpcodes);
		DMESG("vmImg->numIfaceMemberNames         = %d", (uint16_t)vmImg->numIfaceMemberNames);
		
		DMESG("===================================");
		
	}	
	
	void pxt_screen_get_pixels(int width, int height, uint32_t *screen)
	{
		
		if (width != 160 || height != 120) target_panic(PANIC_SCREEN_ERROR);
		
		if (panicCode > 0)
			{
			
			int n = width * height;
			uint32_t *p = screen;
			
			// blue screen
			while (n--)
				{
				*p++ = 0xff0000ff;
				}
			
			}
		else
			{
			
			auto sp = screenBuf;
			
			auto pal = currPalette;
			
			for (int x = 0; x < width; ++x)
				{
				
				uint32_t *p = screen + x;
				
				for (int y = 0; y < (height >> 1); ++y)
					{
					
					uint8_t v = *sp++;
					
					*p = pal[v & 0xf];
					p += width;
					
					*p = pal[v >> 4];
					p += width;
					
					}
				
				}
		
			}
		
	}

	// Un FAKE para no tener que imcluir el #include <emscripten.h>	
	uint32_t FAKEemscripten_get_now(void)
	{
		// En Emscripten usamos performance.now() a través de emscripten_get_now() que devuelve milisegundos con decimales
		double ms=emscripten_get_now();
		
		return (uint32_t)(ms * 1000.0);     						// convertimos a microsegundos
	}
	
}

// definicion de funciones originales de DOCKERESPIDF
extern "C" int app_main();

EMSCRIPTEN_KEEPALIVE
cppexport
void LlamarMainOriginal(void)
{
	
	// para que salga por la consola, hay que enviar el \n , hasta entonces no lo envia a la consola
	printf("RIC debug : WebASM : LlamarMainOriginal()\n");
	
	// comprobacion para ver el entorno de compilacion en que trabaja 32 bits y 64 bits ?
	printf("RIC debug : sizeof(void*)=%d sizeof(BoxedNumber)=%d\n",(int)sizeof(void*),(int)sizeof(BoxedNumber));
	if (PXT32 && (int)sizeof(void*) == 8)
		{
		printf("RIC debug : ERROR flagg PXT32 para compilador de 64 bits\n");
		}
	
	// RIC DEBUG
	printf("RIC debug : \n");
		
	// inicializamos el buffer que contiene los pixels de la pantalla de Arcade Makecode 160x120
	screenBuf = (uint8_t*) xmalloc(160 * 120 / 2 + 20);						// dejamos un margen de 20 bytes
	printf("RIC debug : Primer puntero *screenBuf empieza en %p\n",screenBuf);
	printf("RIC debug : Mem. para pxt_file_buffer %d bytes\n",160 * 120 / 2 + 20);
	
	// generamos la funcion periodica que simula la Task de FreeRTOS
	#ifdef __EMSCRIPTEN__
		
		/*	explicacion de como conseguir en EMSCRIPTEN hacer funciones que se llamen periodicamente
	
			void emscripten_set_main_loop
					(
					em_arg_callback_func func, // Función C/C++ a ejecutar en cada cuadro
					int fps,                    // Cuadros por segundo (0 = sincronizado con el monitor)
					int simulate_infinite_loop  // 1 = pausar la ejecución de main(), 0 = continuar main()
					);
		
			1er param 0 = usar requestAnimationFrame (60 FPS sincronizados con el navegador)
			2o param  1 = simular bucle infinito de forma asíncrona en JS
			
			antes tenia esto -> emscripten_set_main_loop(pxt::worker_main,0,20);
			
			Ahora uso -> emscripten_async_call
				como parametros NULL
				y el 2o param son los milisegundos
				hace en javascript un Timeout ...
		*/
		
		emscripten_async_call(pxt::worker_main, NULL,0);
		
	#endif
	
	if (pxt::CargarFilePXT64("Storage/pxt64.pxt64"))
		{		
		int resultado;

		resultado=app_main();
		}
	
	/*	Flujo de inicio
	
			app_main()
				pxt::vmStart()
					memInfo()
					install_gpio0_handler()
						gpio_install_isr_service(...)
					(cargar imagen y verificar -> minima comprobacion de la la InfoHeader)	-> PXT_EXPORTData
					vmStartCore( ... )
						unloadVMImage( ... )
						gcPreStartup( ... )
						auto img = loadVMImage( ... )		<- comprobacion profunda de la imagen
						gcStartup( ... )
						globals = ...
						initRuntime( ... )
							current_time_ms( ... )
							target_startup( ... )
							setupThread( ... )
							target_init( ... )
							screen_init( ... )
							initKeys( ... )
							mainRunLoop( ... )
							systemReset( ... )
					
	*/
	
	/*	para hacer salida de DEBUG ...
	
		en ArcadeMakecode con CODAL se implementa:
	
			DMESG( ...)
				dmesg( ...)
					vdmesg( ...)
						dmesgRaw( ...)
							codalLogStore.write( ...)
								LogQueue::write( ...)		<-	que va guardando en un buffer , encadenando los mensajes
																y se consume con ets_log_dmesg() que saca los textos
																con ets_printf(...) que es la funcion interna de la ROM
																de ESP32 para sacar mensajes serie, como los que salen
																cuando se hace un boot... esos mensajes antes de la APP
																
		ets_log_dmesg()
			se llama en void soft_panic(...)
					y
			static void fg_worker_idle(...)		<- que es una tarea periodica de cuando esta IDLE el runtime !!!
																					
	*/
	
	
	printf("RIC debug :     END LlamarMainOriginal()\n");
	
}

EMSCRIPTEN_KEEPALIVE
emscripten::val get_screen_buffer_pointer(void)
{
	
	// primero pasamos los pixeles del buffer Arcade Makecode a buffer RGBA normal para HTML5 Canvas
	pxt_screen_get_pixels(160,120,wasmScreenBuffer);
	
	// Crear una variable Uint8Array de JavaScript
    emscripten::val jsuint8array = emscripten::val::global("Uint8Array").new_( sizeof(wasmScreenBuffer) );
	
    // Utilizar memory_view para copiar todos los datos de una vez
    // esto da error -> emscripten::val memoryView=emscripten::val::memory_view<unsigned char>(sizeof(tft160x120xRGB565),(unsigned char *)tft160x120xRGB565);
	// y uso memory_view directamente en la llamada a set
    jsuint8array.call<void>( "set" , emscripten::memory_view<uint8_t>( sizeof(wasmScreenBuffer) , reinterpret_cast<uint8_t*>(wasmScreenBuffer) ) );
	
	// esta variable que ocupa memoria ya es gestionada desde javascript , el garbage ...
    return jsuint8array;
}

EMSCRIPTEN_KEEPALIVE
cppexport
void setKeyState(int pressed,int cfgKey)
{
	TECLAS_DBG("RIC debug : WebASM : setKeyState(%d,%d)\n",pressed,cfgKey);
	
	// src -> pressed , value -> cfgKey
	pxt::raiseEvent(pressed,cfgKey);
}


/*	RIC: puente a JS para el disparo condicional del snapshot de heap.
*/

EMSCRIPTEN_KEEPALIVE
cppexport
void setGCSnapshotInterval(uint32_t n)
{
	pxt::setGCSnapshotInterval(n);
}

EMSCRIPTEN_KEEPALIVE
cppexport
void requestHeapSnapshot(void)
{
	pxt::requestHeapSnapshot();
}

EMSCRIPTEN_KEEPALIVE
cppexport
void setHaltAfterSnapshot(bool halt)
{
	pxt::setHaltAfterSnapshot(halt);
}

EMSCRIPTEN_KEEPALIVE
cppexport
bool isHeapSnapshotHalted(void)
{
	return pxt::isHeapSnapshotHalted();
}

EMSCRIPTEN_KEEPALIVE
void SetFunctionIDsIgnore(emscripten::val jsvar)
{
	dbg_ignoredIDs=emscripten::vecFromJSArray<int>(jsvar);
}




EMSCRIPTEN_BINDINGS(module)
{
	
	emscripten::function("LlamarMainOriginal",&LlamarMainOriginal);	
	emscripten::function("get_screen_buffer_pointer",&get_screen_buffer_pointer);
	emscripten::function("setKeyState", &setKeyState);
	
	/*	RIC: puente a JS para el disparo condicional del snapshot de heap.
		
		Desde script.js (NW.js) se puede, por ejemplo:
		
			- MakecodeArcadeVMengine.setGCSnapshotInterval(50)  -> volcar cada 50 gc(0)
			
			- MakecodeArcadeVMengine.requestHeapSnapshot()      -> volcar en el proximo gc(0)
				(util atado a una tecla de depuracion o a un evento del juego que se
				detecte desde JS, ej. leyendo `globals` o el estado de pantalla)
				
			- MakecodeArcadeVMengine.isHeapSnapshotHalted()     -> saber si la VM ya
				se congelo tras un volcado, para dejar de llamar a renderFrameFromWasm()
	*/
	emscripten::function("setGCSnapshotInterval",&::setGCSnapshotInterval);
	emscripten::function("requestHeapSnapshot",&::requestHeapSnapshot);
	emscripten::function("setHaltAfterSnapshot",&::setHaltAfterSnapshot);
	emscripten::function("isHeapSnapshotHalted",&::isHeapSnapshotHalted);
	emscripten::function("SetFunctionIDsIgnore",&::SetFunctionIDsIgnore);
	
	/*	puente del simulador TFT ST7735
	*/
	emscripten::function("wokwi_configure",&wokwi_configure);	
	emscripten::function("wokwi_chip_init",&wokwi_chip_init);
	emscripten::function("wokwi_reset",&wokwi_reset);
	emscripten::function("wokwi_write_command",&wokwi_write_command);	
	emscripten::function("wokwi_write_data",&wokwi_write_data);
	emscripten::function("wokwi_get_framebuffer",&wokwi_get_framebuffer);
	emscripten::function("wokwi_get_width",&wokwi_get_width);
	emscripten::function("wokwi_get_height",&wokwi_get_height);
	
}

