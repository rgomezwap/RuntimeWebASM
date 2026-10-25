
#ifndef ESP_IDF_FAKE_H
	#define ESP_IDF_FAKE_H


#if defined(__cplusplus)
	#define cppexport			extern "C"						// to prevent C++ name mangling (si no me cambia los nombres de funciona, o algo asi, porque el em++ no encuetra las funciones exportadas ...
#else
	#define cppexport
#endif		


// para usar los tipos int64_t , etc ...
#include <stdint.h>


// main\platform.h	-> extern TaskHandle_t userCodeTask;
struct tskTaskControlBlock
	{
    int dummy;
	};
typedef struct tskTaskControlBlock * TaskHandle_t;


// main\platform.h	-> #define PXT_IN_ISR() (xTaskGetCurrentTaskHandle() != userCodeTask)
TaskHandle_t xTaskGetCurrentTaskHandle( void );


// main\pintarget.h
int64_t esp_timer_get_time(void);


// main\pxt.h
typedef void (*TaskFunction_t)( void * );


// main/core---esp32/codalemu.cpp	-> IRAM_ATTR void target_wait_us(uint32_t us)
#define IRAM_ATTR


// main/core---esp32/codalemu.cpp	-> static portMUX_TYPE global_int_mux = portMUX_INITIALIZER_UNLOCKED;
#define SPINLOCK_FREE 0xB33FC33F
typedef struct
	{
	uint32_t owner;
	} spinlock_t;
typedef struct
	{
    spinlock_t owner;
    uint32_t count;
	} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED		{ .owner = SPINLOCK_FREE, .count = 0 }


// main/core---esp32/codalemu.cpp	-> vPortEnterCritical(&global_int_mux);
void vPortEnterCritical( portMUX_TYPE *mux );
void vPortExitCritical( portMUX_TYPE *mux );


// main/core---esp32/crypto.cpp
typedef unsigned long size_t;
typedef struct mbedtls_sha256_context
	{
    uint32_t total[2];          /* Número total de bytes procesados (contador de 64 bits) */
    uint32_t state[8];          /* Estado intermedio del algoritmo (los 8 registros A-H) */
    unsigned char buffer[64];   /* Buffer interno para acumular bloques de 64 bytes (512 bits) */
    int is224;                  /* Flag: 0 para SHA-256, 1 para SHA-224 */
	} mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *input,size_t ilen);
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,unsigned char output[32]);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);


// main/core---esp32/dmesg.cpp
int ets_printf(const char *fmt, ...);
#define LOG_FORMAT(letter, format)			""
uint32_t esp_log_timestamp(void);
void ESP_LOGW(char *str,char *format,void *);


// main/core---esp32/pins.cpp
typedef int esp_err_t;
esp_err_t esp_task_wdt_init(uint32_t timeout_s, bool panic);
esp_err_t esp_task_wdt_add(TaskHandle_t handle);
esp_err_t esp_task_wdt_delete(TaskHandle_t handle);
esp_err_t esp_task_wdt_reset(void);


// main/core---esp32/pintarget.cpp
typedef enum
	{
    GPIO_MODE_DISABLE = 0,         /*!< Modo deshabilitado */
    GPIO_MODE_INPUT = 1,           /*!< Solo entrada */
    GPIO_MODE_OUTPUT = 2,          /*!< Solo salida */
    GPIO_MODE_OUTPUT_OD = 3,       /*!< Salida con drenador abierto (Open Drain) */
    GPIO_MODE_INPUT_OUTPUT_OD = 4, /*!< Entrada y Salida con drenador abierto */
    GPIO_MODE_INPUT_OUTPUT = 5,    /*!< Entrada y Salida */
	} gpio_mode_t;
typedef enum
	{
    GPIO_PULLUP_DISABLE = 0x0,     /*!< Deshabilitar resistencia Pull-up interna */
    GPIO_PULLUP_ENABLE = 0x1,      /*!< Habilitar resistencia Pull-up interna */
	} gpio_pullup_t;
typedef enum
	{
    GPIO_PULLDOWN_DISABLE = 0x0,   /*!< Deshabilitar resistencia Pull-down interna */
    GPIO_PULLDOWN_ENABLE = 0x1,    /*!< Habilitar resistencia Pull-down interna */
	} gpio_pulldown_t;
typedef enum
	{
    GPIO_NUM_NC = -1,    /*!< Pin no conectado */
    GPIO_NUM_0 = 0,     /*!< GPIO0 */
    GPIO_NUM_1 = 1,     /*!< GPIO1 */
    // ...
    GPIO_NUM_MAX,
	} gpio_num_t;
