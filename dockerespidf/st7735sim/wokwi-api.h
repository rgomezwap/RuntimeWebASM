/*
 * wokwi-api.h  --  Emscripten-compatible drop-in replacement.
 *
 * Mismas firmas/tipos que el wokwi-api.h original, PERO sin los
 * atributos import_name/export_name del runtime de Wokwi: aquí son
 * funciones normales, implementadas en wokwi-shim.c para correr
 * bajo Emscripten. Esto permite compilar st7735-wokwi-chip-main.c
 * SIN modificarlo.
 */
#include <stdint.h>
#include <stdbool.h>

#ifndef WOKWI_API_H
#define WOKWI_API_H

enum pin_value {
  LOW = 0,
  HIGH = 1
};

enum pin_mode {
  INPUT = 0,
  OUTPUT = 1,
  INPUT_PULLUP = 2,
  INPUT_PULLDOWN = 3,
  ANALOG = 4,

  OUTPUT_LOW = 16,
  OUTPUT_HIGH = 17,
};

enum edge {
  RISING = 1,
  FALLING = 2,
  BOTH = 3,
};

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t pin_t;
#define NO_PIN ((pin_t)-1)

typedef struct {
  void *user_data;
  uint32_t edge;
  void (*pin_change)(void *user_data, pin_t pin, uint32_t value);
} pin_watch_config_t;

/* Punto de entrada del chip (llamado por nuestro puente, no por Wokwi) */
void chip_init(void);

pin_t pin_init(const char *name, uint32_t mode);

uint32_t pin_read(pin_t pin);
void pin_write(pin_t pin, uint32_t value);
bool pin_watch(pin_t pin, const pin_watch_config_t *config);
void pin_watch_stop(pin_t pin);
void pin_mode(pin_t pin, uint32_t value);
float pin_adc_read(pin_t pin);
float pin_dac_write(pin_t pin, float voltage);

uint32_t attr_init(const char *name, uint32_t default_value);
uint32_t attr_init_float(const char *name, float default_value);
uint32_t attr_read(uint32_t attr_id);
float attr_read_float(uint32_t attr_id);

typedef struct {
  void *user_data;
  uint32_t address;
  pin_t scl;
  pin_t sda;
  bool (*connect)(void *user_data, uint32_t address, bool connect);
  uint8_t (*read)(void *user_data);
  bool (*write)(void *user_data, uint8_t data);
  void (*disconnect)(void *user_data);
  uint32_t reserved[8];
} i2c_config_t;

typedef uint32_t i2c_dev_t;

i2c_dev_t i2c_init(const i2c_config_t *config);

typedef struct {
  void *user_data;
  pin_t rx;
  pin_t tx;
  uint32_t baud_rate;
  void (*rx_data)(void *user_data, uint8_t byte);
  void (*write_done)(void *user_data);
  uint32_t reserved[8];
} uart_config_t;

typedef uint32_t uart_dev_t;

uart_dev_t uart_init(const uart_config_t *config);
bool uart_write(uart_dev_t uart, uint8_t *buffer, uint32_t count);

typedef struct {
  void *user_data;
  pin_t sck;
  pin_t mosi;
  pin_t miso;
  uint32_t mode;
  void (*done)(void *user_data, uint8_t *buffer, uint32_t count);
  uint32_t reserved[8];
} spi_config_t;
typedef uint32_t spi_dev_t;

spi_dev_t spi_init(const spi_config_t *spi_config);
void spi_start(const spi_dev_t spi, uint8_t *buffer, uint32_t count);
void spi_stop(const spi_dev_t spi);

/* Nota: el timer_t/timer_init originales de Wokwi se han omitido a
   propósito: el chip ST7735 no los usa, y "timer_t" choca con el
   tipo POSIX del mismo nombre que arrastra <stdlib.h>/<sys/types.h>
   bajo Emscripten/musl. Si tu chip los necesitase, renómbralos
   (p.ej. wokwi_timer_t) al añadirlos aquí y en wokwi-shim.c. */

typedef uint32_t buffer_t;
buffer_t framebuffer_init(uint32_t *pixel_width, uint32_t *pixel_height);
void buffer_read(buffer_t buffer, uint32_t offset, uint32_t *data, uint32_t data_len);
void buffer_write(buffer_t buffer, uint32_t offset, uint32_t *data, uint32_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* WOKWI_API_H */
