#include "pxt.h"

#ifndef __EMSCRIPTEN__
	#include "esp_system.h"
	#include "esp_log.h"
	#include "driver/gpio.h"
	#include "driver/ledc.h"
	#include "esp_private/system_internal.h"
	#include "esp_random.h"
#endif

static void reset_pin(PinName p) {
    if (p != -1)
        gpio_set_direction((gpio_num_t)p, GPIO_MODE_DISABLE);
}

void reboot_to_uf2(void) {
    reset_pin(PIN(LED_R));
    reset_pin(PIN(LED_G));
    reset_pin(PIN(LED_B));

#if CONFIG_IDF_TARGET_ESP32S2
    // call esp_reset_reason() is required for idf.py to properly links esp_reset_reason_set_hint()
    (void)esp_reset_reason();
    esp_reset_reason_set_hint((esp_reset_reason_t)0x11F2);
#endif

    esp_restart();
}

namespace pxt {

// RIC
static void initRandomSeed()
{
	// usa ESP-IDF para generar la semilla de random
    seedRandom(esp_random());
}

// RIC
extern "C" void drawPanic(int code)
{
	printf("RIC debug : drawPanic( %d ) TODO\n",code);
	// TODO
}


/*	Gestion de los GPIO (para botones)

	Usa ISR (interrupciones) para facilitar el tratamiento, y no hacerlo en un loop ...
	
	EJ:
	
			static void IRAM_ATTR button_isr(void *arg)
			{
				printf("Interrupción\n");
			}

			void app_main(void)
			{
				gpio_config_t io_conf = {
					.pin_bit_mask = (1ULL << GPIO_NUM_0),
					.mode = GPIO_MODE_INPUT,
					.pull_up_en = true,
					.intr_type = GPIO_INTR_NEGEDGE
				};

				gpio_config(&io_conf);

				gpio_install_isr_service(0);

				gpio_isr_handler_add(GPIO_NUM_0, button_isr, NULL);
			}
	
*/
#if CONFIG_IDF_TARGET_ESP32S2
	RIC por aqui NO entra

static void bootloader_handler(void *) {
    reboot_to_uf2();
}

void install_gpio0_handler() {
    DMESG("reset reason: %x", esp_reset_reason_get_hint());
    gpio_install_isr_service(0);
    gpio_config_t gpio_conf;
    gpio_conf.intr_type = GPIO_INTR_POSEDGE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (1ULL << GPIO_NUM_0);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_conf);
    gpio_isr_handler_add(GPIO_NUM_0, bootloader_handler, NULL);
}
#else

	// RIC por aqui entra
	
	static void bootloader_handler(void *)
	{
		DMESG("ISR GPIO flanco subida en GPIO 0");
	}
	
	void install_gpio0_handler()
	{
	#ifndef __EMSCRIPTEN__
		
		DMESG("reset reason: %x", esp_reset_reason_get_hint());
		
		// activamos poder definir funciones individuales callback por GPIO ...
		gpio_install_isr_service(0);
		
		// OJO los campos deben definirse en el mismo orden de la etsructura (restricciones de versionado de C++)
		gpio_config_t gpio_conf =	{
									.pin_bit_mask = (1ULL << GPIO_NUM_0),
									.mode = GPIO_MODE_INPUT,
									.pull_up_en = GPIO_PULLUP_ENABLE,
									.pull_down_en = GPIO_PULLDOWN_DISABLE,
									.intr_type = GPIO_INTR_POSEDGE
									};
		gpio_config(&gpio_conf);
		gpio_isr_handler_add(GPIO_NUM_0, bootloader_handler, NULL);
		
	#endif	
	}
	
#endif

extern "C" void usb_init();
extern "C" void target_init()
{
    initRandomSeed();
	
    memInfo();
	
	// RIC de momento anulo
    //usb_init();
}

	// aqui estan solo para definirlas porque esta en otro fichero
	void updateScreen(Image_ img);
	void st7735_hal_init(void);		// definida en screen---ext/screen.cpp (real en ESP32, FAKE en Emscripten)
void screen_init()
{
	// inicializa pines de control, bus SPI (o su simulacion FAKE en Emscripten) y el propio controlador ST7735
	#ifndef EMSCRIPTEN_SIN_EMULACION_TFT
		st7735_hal_init();
	#endif
	
	// RIC al pasar NULL, esta funcion no llega ha hacer nada , ???
    updateScreen(NULL);
}

void memInfo()
{
	// heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    DMESG("mem: %d free (%d total)",heap_caps_get_free_size(MALLOC_CAP_DEFAULT),heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
}

void (*logJDFrame)(const uint8_t *data);
void (*sendJDFrame)(const uint8_t *data);

} // namespace pxt