typedef enum
	{
    GPIO_INTR_DISABLE = 0,     /*!< Interrupción deshabilitada */
    GPIO_INTR_POSEDGE = 1,     /*!< Flanco de subida (0V -> 3.3V / Positive Edge) */
    GPIO_INTR_NEGEDGE = 2,     /*!< Flanco de bajada (3.3V -> 0V / Negative Edge) */
    GPIO_INTR_ANYEDGE = 3,     /*!< Cualquier flanco (Cambio de estado: 0->1 o 1->0) */
    GPIO_INTR_LOW_LEVEL = 4,   /*!< Nivel bajo continuo (Mientras el pin esté en 0V) */
    GPIO_INTR_HIGH_LEVEL = 5,  /*!< Nivel alto continuo (Mientras el pin esté en 3.3V) */
    GPIO_INTR_MAX,
	} gpio_int_type_t;
typedef struct
	{
    uint64_t pin_bit_mask;          /*!< Máscara de bits de los pines a configurar (p. ej. BIT64(gpio_num)) */
    gpio_mode_t mode;               /*!< Modo del GPIO: Entrada, Salida, etc. */
    gpio_pullup_t pull_up_en;       /*!< Habilitar/Deshabilitar resistencia Pull-up */
    gpio_pulldown_t pull_down_en;   /*!< Habilitar/Deshabilitar resistencia Pull-down */
    gpio_int_type_t intr_type;      /*!< Tipo de interrupción por GPIO */
	} gpio_config_t;
esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int gpio_get_level(gpio_num_t gpio_num);
esp_err_t gpio_config(const gpio_config_t *pGPIOConfig);


// main/core---esp32/platform.cpp
int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
int gpio_install_isr_service(int arg);
#define MALLOC_CAP_DEFAULT (1 << 0)
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
void esp_restart(void);
uint32_t esp_random(void);


// main/screen---ext/screen.cpp -> heap_caps_malloc(TFT_DMA_BUFFER_TOTAL_BYTES, MALLOC_CAP_DMA)
// en Emscripten no hay memoria "DMA capable" de verdad: es simplemente malloc() normal,
// pero se deja el mismo nombre/firma para que CalculateDMALayout()/st7735_hal_init()
// (ahora compartidos entre ESP32 real y el simulador) no necesiten ningun #ifdef.
#define MALLOC_CAP_DMA (1 << 1)
void *heap_caps_malloc(size_t size, uint32_t caps);


// main/core---esp32/scheduler.cpp
typedef uint32_t TickType_t;
void vTaskDelay(const TickType_t xTicksToDelay);
#define portMAX_DELAY			0xFFFFFFFFUL
#define configMAX_PRIORITIES	25
#define pdMS_TO_TICKS(ms)		(ms)


// main/core---esp32/target.cpp
esp_err_t esp_efuse_mac_get_default(uint8_t mac[6]);


// main/core---esp32/usb.cpp
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"


// main/core---esp32/worker.cpp
#define PRO_CPU_NUM 0
#define APP_CPU_NUM 1
#define pdFALSE 0
#define pdTRUE  1
#define pdPASS  1
#define pdFAIL  0
typedef void* QueueHandle_t;
typedef QueueHandle_t xQueueHandle; // Retrocompatibilidad para 'xQueueHandle'
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue, BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,const char * const pcName,const uint32_t usStackDepth,void * const pvParameters,UBaseType_t uxPriority,TaskHandle_t * const pvCreatedTask,const BaseType_t xCoreID);


/*	main/screen---ext/screen.cpp -> semaforo "frame libre" (xSemaphoreCreateBinary/Take/Give)
	que st7735_core_update_screen() usa para esperar (si hiciera falta) a que el frame
	ANTERIOR haya terminado de enviarse. Es codigo COMPARTIDO con ESP32 real, asi que
	en Emscripten hace falta un fake que de verdad "espere": como aqui no hay un thread
	de verdad, xSemaphoreTake() hace una espera activa cediendo el hilo de JS con
	emscripten_sleep() (por eso el build lleva -sASYNCIFY, ver buildems.js) hasta que
	el temporizador que simula la interrupcion de fin de DMA llama a xSemaphoreGive().
*/
typedef struct tskSemaphoreFake * SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t xTicksToWait);

/*	xSemaphorePeek(): SOLO Emscripten. Consulta si el semaforo esta "dado" (libre)
	SIN tomarlo y sin bloquear jamas. Permite que el llamante (updateScreen(),
	via el scheduler de fibers de pxt) decida NO esperar aqui dentro con
	emscripten_sleep()/ASYNCIFY (fragil: no sobrevive el dispatch de exec_loop()
	ni una llamada expuesta por embind), sino ceder el fiber y reintentar mas
	tarde con el mismo mecanismo que ya usa sleep_ms() (schedule()+wakeTime).
*/
bool xSemaphorePeek(SemaphoreHandle_t sem);


/*	main/screen---ext/screen.cpp -> dma_trans_done_cb()/st7735_dma_worker_task()

	En ESP32 real, vTaskNotifyGiveFromISR() (llamada desde la ISR de fin de DMA)
	despierta a la tarea worker, que sigue esperando en ulTaskNotifyTake(). En el
	simulador NO hay ISR real ni tarea FreeRTOS real (ver xTaskCreatePinnedToCore()
	mas abajo, que no llega a lanzar nada): "despertar la tarea" se resuelve
	ejecutando ya mismo, por su nombre, el paso de trabajo del worker
	(st7735_dma_worker_task_step(), definido en screen.cpp). Esto es valido
	precisamente porque en Emscripten dma_trans_done_cb() NUNCA corre en un
	contexto de interrupcion real, solo en un callback de temporizador.
*/
void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken);
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
#define portYIELD_FROM_ISR()	do { } while (0)

// usadas solo en el DMESG() de arranque de st7735_dma_worker_task() (que en el
// simulador compila pero nunca llega a ejecutarse, ver ESPIDFfake.cpp)
UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask);
int xPortGetCoreID(void);


// main/screen---ext/screen.cpp -> XTHAL_GET_CCOUNT() (medicion de tiempos en ciclos de CPU, solo Xtensa/ESP32 real)
// Fake: fabrica un "contador de ciclos" a partir de esp_timer_get_time() (ya fakeado
// arriba con emscripten_get_now()), escalado a TFT_CPU_FREQ_HZ, para que todo el
// codigo de medicion/logging que ya existia para ESP32 (tft_cycles_to_ms(), etc.)
// compile y funcione igual en el simulador sin ningun cambio.
uint32_t fake_xthal_get_ccount(void);
#define XTHAL_GET_CCOUNT()		fake_xthal_get_ccount()


// main/settings---esp32/settings.cpp
#define ESP_OK                      0
#define ESP_FAIL                   -1
#define ESP_ERR_NVS_BASE            0x1100
#define ESP_ERR_NVS_NO_FREE_PAGES   (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x25)
#define ESP_ERR_NVS_NOT_FOUND       (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)
typedef uint32_t nvs_handle_t;
typedef enum
	{
    NVS_READONLY,
    NVS_READWRITE
	} nvs_open_mode_t;
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char* name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length);
#define NVS_DEFAULT_PART_NAME "nvs"
typedef enum
	{
	NVS_TYPE_U8    = 0x01,
    NVS_TYPE_I8    = 0x11,
    NVS_TYPE_U16   = 0x02,
    NVS_TYPE_I16   = 0x12,
    NVS_TYPE_U32   = 0x04,
    NVS_TYPE_I32   = 0x14,
    NVS_TYPE_U64   = 0x08,
    NVS_TYPE_I64   = 0x18,
    NVS_TYPE_STR   = 0x21,
    NVS_TYPE_BLOB  = 0x41,
    NVS_TYPE_ANY   = 0xff
	} nvs_type_t;
typedef struct
	{
    char key[16];
    nvs_type_t type;
    char namespace_name[16];
	} nvs_entry_info_t;
typedef void* nvs_iterator_t;
nvs_iterator_t nvs_entry_find(const char* part_name, const char* namespace_name, nvs_type_t type);
nvs_iterator_t nvs_entry_next(nvs_iterator_t iterator);
void nvs_entry_info(nvs_iterator_t iterator, nvs_entry_info_t* out_info);
void nvs_release_iterator(nvs_iterator_t iterator);

typedef struct
	{
	size_t length;
	const void *tx_buffer;
	} spi_transaction_t;

typedef void *spi_device_handle_t;


/*	WOKWI related ...
*/

extern uint32_t g_fb_width;
extern uint32_t g_fb_height;

#include "wokwi-api.h"

int find_pin(const char *name);

typedef struct
	{
	char name[16];
	uint32_t mode;
	uint32_t value;
	bool watched;
	pin_watch_config_t watch;
	} pin_record_t;
extern pin_record_t g_pins[];

cppexport void wokwi_set_pin(const char *name, uint32_t value);
cppexport void wokwi_write_command(uint8_t cmd);
void wokwi_write_data_raw(const uint8_t *data, uint32_t len);

#endif				// ESP_IDF_FAKE_H

