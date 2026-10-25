
#include "pxt.h"

#ifdef __EMSCRIPTEN__
	#include <emscripten.h>
	#include "ESPIDFfake.h"
#else
	#include "driver/gpio.h"
	#include "driver/spi_master.h"
	#include "esp_heap_caps.h"				// heap_caps_malloc() / MALLOC_CAP_DMA para los buffers DMA
	#include "xtensa/core-macros.h"			// XTHAL_GET_CCOUNT() para medir tiempos con muy poco overhead
	
		// para trabajar con interrupciones
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"				// tarea worker que continúa el envío DMA por interrupción
	#include "freertos/semphr.h"			// semáforo "frame libre" que espera updateScreen()	
#endif

namespace pxt
{

// heredamos del CRC Game Engine	
extern ESTRUCTURA_PRECALCULATED_SCENE precalculatedscene;
	
static spi_device_handle_t s_spi = NULL;


/*	Funciones de tratamiento del formato CRCrsc type 19 Multipaleta
*/

/*	imagen multipaleta (recurso CRC tipo 19).

	Cuando hay un recurso activo, cualquier pixel de SALIDA cuyo nibble de
	Arcade sea 0 (transparente) se sustituye, en compute_pixel_bytes(), por el
	color real que le corresponda dentro de esta imagen, en vez de usar
	s_palette565[0]/s_palette888[0]. El buffer llega UNA sola vez desde TS
	(TileMapData.setCRC19Resource -> image.setMultipaletteBackground) y
	queda en memoria de Arcade (NO lo copiamos ni liberamos aqui). El
	offset de camara se actualiza una vez por frame, antes de pintar,
	desde image.setMultipaletteBackgroundOffset().

	Formato del recurso (ver PNGtoCRCresourceType19.js):
		[0..1]  uint16 LE  tipo (19)
		[2..3]  uint16 LE  ancho
		[4..5]  uint16 LE  alto
		[6..133]           paleta global: 64 x uint16 LE (16 minipaletas de 4 colores)
		[134..]            tabla paleta-por-tile: 4 bits/tile, (ancho/4)*(alto/4)/2 bytes
		[..fin]            pixeles a 2bpp: (ancho/4)*alto bytes (4 pixeles/byte)
*/
static const uint8_t       *s_mpBuf  	= nullptr;
static int                s_mpWidth    	= 0;
static int               s_mpHeight   	= 0;
static const uint16_t  *s_mpPalette    	= nullptr;	// 64 colores, formato propio del generador (crudo, tal cual llega del recurso)
static const uint8_t   *s_mpTilePal    	= nullptr;	// 4 bits/tile
static const uint8_t    *s_mpPixels   	= nullptr;	// 2bpp
static int              s_mpOffsetX    	= 0;
static int              s_mpOffsetY    	= 0;
	// referente a los MapPaths
const uint8_t       	  *s_mpathb  	= nullptr;
	/*	Mascara de FOREGROUND (1bpp). Cada bit = una celda 2x2 del fondo multipaleta.
		Bit a 1 -> esos 4 pixeles se pintan SIEMPRE con el fondo multipaleta, ignorando
		el framebuffer de Arcade (zonas en primer plano que tapan todo).
		Mismo offset de camara que el fondo (s_mpOffsetX/Y). El buffer es de Arcade
		(no se copia ni se libera aqui). Formato: sin cabecera, (s_mpWidth/2)x(s_mpHeight/2)
		bits, row-major sin padding por fila, MSB primero (igual que los MapPaths).
	*/
static const uint8_t 	  *s_fgMask  	= nullptr;
static int            	  s_fgMaskW 	= 0;		// (s_mpWidth  >> 1)
static int            	  s_fgMaskH 	= 0;		// (s_mpHeight >> 1)
static size_t		     s_fgStride 	= 0;   		// bytes por fila = ceil(W/8)
static int              s_fgCacheCy   	= -1;		// cache de fila de celdas
static size_t         s_fgCacheBase 	= 0;
	// referente al detalle de las SPECIAL ZONES de esta SCENE
const uint8_t 			 *s_mpathsp		= nullptr;

// sistema de DEBUG visual SOLO en EMSCRIPTEN
#ifdef __EMSCRIPTEN__
	
	static int          s_mpathWidth   = 0;		// ancho en PIXELS del recurso MapPaths (cabecera tipo 14)
	static int          s_mpathHeight  = 0;		// alto en PIXELS

	/*	Modo "ver MapPaths": cuando esta activo, los pixeles de fondo (nibble 0, los que
		normalmente salen del fondo multipaleta) se pintan reinterpretando el MapPaths:
		cada celda de 2x2 pixels = 2 bits -> 0 blanco, 1 magenta, 2 rojo, 3 verde.
		Se activa/desactiva a voluntad con setCRCshowMapPaths(). Usa el MISMO offset de
		camara que el fondo multipaleta (s_mpOffsetX/Y), asi que ambos van alineados.
	*/
	static bool         s_showMapPaths = EMSCRIPTEN_DEBUG_MAPPATHS;
	static const int    MPATH_CELL     = 2;		// celda de 2x2 pixels

	// Los 4 colores en RGB565 estandar y ya con byte-swap (camino rapido x2) - indices 0..3
	static const uint16_t s_pathColorStd[4]  = { 0xFFFF, 0xF81F, 0xF800, 0x07E0 };	// blanco, magenta, rojo, verde
	static const uint16_t s_pathColorWire[4] = { 0xFFFF, 0x1FF8, 0x00F8, 0xE007 };	// idem con bytes intercambiados

	// cache de fila para el MapPaths (misma idea que s_mpCacheWy)
	static int          s_pathCacheWy      = -1;
	static int          s_pathCacheRowBase = 0;
	
	// Resuelve la POSICION (srcX, srcY) al valor 0..3 del MapPaths (o -1 si cae fuera).
	static inline int mapPathsValueAt(int srcX, int srcY)
	{
		if (!s_mpathb) return -2;

		int wx = srcX + s_mpOffsetX;
		int wy = srcY + s_mpOffsetY;
		
		if (wx < 0 || wy < 0) return -3;                       // offset negativo
		if (wx >= s_mpathWidth)  return -4;   // azul   -> falla X
		if (wy >= s_mpathHeight) return -5;   // cian   -> falla Y
		
		if (wy != s_pathCacheWy)
			{
			// una multiplicacion solo al cambiar de fila de celdas
			s_pathCacheRowBase = (wy / MPATH_CELL) * (s_mpathWidth / MPATH_CELL);
			s_pathCacheWy      = wy;
			}

		int indice = s_pathCacheRowBase + (wx / MPATH_CELL);		// mismo indice que GMA_MapaPathsFromIndex()

		return (s_mpathb[indice >> 2] >> (6 - ((indice & 3) << 1))) & 0x03;
	}
	
#endif

/*	s_mpPaletteStd/s_mpPaletteWire - las mismas 64 combinaciones de
	s_mpPalette, pero YA resueltas al color final, calculadas UNA sola vez
	en setMultipaletteBackground() (cuando llega el recurso), no por pixel:

	  - s_mpPaletteStd[i]  : RGB565 ESTANDAR (r<<11|g<<5|b), para el camino
							 general (RGB888/666 y RGB565 via compute_pixel_bytes,
							 que extraen los bits ellos mismos).
	  - s_mpPaletteWire[i] : el mismo color, con el byte-swap YA hecho (MSB
							 primero), listo para escribirse directo como
							 uint16_t en el buffer de salida -> usado por el
							 camino rapido zoom x2 RGB565 (fill_row565_from_*),
							 que asi no repite "(v<<8)|(v>>8)" en cada pixel
							 de fondo multipaleta.

	Antes esta conversion (deshacer el swap del generador + reordenar bits +
	swap final) se repetia en CADA pixel de fondo distinto; con solo 64
	combinaciones posibles, sale mucho mas barato resolverlas todas una vez
	al cargar el recurso y despues solo indexar.
*/
static uint16_t s_mpPaletteStd[64];
static uint16_t s_mpPaletteWire[64];

/*	Cache de la ULTIMA fila (wy) resuelta: dentro de un mismo frame, el barrido
	de salida SIEMPRE avanza wy de forma NO decreciente (viene de LUT_Y, que es
	monotona), tanto si se llama desde dma_fill_direct como desde
	dma_fill_pending. Por eso los dos calculos que dependen SOLO de la fila
	(tileRowBase = multiplicacion, y el puntero a la fila de pixeles) se pueden
	calcular una vez por fila y reutilizar para todos los pixeles de esa fila,
	en vez de recalcularlos (multiplicar+sumar) en cada pixel.
*/
static int             s_mpCacheWy         = -1;
static int             s_mpCacheTileRowBase = 0;
static const uint8_t  *s_mpCacheRowPtr     = nullptr;

// Resuelve la POSICION (srcX, srcY) al indice 0..63 dentro de la paleta
// multipaleta (o -1 si cae fuera del area de fondo). Es lo UNICO que depende
// de la posicion; la resolucion de ese indice a color ya esta precalculada
// (ver s_mpPaletteStd/s_mpPaletteWire arriba).
static inline int multipaletteIndexAt(int srcX, int srcY)
{
	int wx = srcX + s_mpOffsetX;
	int wy = srcY + s_mpOffsetY;

	if (wx < 0 || wy < 0 || wx >= s_mpWidth || wy >= s_mpHeight) return -1;		// fuera del area del fondo -> negro

	if (wy != s_mpCacheWy)
		{
		// solo se multiplica al cambiar de fila (una vez cada ~TFT_SRC_WIDTH pixeles), no en cada pixel
		s_mpCacheTileRowBase = (wy >> 2) * (s_mpWidth >> 2);
		s_mpCacheRowPtr      = s_mpPixels + (size_t)wy * (s_mpWidth >> 2);
		s_mpCacheWy          = wy;
		}

	int tileIndex = s_mpCacheTileRowBase + (wx >> 2);							// ya solo suma/shift

	uint8_t tileByte   = s_mpTilePal[tileIndex >> 1];
	uint8_t paletteNum = (tileIndex & 1) ? (tileByte & 0x0F) : (tileByte >> 4);

	uint8_t pixByte = s_mpCacheRowPtr[wx >> 2];							// puntero de fila ya calculado -> solo shift
	uint8_t shift   = 6 - ((wx & 3) << 1);									// pixel 0 -> bits 6-7 (ver generador)
	uint8_t pixIdx  = (pixByte >> shift) & 0x03;

	return (paletteNum << 2) + pixIdx;
}

// Color en formato RGB565 ESTANDAR para el camino general (RGB888/666 y
// RGB565 "normal", que extraen los bits ellos mismos en compute_pixel_bytes()).
static inline uint16_t multipaletteRGB565At(int srcX, int srcY)
{

	// en EMSCRIPTEN tengo un DEBUG visual de ver los MapPaths
	#ifdef __EMSCRIPTEN__
		if (s_showMapPaths)
			{
			int v = mapPathsValueAt(srcX, srcY);
			if (v == -2) return 0xF81F;   // magenta -> s_mpathb nulo
			if (v == -3) return 0xFFE0;   // amarillo -> offset negativo
			if (v == -4) return 0x001F;   // azul -> fuera de rango X
			if (v == -5) return 0x07FF;	  // cian -> fuera de rango Y
			if (v <  0)  return 0x0000;
			return s_pathColorStd[v];
			}
	#endif

	int idx = multipaletteIndexAt(srcX, srcY);
	return (idx < 0) ? 0x0000 : s_mpPaletteStd[idx];
}

// Mismo color, pero YA con el byte-swap hecho -> para el camino rapido
// zoom x2 RGB565 (fill_row565_from_direct/pending), sin ningun calculo
// aparte de la busqueda de indice + un acceso a tabla.
static inline uint16_t multipaletteRGB565WireAt(int srcX, int srcY)
{
#ifdef __EMSCRIPTEN__
	if (s_showMapPaths)
		{
		int v = mapPathsValueAt(srcX, srcY);
		return (v < 0) ? 0x0000 : s_pathColorWire[v];
		}
#endif

	int idx = multipaletteIndexAt(srcX, srcY);
	return (idx < 0) ? 0x0000 : s_mpPaletteWire[idx];
}

static inline bool foregroundAt(int srcX, int srcY)
{
	if (!s_fgMask) return false;

	int wx = srcX + s_mpOffsetX;
	int wy = srcY + s_mpOffsetY;
	if (wx < 0 || wy < 0) return false;

	int cx = wx >> 1;
	int cy = wy >> 1;
	if (cx >= s_fgMaskW || cy >= s_fgMaskH) return false;

	if (cy != s_fgCacheCy)
		{
		s_fgCacheBase = (size_t)cy * s_fgStride;     		// ya en BYTES
		s_fgCacheCy   = cy;
		}
	
	return (s_fgMask[s_fgCacheBase + (cx >> 3)] >> (7 - (cx & 7))) & 1;
}

static inline bool useBackground(uint8_t nibble, int x, int y)
{
	/*	Decision unica: este pixel se pinta con el fondo multipaleta?
		(nibble 0 = transparente, o celda marcada como foreground)
	*/
	return s_mpBuf && (nibble == 0 || foregroundAt(x, y));
}

// Codigo especifico EMSCRIPTEN para simular SPI y DMA de ESP-IDF
#ifdef __EMSCRIPTEN__
		
	// para renderizar en HTML Canvas directamente en RGBA
	extern uint8_t *screenBuf;
	extern uint32_t currPalette[16];
	
	
	/*	Simulamos funcionamiento SPI de ESP-IDF con transacciones e interrupciones ...
	*/
		
	static inline esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t *t)
	{
		
		(void)handle;
		
		size_t nbytes = t->length / 8;
		
		const uint8_t *buf = (const uint8_t *)t->tx_buffer;
		
		if (g_pins[ find_pin("DC") ].value == 0)
			{
			wokwi_write_command(buf[0]); 			// OJO por convención de uso en 1 byte
			}
		else
			{
			wokwi_write_data_raw(buf, (uint32_t)nbytes);
			}
			
		return ESP_OK;
	
	}
	
	/*	variables para simular interrupciones :
	
		Guardamos simplemente CUANDO (tiempo real) deberia terminar la transaccion, y
		st7735_poll_fake_dma() -llamada UNA vez por vuelta desde mainRunLoop(), dentro
		del mismo hilo de ejecucion de siempre- mira si ya toca y, si es asi,
		llama a dma_trans_done_cb() directamente.
	*/
	static spi_transaction_t *s_fakeDmaPendingTrans = NULL;
	static uint64_t           s_fakeDmaDueAtUs      = 0;

		// la definimos porque esta mas abajo y la usamos aqui
		static void IRAM_ATTR dma_trans_done_cb(spi_transaction_t *trans);
	void st7735_poll_fake_dma(void)
	{
		if (!s_fakeDmaPendingTrans) return;
		
		if ((uint64_t)esp_timer_get_time() < s_fakeDmaDueAtUs) return;
		
		spi_transaction_t *t = s_fakeDmaPendingTrans;
		s_fakeDmaPendingTrans = NULL;
		
		dma_trans_done_cb(t);
	}
	
	// duracion simulada (us) de una transaccion DMA; se define mas abajo (necesita TFT_OUT_WIDTH, s_rectMode...)
	static inline uint64_t sim_dma_time_us(const spi_transaction_t *t);

	static inline esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t *t,TickType_t ticks_to_wait)
	{
		(void)handle; (void)ticks_to_wait;
		
		size_t nbytes = t->length / 8;
		wokwi_write_data_raw((const uint8_t *)t->tx_buffer, (uint32_t)nbytes);
		
		s_fakeDmaPendingTrans = t;
		s_fakeDmaDueAtUs      = (uint64_t)esp_timer_get_time() + sim_dma_time_us(t);
		
		return ESP_OK;
	}
	
	static inline esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,spi_transaction_t **trans_desc,TickType_t ticks_to_wait)
	{
		(void)handle; (void)ticks_to_wait;
		
		if (trans_desc) *trans_desc = NULL;					// nadie lo usa en el codigo compartido, solo hace falta liberar el 'slot'
		
		return ESP_OK;
	}
	
#endif

/*	Funciones para tratamiento del Hardware de TFT
*/

/*	TFT_OUT_WIDTH / TFT_OUT_HEIGHT: tamaño real de salida (con zoom ya
	aplicado) que se manda por SPI al panel. Cada panel define el suyo
	directamente (ya NO se calcula como SRC*TFT_ZOOM, porque con 400x300 el
	factor es 2.5x, no entero). El escalado píxel a píxel ya NO se hace por
	división/replicación: se usa una LUT precalculada una sola vez (ver
	LUT_X/LUT_Y/build_zoom_luts() más abajo), así que cualquier ratio de
	salida funciona igual de rápido, sea entero o no.

	TFT_ZOOM_NUM/TFT_ZOOM_DEN es solo el factor en forma de fracción, para
	logging (DMESG) y como referencia legible; no se usa para calcular nada.
	  - ST7735 160x128  -> zoom x1   (1/1)
	  - ILI9341 320x240 -> zoom x2   (2/1)
	  - panel 400x300   -> zoom x2.5 (5/2)
*/
#if defined(TFT_ST7735_160x128)
	#define TFT_OUT_WIDTH		160
	#define TFT_OUT_HEIGHT		128
	#define TFT_ZOOM_NUM		1
	#define TFT_ZOOM_DEN		1
#elif defined(TFT_ILI9341_320x240)
	#define TFT_OUT_WIDTH		320
	#define TFT_OUT_HEIGHT		240
	#define TFT_ZOOM_NUM		2
	#define TFT_ZOOM_DEN		1
#elif defined(TFT_ILI9488_480x320)				
	#define TFT_OUT_WIDTH		400
	#define TFT_OUT_HEIGHT		300
	#define TFT_ZOOM_NUM		5
	#define TFT_ZOOM_DEN		2
#elif defined(EMSCRIPTEN_SIN_EMULACION_TFT)
	// aunque no se usa la emulacion, declaramos para mantener integridad de estructuras
	#define TFT_OUT_WIDTH		160
	#define TFT_OUT_HEIGHT		128
	#define TFT_ZOOM_NUM		1
	#define TFT_ZOOM_DEN		1
#endif

// Bytes/bits por píxel que se mandan realmente por SPI. La ILI9488, en modo
// SPI de 3/4 hilos, no acepta de forma fiable 16 bits/píxel (RGB565): hay
// que mandar 18 bits/píxel como 3 bytes (R,G,B de 8 bits, el panel usa solo
// los 6 bits altos de cada uno). El resto de paneles siguen en RGB565 (2 bytes).
#if defined(TFT_ILI9488_480x320)
	#define TFT_BYTES_PER_PIXEL	3
	#define TFT_BITS_PER_PIXEL		24
#else
	#define TFT_BYTES_PER_PIXEL	2
	#define TFT_BITS_PER_PIXEL		16
#endif

// Añade la opcion de envio parcial por rectangulos invalidate. Comentar esta linea = siempre frame completo.
#define TFT_PARTIAL_UPDATE

// Dimensiones del frame que se envía por SPI para la simulacion
#define TFT_SCREEN_WIDTH	TFT_OUT_WIDTH
#define TFT_SCREEN_HEIGHT	TFT_OUT_HEIGHT	

// Algunos clones ST7735 ("green/red/black tab") tienen la RAM del panel
// desplazada respecto al área visible. Si la imagen sale corrida unos
// píxeles, ajustar estos offsets (se suman a x,y en setAddrWindow).
#define TFT_XSTART		0
#define TFT_YSTART		0

// Comandos DCS estándar, iguales en ST7735 y en ILI9341
#define ST7735_CASET	0x2A
#define ST7735_RASET   	0x2B
#define ST7735_RAMWR   	0x2C

// paleta ORIGINAL ARCADE MAKECODE en RGB565
static uint16_t s_palette565[16] =	{
									0x0000, // #000000 Transparente
									0xFFFF, // #FFFFFF Blanco
									0xF904, // #FF2121 Rojo
									0xFCB8, // #FF93C4 Rosa
									0xFC06, // #FF8135 Naranja
									0xFFC1, // #FFF609 Amarillo
									0x24F4, // #249CA3 Verde azulado
									0x7EEA, // #78DC52 Verde
									0x01D5, // #003FAD Azul oscuro
									0x87DF, // #87F2FF Azul claro
									0x8978, // #8E2EC4 Morado
									0xA432, // #A4839F Lavanda
									0x620D, // #5C406C Morado oscuro
									0xE676, // #E5CDC4 Beige
									0x9227, // #91463D Marrón
									0x0000  // #000000 Negro			
									};

// Misma paleta que s_palette565, pero con el byte-swap YA hecho (MSB primero,
// como espera el panel). Se calcula UNA sola vez (build_zoom_luts()) porque
// s_palette565 nunca cambia en tiempo de ejecucion en hardware real; asi el
// camino zoom x2 RGB565 (fill_row565_from_direct/pending) no repite el
// "(v<<8)|(v>>8)" en cada pixel de paleta, solo en el de fondo multipaleta
// (bgColor565), que si es dinamico.
static uint16_t s_palette565_be[16];

// Misma paleta que s_palette565, pero en RGB888 (8 bits por canal), para el
// modo 18bpp (TFT_BYTES_PER_PIXEL == 3, ver compute_pixel_bytes()). El panel usa
// solo los 6 bits altos de cada byte; los 2 bajos se ignoran.
static const uint32_t s_palette888[16] =	{
											0x000000, 0xFFFFFF, 0xFF2121, 0xFF93C4,
											0xFF8135, 0xFFF609, 0x249CA3, 0x78DC52,
											0x003FAD, 0x87F2FF, 0x8E2EC4, 0xA4839F,
											0x5C406C, 0xE5CDC4, 0x91463D, 0x000000
											};

/*	Sistema para hacer ZOOM a partir de tablas, y no calculos matematicos

	LUT_X / LUT_Y: mapeo, precalculado UNA sola vez, de cada píxel de SALIDA
	a su píxel de ORIGEN (nearest-neighbor). Sustituye la vieja cuenta
	"ox/oy dividido entre TFT_ZOOM": con zoom no entero (400x300 <- 160x120
	= 2.5x) esa división ya no sirve. En vez de calcularla en caliente por
	cada píxel, se hace UNA vez al arrancar (build_zoom_luts()) y el código
	caliente solo hace LUT_X[ox] / LUT_Y[oy] -> más barato que la propia
	división a la que sustituye, y vale igual para zoom entero (1x/2x salen
	tablas idénticas a "ox/TFT_ZOOM").
*/
static uint8_t LUT_X[TFT_OUT_WIDTH];
static uint8_t LUT_Y[TFT_OUT_HEIGHT];

/*	SRC_ROW_FIRST_OUT[srcRow] = primera línea de SALIDA donde empieza
	'srcRow'. Tamaño TFT_SRC_HEIGHT+1: la última posición (índice
	TFT_SRC_HEIGHT) vale TFT_OUT_HEIGHT ("un pasado el final"), para
	poder usarla también como límite superior sin casos especiales.
*/
static uint16_t SRC_ROW_FIRST_OUT[TFT_SRC_HEIGHT + 1];

/*	SRC_COL_FIRST_OUT[srcCol] = primera columna de SALIDA donde empieza 'srcCol'
	(idem que SRC_ROW_FIRST_OUT pero en horizontal). Tamaño TFT_SRC_WIDTH+1: la
	ultima posicion vale TFT_OUT_WIDTH. Se usa en el envio por rectangulos
	invalidate para pasar un rectangulo de ORIGEN a su rectangulo de SALIDA.
*/
static uint16_t SRC_COL_FIRST_OUT[TFT_SRC_WIDTH + 1];

static void build_zoom_luts(void)
{
	// paleta RGB565 ya con byte-swap hecho (ver s_palette565_be arriba)
	for (int i = 0; i < 16; i++)
		s_palette565_be[i] = (uint16_t)((s_palette565[i] << 8) | (s_palette565[i] >> 8));

	for (int ox = 0; ox < TFT_OUT_WIDTH; ox++)
		LUT_X[ox] = (uint8_t)(((size_t)ox * TFT_SRC_WIDTH) / TFT_OUT_WIDTH);

	for (int oy = 0; oy < TFT_OUT_HEIGHT; oy++)
		LUT_Y[oy] = (uint8_t)(((size_t)oy * TFT_SRC_HEIGHT) / TFT_OUT_HEIGHT);
	
	for (int r = 0; r <= TFT_SRC_HEIGHT; r++) SRC_ROW_FIRST_OUT[r] = (uint16_t)TFT_OUT_HEIGHT;	// centinela = "no alcanzado"
	for (int oy = 0; oy < TFT_OUT_HEIGHT; oy++)
		{
		int row = LUT_Y[oy];
		if (SRC_ROW_FIRST_OUT[row] == (uint16_t)TFT_OUT_HEIGHT) SRC_ROW_FIRST_OUT[row] = (uint16_t)oy;
		}
	
	for (int c = 0; c <= TFT_SRC_WIDTH; c++) SRC_COL_FIRST_OUT[c] = (uint16_t)TFT_OUT_WIDTH;	// centinela
	for (int ox = 0; ox < TFT_OUT_WIDTH; ox++)
		{
		int col = LUT_X[ox];
		if (SRC_COL_FIRST_OUT[col] == (uint16_t)TFT_OUT_WIDTH) SRC_COL_FIRST_OUT[col] = (uint16_t)ox;
		}
}

static inline void dc_cmd(void) 
{
	#ifdef __EMSCRIPTEN__
		gpio_set_level( (gpio_num_t)find_pin("DC") ,0);
	#else
		gpio_set_level((gpio_num_t)TFT_PIN_DC,0);
	#endif	
}

static inline void dc_data(void)
{
	#ifdef __EMSCRIPTEN__
		gpio_set_level( (gpio_num_t)find_pin("DC") ,1);
	#else
		gpio_set_level((gpio_num_t)TFT_PIN_DC,1);
	#endif
}

// DOBLE BUFFER + DMA

struct BUFFER_DMA_LAYOUT
	{
		
	// el buffer DMA se divide en tres partes
	uint8_t* dmaA;        // buffer DMA A (RGB565) (puntero DMA capable)
	uint8_t* dmaB;        // buffer DMA B (RGB565) (puntero DMA capable)
	uint8_t* pending;     // datos 4bpp aún no procesados
	
	/*	valores - OJO con la diferencia SALIDA vs ORIGEN (importante con zoom != 1x):
		
		  - dmaLines      -> líneas de SALIDA (ya con zoom) que caben en CADA
							 buffer ping-pong. dmaSize = dmaLines * (ancho de
							 salida) * 2 bytes.
		  - srcRowsDirect -> filas del framebuffer de ORIGEN (160x120, sin
							 zoom) que quedan cubiertas por dmaA+dmaB juntos,
							 o sea las que se leen DIRECTAS de packed4bpp
							 durante la parte síncrona.
							 Ya no se deriva con TFT_ZOOM (fórmula que solo
							 valía para zoom entero): sale directo del bucle
							 de CalculateDMALayout(), apoyado en la tabla
							 SRC_ROW_FIRST_OUT[] (ver build_zoom_luts()),
							 que vale también para zoom no entero (2.5x).
		  - srcFirstPendingYb -> primer byte-fila (par de filas) del origen
							 que ya NO entra en dmaA+dmaB y por tanto hay que
							 copiar a 'pending'. = srcRowsDirect / 2
	*/
	size_t dmaSize;
	size_t dmaLines;
	size_t srcRowsDirect;
	size_t srcFirstPendingYb;
	size_t pendingSize;
	};
	
BUFFER_DMA_LAYOUT dmalayout;

static uint8_t *s_dmaBigBuffer = NULL;								// único malloc DMA capable; dmalayout.dmaA/dmaB/pending apuntan dentro de él

/*	--- ESTADO ELÁSTICO de los buffers ping-pong ---

	dmalayout.dmaA/dmaB/dmaSize/dmaLines (arriba) son el reparto INICIAL,
	calculado una sola vez en st7735_hal_init() y que ya no cambia nunca.

	A partir de ahí, DURANTE cada frame, dmaA y dmaB van "creciendo" para
	aprovechar los bytes que 'pending' va dejando libres según se procesa.

	REGLA DE ORO (la que hay que respetar sí o sí): entre el instante en
	que un buffer se RELLENA y el instante en que termina de ENVIARSE,
	ni su puntero ni su tamaño pueden cambiar, y ningún otro buffer puede
	escribir dentro de su rango. Con el ping-pong hay siempre un desfase
	de una vuelta (se rellena en una IRQ y se encola en la SIGUIENTE), así
	que mover un puntero "para la próxima vez" justo después de rellenarlo
	hace que se envíe desde una dirección equivocada.

	Por eso aquí NO se desplaza nada a posteriori: cada relleno decide su
	(puntero, tamaño) al principio, los deja fijos en s_bufPtr/s_bufSize, y
	el envío posterior usa exactamente esos mismos valores.

	Reparto en cada relleno (ver elastic_fill): el espacio realmente
	utilizable en ese momento es [dmalayout.dmaA, pending_read_ptr()), o
	sea todo el bloque menos la parte de 'pending' que aún NO se ha
	convertido. Ese espacio se reparte a medias:

	  - dmaA (idx 0) se ancla al PRINCIPIO y crece hacia delante.
	  - dmaB (idx 1) se ancla al FINAL (pegado a 'pending') y crece hacia
		atrás.

	Cada uno se capa además para no invadir al OTRO buffer, que en ese
	momento está en vuelo. Como pending_read_ptr() solo avanza, los dos
	van creciendo frame adentro hasta repartirse el bloque entero.

	Se reinician a los valores de dmalayout en cada frame nuevo
	(elastic_reset_for_new_frame()), llamada desde la parte síncrona de
	st7735_core_update_screen().
*/
static uint8_t *s_bufPtr[2]  = { NULL, NULL };						// dónde se rellenó cada buffer (y desde dónde se enviará)
static size_t   s_bufSize[2] = { 0, 0 };							// bytes rellenados en cada buffer

// puntero al buffer ping-pong 'idx' (0=A, 1=B), ambos dentro de s_dmaBigBuffer
static inline uint8_t *dma_buf_ptr(int idx) { return s_bufPtr[idx]; }

// cuántos bytes RGB565 caben en el buffer ping-pong 'idx' en este relleno
static inline size_t dma_buf_size(int idx) { return s_bufSize[idx]; }

// pone dmaA/dmaB tal como salieron de CalculateDMALayout: se llama al empezar cada frame nuevo
static inline void elastic_reset_for_new_frame(void)
{
	s_bufPtr[0]  = dmalayout.dmaA;
	s_bufSize[0] = dmalayout.dmaSize;
	s_bufPtr[1]  = dmalayout.dmaB;
	s_bufSize[1] = dmalayout.dmaSize;
}

static spi_transaction_t s_dmaTrans[2];
static volatile bool s_dmaTransPending[2] = { false, false };	// accedidas también desde la tarea worker


/*	--- envío de frame ASÍNCRONO, continuado por interrupción de fin de DMA ---

	Idea: updateScreen() copia el frame de MakeCode a un snapshot propio
	(porque en cuanto devuelve el control, Arcade puede empezar a pintar
	el siguiente frame encima), rellena los DOS buffers DMA, encola el
	envío del primero y devuelve el control YA. El segundo buffer queda
	"listo" esperando.

	OJO importante de ESP-IDF: spi_device_queue_trans() / 
	spi_device_get_trans_result() NO son funciones ISR-safe, así que no
	se pueden llamar directamente desde el callback post_cb de la
	transacción (que sí corre en contexto de interrupción). Por eso el
	callback se limita a despertar (vTaskNotifyGiveFromISR) a una tarea
	worker dedicada, de prioridad alta, que es quien de verdad encola el
	buffer siguiente y rellena el que acaba de quedar libre. El efecto
	práctico es el mismo que reaccionar en la propia ISR (la tarea
	despierta casi instantáneamente), pero sin romper las reglas del
	driver SPI.
	
	Ya NO copiamos el frame de MakeCode entero a un snapshot propio.
		- Las primeras 'dmalayout.srcRowsDirect' filas se leen DIRECTAMENTE de
		  packed4bpp (el puntero que nos pasa MakeCode), porque en ese instante
		  todavía es válido: solo lo usamos durante la parte SÍNCRONA de
		  st7735_core_update_screen(), justo antes de devolver el control.
		- El resto de filas ("pending") SÍ hace falta copiarlas a un buffer
		  propio (dmalayout.pending), porque su conversión/envío continúa de
		  forma asíncrona (tarea worker) después de que updateScreen() haya
		  devuelto el control, momento en el que Arcade ya puede haber
		  sobrescrito su framebuffer.		
*/

static const uint8_t *s_frameSrc = NULL;							// framebuffer 4bpp ORIGINAL de MakeCode; solo válido durante la parte síncrona del frame

static int      s_frameWidth = 0, s_frameHeight = 0, s_frameBytesPerCol = 0;
static size_t   s_frameCursor = 0;								// próximo píxel (index lineal y*width+x) a convertir
static size_t   s_frameTotalPixels = 0;

static int      s_sendIdx  = -1;								// buffer actualmente en vuelo (encolado/enviándose), -1 = ninguno
static int      s_readyIdx = -1;								// buffer ya relleno, listo para encolarse en cuanto el otro termine

static TaskHandle_t   s_dmaTaskHandle  = NULL;					// tarea worker que continúa el envío tras cada interrupción de fin de DMA
static SemaphoreHandle_t s_frameFreeSem = NULL;					// "dado" (available) cuando no hay ningún envío de frame en curso

/*	OJO: post_cb (dma_trans_done_cb) también se dispara para transacciones
	POLLING (spi_device_polling_transmit), no solo para las encoladas por
	interrupción. Como st7735_hal_write_command()/write_data() usan polling
	(comandos de init, setAddrWindow, RAMWR...), sin esta bandera el worker
	recibiría notificaciones "fantasma" antes de que exista ningún envío
	DMA real, con s_sendIdx todavía en -1 -> acceso fuera de rango y/o
	bloqueo en spi_device_get_trans_result(). Solo dejamos pasar la
	notificación al worker mientras hay un envío async de verdad en vuelo.
*/
static volatile bool s_asyncBusy = false;

/*	--- medición de tiempos (en ciclos de CPU) ---

	Usamos XTHAL_GET_CCOUNT() (lee directamente el registro CCOUNT del
	Xtensa, 1 instrucción, sin llamada a función ni acceso a periférico)
	en vez de esp_timer_get_time(): es la opción más barata en carga de
	CPU para medir tramos cortos/frecuentes como este. esp_timer_get_time()
	internamente consulta un timer por hardware y tiene bastante más
	overhead; para 4-8 medidas por frame (como aquí) la diferencia de CPU
	es despreciable, pero si en el futuro se quisiera medir por CHUNK en
	vez de por FRAME, CCOUNT es la opción correcta.

	Solo convertimos a mseg. UNA vez, al final del frame (aqui si que no
	importa el coste, es una sola división).
*/
static uint32_t s_dmaWaitStartCycles = 0;						// ciclos que updateScreen() tuvo que esperar a que el frame ANTERIOR terminase de enviarse
	static uint32_t s_frameStartCycles = 0;
	static uint32_t s_lastFrameCycles = 0;
static uint32_t s_cpuCycles = 0;
static uint32_t s_lastCPUCycles = 0;

// Hacemos que un puntero que alineado a DMA CAPABLE (puede ser que saltemos algunos bytes hacia adelante)
static inline uintptr_t AlignUp(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

// idem pero hacia atrás (retroceder dentro de una zona ya libre siempre es seguro)
static inline uintptr_t AlignDown(uintptr_t value, size_t alignment)
{
	return value & ~(uintptr_t)(alignment - 1);
}

/*	OJO formato: el framebuffer 4bpp de MakeCode Arcade es COLUMN-MAJOR,
	con 2 filas empaquetadas por byte (bytesPerCol = ceil(height/2) bytes
	por columna). Por eso 'dmaLines' (que siempre avanza de 2 en 2 filas)
	equivale exactamente a 'dmaLines' bytes consumidos de cada columna.

	En cambio 'pending' NO guarda ese formato: se copia TRASPUESTO a
	ROW-MAJOR (2 columnas por byte, stride = ceil(width/2) bytes por
	fila), para que el relleno de los chunks lo recorra de forma
	secuencial (ver pending_copy_transposed / dma_fill_pending). Por eso
	aquí 'pendingSize' se dimensiona como filas_pendientes * stride.
	
	CalculateDMALayout(): reparte el bloque DMA único entre dmaA, dmaB y 'pending'.

	'width'/'height' son SIEMPRE las del framebuffer de Arcade (160x120,
	sin zoom). El tamaño de los buffers ping-pong, en cambio, va en
	píxeles de SALIDA (con zoom): una línea de salida ocupa
	TFT_OUT_WIDTH píxeles RGB565.

	Se prueba srcRows = 2, 4, 6... (siempre PAR) y para cada uno se lee
	en SRC_ROW_FIRST_OUT[srcRows] cuántas líneas de SALIDA hacen falta
	para cubrir esas filas de origen -ya no se calcula con TFT_ZOOM,
	porque con zoom no entero (2.5x) una fila de origen no siempre dura
	el mismo número de líneas de salida (dura 2 o 3, alternando)-.

	Que 'srcRows' sea entero es imprescindible para poder cortar el frame por la
	frontera "directo / pending" justo entre dos filas de origen; que sea
	par lo es porque 'pending' se copia byte-fila a byte-fila (2 filas por
	byte del formato de MakeCode).		
*/
bool CalculateDMALayout(uint8_t* buffer,size_t bufferSize,int width,int height,struct BUFFER_DMA_LAYOUT &out)
{
	// framebuffer origen 4bpp: bytes por columna (2 filas/byte)
	const size_t bytesPerCol = ((size_t)height + 1) >> 1;
	
	// 'pending' (traspuesto, row-major): bytes por fila (2 columnas/byte)
	const size_t pendingStride = ((size_t)width + 1) >> 1;
	
	// TFT_BYTES_PER_PIXEL bytes/pixel (2=RGB565, 3=RGB666), TFT_OUT_WIDTH píxeles por línea de SALIDA
	const size_t outLineBytes = (size_t)TFT_OUT_WIDTH * TFT_BYTES_PER_PIXEL;

	// vamos probando srcRows = 2,4,6... (siempre PAR, por el empaquetado
	// 2 filas/byte) hasta que no quepa o hasta cubrir el frame entero.
	// tryLines = líneas de SALIDA que cubren exactamente esas 'srcRows'
	// filas de origen, leído de SRC_ROW_FIRST_OUT[] (precalculada en
	// build_zoom_luts()) en vez de derivarlo con TFT_ZOOM: así vale para
	// cualquier factor de zoom, entero o no (con zoom entero da EXACTAMENTE
	// lo mismo que antes, porque SRC_ROW_FIRST_OUT[srcRows] == srcRows*TFT_ZOOM).
	size_t dmaLines = 0;
	size_t srcRowsUsed = 0;

	for (size_t srcRows = 2; srcRows <= (size_t)height; srcRows += 2)
		{

		const size_t tryLines = SRC_ROW_FIRST_OUT[srcRows];

		const size_t pendingRows = (bytesPerCol - (srcRows >> 1)) * 2;
		const size_t pendingSize = pendingRows * pendingStride;
		
		uintptr_t p = (uintptr_t)buffer;

		p = AlignUp(p, 4);
		p += tryLines * outLineBytes;			// dmaA

		p = AlignUp(p, 4);
		p += tryLines * outLineBytes;			// dmaB

		p = AlignUp(p, 4);
		p += pendingSize;						// pending

		const size_t used = (size_t)(p - (uintptr_t)buffer);

		// ya no cabe -> nos quedamos con el srcRows/dmaLines anterior
		if (used > bufferSize) break;

		dmaLines    = tryLines;
		srcRowsUsed = srcRows;

		// ya cabe el frame ENTERO en dmaA+dmaB -> no hace falta más
		if (srcRows >= (size_t)height) break;

		}

	// ni una sola línea cabe -> el buffer DMA es demasiado pequeño
	if (dmaLines == 0) return false;

	const size_t srcRowsDirect = srcRowsUsed;						// filas de ORIGEN leídas directas de packed4bpp
	const size_t firstPendingYb = srcRowsDirect >> 1;				// primer byte-fila del origen que va a 'pending'
	const size_t pendingRows = (bytesPerCol - firstPendingYb) * 2;

	out.dmaLines          = dmaLines;
	out.dmaSize           = dmaLines * outLineBytes;
	out.srcRowsDirect     = srcRowsDirect;
	out.srcFirstPendingYb = firstPendingYb;
	out.pendingSize       = pendingRows * pendingStride;

	uintptr_t p = (uintptr_t)buffer;

	p = AlignUp(p, 4);
	out.dmaA = (uint8_t*)p;
	p += out.dmaSize;

	p = AlignUp(p, 4);
	out.dmaB = (uint8_t*)p;
	p += out.dmaSize;

	p = AlignUp(p, 4);
	out.pending = (uint8_t*)p;

	return true;
}

static inline uint32_t tft_cycles_to_ms(uint32_t cycles)
{
	return (uint32_t)(((uint64_t)cycles * 1000ULL) / TFT_CPU_FREQ_HZ);
}

// espera (si hace falta) a que la transacción del buffer 'idx' haya terminado
static inline void dma_wait(int idx)
{
	if (s_dmaTransPending[idx])
		{
		// RIC DEBUG - de momento este tiempo no lo cuento
		//uint32_t cycWaitStart = XTHAL_GET_CCOUNT();
		
		spi_transaction_t *rtrans;
		spi_device_get_trans_result(s_spi, &rtrans, portMAX_DELAY);
		
		//s_dmaWaitCycles += (XTHAL_GET_CCOUNT() - cycWaitStart);
		s_dmaTransPending[idx] = false;
		}
}

// espera a que AMBOS buffers estén libres (no queda nada pendiente de enviar)
static inline void st7735_hal_dma_wait_idle(void)
{
	dma_wait(0);
	dma_wait(1);
}


/*	--- relleno de los buffers DMA ---

	s_frameCursor recorre el espacio de SALIDA (ya con zoom aplicado,
	TFT_OUT_WIDTH/TFT_OUT_HEIGHT píxeles, sea cual sea el factor -entero
	o no-). El frame se parte en DOS regiones, cada una con su propia
	función de relleno (así el bucle interno no lleva ningún 'if' de
	formato por píxel):

	  - filas de origen [0, srcRowsDirect) -> dma_fill_direct(): se leen
		DIRECTAMENTE de s_frameSrc, que sigue en formato nativo de
		MakeCode (COLUMN-MAJOR, 2 filas por byte). Ese puntero solo es
		válido durante la parte SÍNCRONA de
		st7735_core_update_screen(), que es justo lo que tarda en
		rellenarse dmaA+dmaB, así que nunca se lee fuera de plazo.

	  - filas de origen [srcRowsDirect, height) -> dma_fill_pending(): se
		leen de dmalayout.pending, que ya se copió TRASPUESTO a
		ROW-MAJOR (ver pending_copy_transposed): 2 píxeles horizontales
		por byte, stride = pendingStride bytes por fila.
*/

// bytes por fila en 'pending' (row-major, 2 píxeles/byte)
static inline size_t pending_stride(void) { return ((size_t)s_frameWidth + 1) >> 1; }

/*	Primer píxel de SALIDA que ya pertenece a la región 'pending'.

	Son las 'srcRowsDirect' primeras filas de ORIGEN; SRC_ROW_FIRST_OUT[]
	da directamente en cuántas líneas de salida (de TFT_OUT_WIDTH píxeles
	cada una) se expanden. Con el layout actual ese número coincide con lo
	que cabe en
	dmaA+dmaB (2 * dmaLines líneas de salida), que es justo lo que se
	rellena en la parte síncrona de st7735_core_update_screen().
*/
static inline size_t pending_first_out_pixel(void)
{
	return (size_t)SRC_ROW_FIRST_OUT[dmalayout.srcRowsDirect] * (size_t)TFT_OUT_WIDTH;
}

#if TFT_BYTES_PER_PIXEL == 3
// Expansion 5->8 y 6->8 bits por bit-replication (reparte los bits altos
// hacia los bajos): mismo resultado que la conversion "exacta" *255/(2^n-1),
// pero sin division y sin tabla (ni siquiera hace falta acceso a memoria).
static inline uint8_t expand5to8(uint8_t v5) { return (uint8_t)((v5 << 3) | (v5 >> 2)); }
static inline uint8_t expand6to8(uint8_t v6) { return (uint8_t)((v6 << 2) | (v6 >> 4)); }
#endif

/*	compute_pixel_bytes() - calcula los TFT_BYTES_PER_PIXEL bytes FINALES
	(ya en el formato que espera el panel) para UN pixel de ORIGEN.

	OJO: se llama SOLO cuando cambia el pixel de origen (ver cache
	lastX/lastY en dma_fill_direct/dma_fill_pending), nunca por cada pixel
	de SALIDA. Antes (dma_put_pixel) se recalculaba esto en cada pixel de
	salida aunque repitiera pixel de origen (con zoom, varias veces); ahora
	el resultado se guarda en un array local y dma_put_cached() solo lo
	copia, sin volver a decidir nada.
*/
static inline void compute_pixel_bytes(uint8_t nibble, bool useBg, uint16_t bgColor565, uint8_t *out)
{
	#if TFT_BYTES_PER_PIXEL == 3
	
		// hay que poner fondo Multipaleta ?
		if (useBg)
			{
			// RGB565 -> RGB888 SIN division (ver expand5to8/expand6to8 arriba)
			out[0] = expand5to8((uint8_t)((bgColor565 >> 11) & 0x1F));
			out[1] = expand6to8((uint8_t)((bgColor565 >> 5)  & 0x3F));
			out[2] = expand5to8((uint8_t)( bgColor565         & 0x1F));
			return;
			}
	
		uint32_t v = s_palette888[nibble];
		out[0] = (uint8_t)(v >> 16);		// R (8 bits; el panel usa los 6 altos)
		out[1] = (uint8_t)(v >> 8);		// G
		out[2] = (uint8_t)(v);				// B
	
	#else
		
		uint16_t v = useBg ? bgColor565 : s_palette565[nibble];
		out[0] = (uint8_t)(v >> 8);		// panel real -> MSB primero
		out[1] = (uint8_t)(v & 0xff);
	
	#endif
}

// dma_put_cached() - vuelca en el buffer DMA los bytes YA calculados por
// compute_pixel_bytes(). Ya no decide nada por pixel de salida (ni nibble,
// ni s_mpBuf, ni el formato del panel salvo el numero de bytes a copiar).
static inline void dma_put_cached(uint8_t *chunk, size_t at, const uint8_t *bytes)
{
	uint8_t *p = &chunk[at * TFT_BYTES_PER_PIXEL];
	
	#if TFT_BYTES_PER_PIXEL == 3
		p[0] = bytes[0]; p[1] = bytes[1]; p[2] = bytes[2];
	#else
		p[0] = bytes[0]; p[1] = bytes[1];
	#endif
}

// Región 1: lee de s_frameSrc, formato NATIVO de MakeCode (column-major,
// 2 filas por byte: nibble bajo = fila par, nibble alto = fila impar).
// Rellena desde 'filled' hasta llenar el chunk o alcanzar 'endPixel'.
static inline size_t dma_fill_direct(uint8_t *chunk, size_t filled, size_t chunkPixels, size_t endPixel)
{
	const int outWidth = TFT_OUT_WIDTH;

	// la división/módulo solo se hace UNA vez al entrar; dentro del bucle
	// ox/oy se llevan incrementando (con zoom hay varias veces más píxeles
	// de salida, así que una división por píxel se notaría)
	int oy = (int)(s_frameCursor / outWidth);
	int ox = (int)(s_frameCursor % outWidth);

	// con zoom, varios píxeles de salida seguidos salen del MISMO píxel de
	// origen: cacheamos el nibble para no releer/desempaquetar cada vez.
	// x/y de origen salen de LUT_X/LUT_Y (tabla precalculada una vez en
	// build_zoom_luts()), no de una división -> vale para CUALQUIER
	// factor de zoom, entero o no (p.ej. 2.5x en el panel 400x300).
	uint8_t cachedBytes[TFT_BYTES_PER_PIXEL];
	int lastX = -1, lastY = -1;

	while (filled < chunkPixels && s_frameCursor < endPixel)
		{
		int x = LUT_X[ox];
		int y = LUT_Y[oy];

		if (x != lastX || y != lastY)
			{
			uint8_t b = s_frameSrc[(size_t)x * s_frameBytesPerCol + (y >> 1)];
			uint8_t nibble = (y & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);

			// solo se recalcula el fondo cuando cambia el pixel de origen (no en cada pixel de salida repetido por zoom)
			const bool useBg = useBackground(nibble, x, y);
			uint16_t bgColor = useBg ? multipaletteRGB565At(x, y) : 0;
			
			compute_pixel_bytes(nibble, useBg, bgColor, cachedBytes);
			lastX = x;
			lastY = y;
			}

		dma_put_cached(chunk, filled, cachedBytes);

		filled++;
		s_frameCursor++;

		if (++ox == outWidth) { ox = 0; oy++; }
		}

	return filled;
}

// Región 2: lee de dmalayout.pending, ya TRASPUESTO a row-major
// (2 píxeles horizontales por byte: nibble bajo = x par, nibble alto = x impar).
// Rellena desde 'filled' hasta llenar el chunk o terminar el frame.
static inline size_t dma_fill_pending(uint8_t *chunk, size_t filled, size_t chunkPixels)
{
	const int outWidth = TFT_OUT_WIDTH;
	const int yThreshold = (int)dmalayout.srcRowsDirect;	// primera fila de ORIGEN que vive en 'pending'
	const size_t stride = pending_stride();

	int oy = (int)(s_frameCursor / outWidth);
	int ox = (int)(s_frameCursor % outWidth);

	uint8_t cachedBytes[TFT_BYTES_PER_PIXEL];
	int lastX = -1, lastY = -1;

	while (filled < chunkPixels && s_frameCursor < s_frameTotalPixels)
		{
		int x = LUT_X[ox];
		int y = LUT_Y[oy];

		if (x != lastX || y != lastY)
			{
			// fila relativa dentro de 'pending'
			size_t yp = (size_t)(y - yThreshold);

			uint8_t b = dmalayout.pending[yp * stride + (size_t)(x >> 1)];
			uint8_t nibble = (x & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);

			// solo se recalcula el fondo cuando cambia el pixel de origen (no en cada pixel de salida repetido por zoom)
			const bool useBg = useBackground(nibble, x, y);
			uint16_t bgColor = useBg ? multipaletteRGB565At(x, y) : 0;
			
			compute_pixel_bytes(nibble, useBg, bgColor, cachedBytes);
			lastX = x;
			lastY = y;
			}

		dma_put_cached(chunk, filled, cachedBytes);

		filled++;
		s_frameCursor++;

		if (++ox == outWidth) { ox = 0; oy++; }
		}

	return filled;
}

#if TFT_BYTES_PER_PIXEL == 2 && TFT_ZOOM_NUM == 2 && TFT_ZOOM_DEN == 1
/*	Camino RAPIDO especifico para zoom ENTERO x2 en salida RGB565 (panel
	ILI9341 320x240 <- origen 160x120). La version general (dma_fill_direct/
	dma_fill_pending, basada en LUT_X/LUT_Y) vale para CUALQUIER zoom, entero
	o no, pero por eso no puede aprovechar dos cosas que aqui SI son ciertas
	siempre:

	  1) cada pixel de ORIGEN genera EXACTAMENTE 2 pixeles de SALIDA en
		 horizontal -> se escriben los dos de una vez, sin pasar por
		 LUT_X ni por el cache lastX pixel a pixel.
	  2) cada fila de ORIGEN genera EXACTAMENTE 2 filas de SALIDA IDENTICAS
		 -> la segunda fila no se vuelve a pintar: se copia entera de la
		 primera con memcpy (paleta y fondo multipaleta se evaluan UNA
		 sola vez por fila de origen, no dos).

	Los rellenos (dma_fill_chunk/elastic_fill) siempre cubren un numero
	entero de filas de SALIDA, asi que aqui nunca se corta una fila a la
	mitad ni se intenta copiar entre los dos buffers ping-pong (lastSrcY se
	reinicia en cada llamada, o sea en cada buffer).
*/

// Rellena UNA fila de SALIDA completa (TFT_OUT_WIDTH pixeles) a partir de
// una fila de origen en formato NATIVO (column-major, 2 filas/byte).
static inline void fill_row565_from_direct(uint16_t *outRow, int y)
{
	for (int x = 0; x < TFT_SRC_WIDTH; x++)
		{
		uint8_t b = s_frameSrc[(size_t)x * s_frameBytesPerCol + (y >> 1)];
		uint8_t nibble = (y & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);

		uint16_t vbe = useBackground(nibble, x, y) ? multipaletteRGB565WireAt(x, y) : s_palette565_be[nibble];

		outRow[x * 2]     = vbe;
		outRow[x * 2 + 1] = vbe;		// duplicado horizontal: un calculo, dos pixeles de salida
		}
}

// idem pero leyendo de 'pending' (row-major, 2 pixeles/byte)
static inline void fill_row565_from_pending(uint16_t *outRow, int y, int yThreshold, size_t stride)
{
	const uint8_t *rowPtr = dmalayout.pending + (size_t)(y - yThreshold) * stride;

	for (int x = 0; x < TFT_SRC_WIDTH; x++)
		{
		uint8_t b = rowPtr[x >> 1];
		uint8_t nibble = (x & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
		
		uint16_t vbe = useBackground(nibble, x, y) ? multipaletteRGB565WireAt(x, y) : s_palette565_be[nibble];
		
		outRow[x * 2]     = vbe;
		outRow[x * 2 + 1] = vbe;
		}
}

static inline size_t dma_fill_direct_zoom2_565(uint8_t *chunk, size_t filled, size_t chunkPixels, size_t endPixel)
{
	uint16_t *out = (uint16_t *)chunk;
	int oy = (int)(s_frameCursor / TFT_OUT_WIDTH);
	int lastSrcY = -1;		// fuerza el calculo de la primera fila de esta llamada

	while (filled < chunkPixels && s_frameCursor < endPixel)
		{
		int y = LUT_Y[oy];

		if (y == lastSrcY)
			memcpy(&out[filled], &out[filled - TFT_OUT_WIDTH], TFT_OUT_WIDTH * sizeof(uint16_t));
		else
			{
			fill_row565_from_direct(&out[filled], y);
			lastSrcY = y;
			}

		filled        += TFT_OUT_WIDTH;
		s_frameCursor += TFT_OUT_WIDTH;
		oy++;
		}

	return filled;
}

static inline size_t dma_fill_pending_zoom2_565(uint8_t *chunk, size_t filled, size_t chunkPixels)
{
	uint16_t *out = (uint16_t *)chunk;
	int oy = (int)(s_frameCursor / TFT_OUT_WIDTH);
	const int yThreshold = (int)dmalayout.srcRowsDirect;
	const size_t stride = pending_stride();
	int lastSrcY = -1;

	while (filled < chunkPixels && s_frameCursor < s_frameTotalPixels)
		{
		int y = LUT_Y[oy];

		if (y == lastSrcY)
			memcpy(&out[filled], &out[filled - TFT_OUT_WIDTH], TFT_OUT_WIDTH * sizeof(uint16_t));
		else
			{
			fill_row565_from_pending(&out[filled], y, yThreshold, stride);
			lastSrcY = y;
			}

		filled        += TFT_OUT_WIDTH;
		s_frameCursor += TFT_OUT_WIDTH;
		oy++;
		}

	return filled;
}
#endif // TFT_BYTES_PER_PIXEL == 2 && TFT_ZOOM_NUM == 2 && TFT_ZOOM_DEN == 1

// convierte hasta dma_chunk_pixels() píxeles (a partir de s_frameCursor)
// al buffer DMA 'idx'. Avanza s_frameCursor. Devuelve cuántos píxeles se
// han escrito. Solo comprueba la frontera entre regiones UNA vez por
// chunk (no por píxel): un chunk puede cruzarla, y entonces se rellena
// primero con la región directa y luego se continúa con la de 'pending'.
static inline size_t dma_fill_chunk(int idx)
{
	//DMESG("    GURU - dma_fill_chunk()");
	
	uint8_t *chunk = dma_buf_ptr(idx);
	size_t chunkPixels = dma_buf_size(idx) / TFT_BYTES_PER_PIXEL;
	size_t filled = 0;

	size_t boundary = pending_first_out_pixel();

	#if TFT_BYTES_PER_PIXEL == 2 && TFT_ZOOM_NUM == 2 && TFT_ZOOM_DEN == 1

		// camino rapido: zoom entero x2, RGB565 (ver dma_fill_direct_zoom2_565 arriba)
		if (s_frameCursor < boundary)
			filled = dma_fill_direct_zoom2_565(chunk, filled, chunkPixels, boundary);

		if (filled < chunkPixels)
			filled = dma_fill_pending_zoom2_565(chunk, filled, chunkPixels);

	#else

		// camino general: vale para cualquier zoom (entero o no) y cualquier TFT_BYTES_PER_PIXEL
		if (s_frameCursor < boundary)
			filled = dma_fill_direct(chunk, filled, chunkPixels, boundary);

		if (filled < chunkPixels)
			filled = dma_fill_pending(chunk, filled, chunkPixels);

	#endif

	return filled;
}

// bytes de una línea de SALIDA completa (RGB565, con zoom ya aplicado)
static inline size_t out_line_bytes(void) { return (size_t)TFT_OUT_WIDTH * TFT_BYTES_PER_PIXEL; }

// líneas de SALIDA que quedan por generar (de 'pending')
static inline size_t remaining_out_lines(void)
{
	return (s_frameTotalPixels - s_frameCursor) / (size_t)TFT_OUT_WIDTH;
}

/*	Puntero de lectura actual dentro de 'pending': todo lo que queda por
	DEBAJO de él ya se ha convertido y enviado al buffer DMA, así que esa
	memoria es libre y reutilizable. Todo lo que hay a partir de él sigue
	siendo dato vivo y NO se puede pisar.

	Como todos los rellenos cubren siempre un número entero de LÍNEAS DE
	SALIDA (ver dma_fill_chunk/elastic_fill), s_frameCursor siempre cae
	en un borde de línea, así que LUT_Y[oy] da la fila de origen exacta
	sin necesidad de ninguna división por el factor de zoom.
*/
static inline uint8_t *pending_read_ptr(void)
{
	const size_t boundary = pending_first_out_pixel();

	if (s_frameCursor <= boundary) return dmalayout.pending;

	// cada relleno cubre siempre un número entero de líneas de SALIDA (ver
	// dma_fill_chunk/elastic_fill), así que s_frameCursor está alineado a
	// un borde de línea: 'oy' es la línea de salida que se está rellenando
	// ahora. LUT_Y[oy] es la fila de ORIGEN correspondiente -> todo lo
	// anterior a esa fila ya se leyó por completo de 'pending'.
	const size_t oy          = s_frameCursor / (size_t)TFT_OUT_WIDTH;
	const size_t srcRowsDone = LUT_Y[oy] - dmalayout.srcRowsDirect;

	return dmalayout.pending + srcRowsDone * pending_stride();
}

/*	elastic_fill() - relleno de un buffer ping-pong APROVECHANDO el hueco
	que 'pending' ha ido dejando libre (ver el comentario grande junto a
	s_bufPtr/s_bufSize más arriba). SOLO se usa para los rellenos que
	pasan por 'pending' (o sea, a partir del 2º relleno de cada buffer);
	el primerísimo relleno de dmaA+dmaB, en la parte síncrona de
	st7735_core_update_screen(), sigue usando dma_fill_chunk() a secas,
	con los tamaños "de fábrica" de dmalayout, SIN pasar por aquí.

	Cuando se entra aquí (desde la tarea worker) el OTRO buffer está en
	vuelo: su rango [ptr, ptr+size) es intocable. El espacio utilizable es

		[dmalayout.dmaA, pending_read_ptr())

	y se reparte a medias: dmaA (idx 0) anclado al principio creciendo
	hacia delante, dmaB (idx 1) anclado al final creciendo hacia atrás.
	Cada uno se capa además para no invadir al otro ni pasarse de lo que
	queda por enviar, de ahí que el último trozo del frame salga pequeño
	de forma natural, sin ningún caso especial.

	Escribir en esta zona NUNCA pisa datos de 'pending' vivos: el límite
	superior es justo pending_read_ptr(), y durante el relleno la lectura
	avanza a partir de ahí, siempre por delante de la escritura.
*/
static size_t elastic_fill(int idx)
{
	const size_t lineBytes = out_line_bytes();
	const size_t remLines  = remaining_out_lines();

	uint8_t *const base     = dmalayout.dmaA;			// principio del bloque grande (no se mueve nunca)
	uint8_t *const pendRead = pending_read_ptr();		// primer byte de 'pending' TODAVIA sin convertir

	// el otro buffer está EN VUELO ahora mismo: su rango es intocable
	const int other = idx ^ 1;
	uint8_t *const otherStart = s_bufPtr[other];
	uint8_t *const otherEnd   = otherStart + s_bufSize[other];

	// espacio utilizable en este instante, a medias entre los dos buffers
	const uintptr_t usableEnd = AlignDown((uintptr_t)pendRead, 4);
	const size_t totalLines = (usableEnd > (uintptr_t)base) ? (size_t)(usableEnd - (uintptr_t)base) / lineBytes : 0;

	size_t lines = totalLines / 2;
	size_t maxLines;

	if (idx == 0)
		{
		// dmaA: anclado al PRINCIPIO -> no puede llegar a donde empieza el otro
		maxLines = (otherStart > base) ? (size_t)(otherStart - base) / lineBytes : 0;
		}
	else
		{
		// dmaB: anclado al FINAL -> no puede bajar de donde acaba el otro
		const uintptr_t lo = AlignUp((uintptr_t)otherEnd, 4);
		maxLines = (usableEnd > lo) ? (size_t)(usableEnd - lo) / lineBytes : 0;
		}

	if (lines > maxLines) lines = maxLines;
	if (lines > remLines) lines = remLines;

	// no queremos partir una fila de ORIGEN a la mitad entre dos chunks:
	// recortamos 'lines' para terminar justo en un borde de fila. Con zoom
	// no entero cada fila de origen "dura" un número distinto de líneas de
	// salida (2 o 3 en el caso 2.5x), así que ya no vale un simple módulo;
	// se resuelve en O(1) con SRC_ROW_FIRST_OUT[], sin bucles ni división.
	{
	const size_t oyStart = s_frameCursor / (size_t)TFT_OUT_WIDTH;	// s_frameCursor siempre cae en borde de línea aquí
	const size_t oyEnd   = oyStart + lines;

	if (oyEnd < (size_t)TFT_OUT_HEIGHT)
		{
		const int rowAtEnd = LUT_Y[oyEnd];
		if ((size_t)SRC_ROW_FIRST_OUT[rowAtEnd] != oyEnd)		// oyEnd cae A MITAD de una fila de origen
			lines = SRC_ROW_FIRST_OUT[rowAtEnd] - oyStart;		// recortamos al último borde de fila completo
		}
	}

	if (lines == 0)
		{
		// no debería ocurrir nunca: el reparto garantiza al menos el tamaño
		// "de fábrica" (dmalayout.dmaLines) mientras quede frame por enviar
		DMESG("ERROR elastic_fill(): sin hueco para el buffer %d", idx);
		return 0;
		}

	// puntero definitivo de ESTE relleno; no se toca hasta que se haya enviado
	s_bufPtr[idx]  = (idx == 0) ? base : (uint8_t *)(usableEnd - lines * lineBytes);
	s_bufSize[idx] = lines * lineBytes;

	return dma_fill_chunk(idx);
}

/*	pending_copy_transposed() - copia a 'dmalayout.pending' la parte del
	frame que todavía no se procesa en el acto, TRASPONIÉNDOLA de:

		origen : COLUMN-MAJOR, 2 FILAS por byte
				 (nibble bajo = fila par, nibble alto = fila impar)
		destino: ROW-MAJOR, 2 COLUMNAS por byte
				 (nibble bajo = x par, nibble alto = x impar)

	Truco para no ir píxel a píxel: un byte de origen ya trae las dos
	filas (2*yb, 2*yb+1) de una columna x. Leyendo DOS columnas
	consecutivas (x, x+1) tenemos los 4 nibbles justos para escribir
	DOS bytes de destino completos, uno en cada fila. Así son 2 lecturas
	y 2 escrituras por cada 4 píxeles, sin máscaras por píxel suelto.
*/
static inline void pending_copy_transposed(const uint8_t *src, int width, int bytesPerCol)
{
	if (dmalayout.pendingSize == 0) return;

	const size_t stride = ((size_t)width + 1) >> 1;			// bytes por fila en destino
	const size_t firstYb = dmalayout.srcFirstPendingYb;		// primer byte-fila de origen que aún no se ha procesado (OJO: NO es dmaLines cuando hay zoom)

	for (size_t yb = firstYb; yb < (size_t)bytesPerCol; yb++)
		{
		uint8_t *rowEven = dmalayout.pending + ((yb - firstYb) * 2)       * stride;
		uint8_t *rowOdd  = rowEven + stride;

		int x = 0;
		for (; x + 1 < width; x += 2)
			{
			uint8_t b0 = src[(size_t)x       * bytesPerCol + yb];		// columna x   : filas 2*yb (bajo) y 2*yb+1 (alto)
			uint8_t b1 = src[(size_t)(x + 1) * bytesPerCol + yb];		// columna x+1 : idem

			rowEven[x >> 1] = (uint8_t)((b0 & 0x0f) | ((b1 & 0x0f) << 4));
			rowOdd [x >> 1] = (uint8_t)(((b0 >> 4) & 0x0f) | (b1 & 0xf0));
			}

		// ancho impar: última columna suelta (solo nibble bajo del byte destino)
		if (x < width)
			{
			uint8_t b0 = src[(size_t)x * bytesPerCol + yb];
			rowEven[x >> 1] = (uint8_t)(b0 & 0x0f);
			rowOdd [x >> 1] = (uint8_t)((b0 >> 4) & 0x0f);
			}
		}
}

// encola por DMA (no bloqueante) el buffer 'idx', ya relleno con 'n' píxeles
static inline void dma_queue_send(int idx, size_t n)
{
	spi_transaction_t *t = &s_dmaTrans[idx];
	*t = (spi_transaction_t){0};
	t->length = n * TFT_BITS_PER_PIXEL;
	t->tx_buffer = dma_buf_ptr(idx);

	s_asyncBusy = true;						// a partir de aquí sí queremos que dma_trans_done_cb despierte al worker
	esp_err_t err = spi_device_queue_trans(s_spi, t, portMAX_DELAY);
	if (err != ESP_OK)
		{
		DMESG("ERROR spi_device_queue_trans() = %d", (int)err);
		target_panic(PANIC_SCREEN_ERROR);
		}
	s_dmaTransPending[idx] = true;
	s_sendIdx = idx;
}


/*	Funciones para el sistema de envio mediante INVALIDATE RECTANGLES

	FLUJO
	
	  1) PARTE SINCRONA (rect_begin, dentro de st7735_core_update_screen):
		 - cada rectangulo se COPIA a la cola del buffer DMA (s_rectData) (4bpp, row-major, 2 pixeles
		   por byte), porque el framebuffer de Arcade solo es valido hasta que
		   updateScreen() devuelve el control.
		 - se calcula su rectangulo de SALIDA (con zoom) con las tablas
		   SRC_COL_FIRST_OUT[] / SRC_ROW_FIRST_OUT[] (valen tambien para zoom
		   no entero).
		 - se rellenan los DOS buffers ping-pong y se encola el primero.
	
	  2) PARTE ASINCRONA (worker): igual que en frame completo, pero cada
		 chunk que ABRE un rectangulo manda antes, por polling, la ventana
		 (CASET/RASET) + RAMWR. Eso es seguro porque en ese instante no hay
		 ninguna transaccion en vuelo (se acaba de completar la anterior y la
		 siguiente aun no se ha encolado).
		 La conversion del primer chunk del rectangulo siguiente se hace
		 MIENTRAS el ultimo chunk del anterior esta en vuelo, asi que entre
		 rectangulos solo queda el coste de los comandos de ventana.
*/

struct RECT_HDR { uint8_t xi, yi, w, h; };			// 4 bytes por rectangulo, vive en la cola del buffer DMA

struct RECT_JOB		// temporal en pila, ya NO hay array estatico
	{
	uint16_t ox0, oy0, ow, oh;
	uint8_t  xi, yi, w, h;
	uint16_t stride;
	uint32_t stageOff;
	};

static RECT_HDR *s_rectHdr  = nullptr;				// apunta dentro de s_dmaBigBuffer (cola)
static uint8_t  *s_rectData = nullptr;				// datos 4bpp, justo detras de las cabeceras
static uint32_t  s_prodStageOff = 0;				// offset en s_rectData del rectangulo que produce el productor

static volatile bool s_rectMode = false;							// el frame en curso se esta enviando por rectangulos (lo lee el worker)

#ifdef __EMSCRIPTEN__

	/*	sim_dma_time_us() - cuanto tarda (simulado) la transaccion DMA 't'.
			- Frame COMPLETO (s_rectMode == false): el frame entero cuesta TFT_SIM_FULLFRAME_US y se reparte entre
			  los chunks EN PROPORCION a sus bytes, de modo que la suma de todos los chunks = 30 ms exactos,
			  da igual cuantos chunks salgan (el tamaño de dmaA/dmaB es elastico).
			- Invalidate rectangles (s_rectMode == true): se mantiene el calculo de siempre (TFT_SIM_SPI_US).
	*/
	static inline uint64_t sim_dma_time_us(const spi_transaction_t *t)
	{
		if (!s_rectMode)
			{
			const uint64_t frameBits = (uint64_t)TFT_OUT_WIDTH * (uint64_t)TFT_OUT_HEIGHT * (uint64_t)TFT_BITS_PER_PIXEL;
			return ((uint64_t)TFT_SIM_FULLFRAME_US * (uint64_t)t->length) / frameBits;
			}

		return (uint64_t)TFT_SIM_SPI_US;		// calculo original
	}
#endif

static int      s_rectCount = 0;
static int      s_prodJob = 0;										// productor: proximo rectangulo a convertir ...
static int      s_prodRow = 0;										// ... y proxima fila de SALIDA (relativa al rectangulo)
static bool     s_chunkStartsJob[2] = { false, false };				// el chunk del buffer idx es el PRIMERO de su rectangulo -> hay que mandar ventana+RAMWR antes
static int      s_chunkJob[2] = { 0, 0 };

// definidos mas abajo (codigo compartido con el camino de frame completo)
void st7735_hal_write_command(uint8_t cmd);
void st7735_core_set_addr_window(int x, int y, int w, int h);

// Recorta a pantalla y devuelve false si queda vacio
static inline bool rect_clamp(const RECTANGULO &r, int &xi, int &yi, int &w, int &h)
{
	xi = r.xi < 0 ? 0 : r.xi;
	yi = r.yi < 0 ? 0 : r.yi;
	int xf = r.xf > TFT_SRC_WIDTH  - 1 ? TFT_SRC_WIDTH  - 1 : r.xf;
	int yf = r.yf > TFT_SRC_HEIGHT - 1 ? TFT_SRC_HEIGHT - 1 : r.yf;
	if (xi > xf || yi > yf) return false;
	w = xf - xi + 1;
	h = yf - yi + 1;
	return true;
}

// Reconstruye el job completo a partir de la cabecera de 4 bytes + LUTs
static inline RECT_JOB rect_job_make(int i, uint32_t stageOff)
{
	const RECT_HDR &hd = s_rectHdr[i];
	RECT_JOB j;
	j.xi = hd.xi;  j.yi = hd.yi;  j.w = hd.w;  j.h = hd.h;
	j.ox0 = SRC_COL_FIRST_OUT[hd.xi];
	j.oy0 = SRC_ROW_FIRST_OUT[hd.yi];
	j.ow  = SRC_COL_FIRST_OUT[hd.xi + hd.w] - j.ox0;
	j.oh  = SRC_ROW_FIRST_OUT[hd.yi + hd.h] - j.oy0;
	j.stride   = (uint16_t)((hd.w + 1) >> 1);
	j.stageOff = stageOff;
	return j;
}

static inline void rect_fill_row565(uint16_t *outRow, const RECT_JOB &j, int y)
{
	/*	Camino rapido zoom x2 RGB565 (mismo criterio que fill_row565_from_pending): cada pixel de origen
		son 2 de salida, y cada fila de origen son 2 filas de salida identicas (la 2ª se copia con memcpy).
	*/
	
	const uint8_t *rowPtr = s_rectData + j.stageOff + (size_t)(y - j.yi) * j.stride;

	for (int xr = 0; xr < j.w; xr++)
		{
		const uint8_t b = rowPtr[xr >> 1];
		const uint8_t nibble = (xr & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
		const int x = j.xi + xr;

		const uint16_t vbe = useBackground(nibble, x, y) ? multipaletteRGB565WireAt(x, y) : s_palette565_be[nibble];
		
		outRow[xr * 2]     = vbe;
		outRow[xr * 2 + 1] = vbe;
		}
}

static inline void rect_convert_rows(const RECT_JOB &j, int row0, int rows, uint8_t *chunk)
{
	// Convierte 'rows' filas de SALIDA del rectangulo 'j' (a partir de la fila relativa 'row0') al buffer 'chunk'
	
	const size_t rowBytes = (size_t)j.ow * TFT_BYTES_PER_PIXEL;
	
	int lastSrcY = -1;			// por llamada (o sea por buffer): nunca se copia una fila del buffer anterior

	for (int r = 0; r < rows; r++)
		{
		
		const int oy = j.oy0 + row0 + r;
		const int y  = LUT_Y[oy];
		uint8_t *dst = chunk + (size_t)r * rowBytes;

		// con zoom, varias filas de salida seguidas salen de la MISMA fila de origen -> copia
		if (r > 0 && y == lastSrcY)
			{
			memcpy(dst, dst - rowBytes, rowBytes);
			continue;
			}
		lastSrcY = y;

		#if TFT_BYTES_PER_PIXEL == 2 && TFT_ZOOM_NUM == 2 && TFT_ZOOM_DEN == 1

			rect_fill_row565((uint16_t *)dst, j, y);

		#else

			const uint8_t *rowPtr = s_rectData + j.stageOff + (size_t)(y - j.yi) * j.stride;
			uint8_t cachedBytes[TFT_BYTES_PER_PIXEL];
			int lastX = -1;

			for (int c = 0; c < (int)j.ow; c++)
				{
				const int x = LUT_X[j.ox0 + c];

				if (x != lastX)
					{
					const int xr = x - j.xi;
					const uint8_t b = rowPtr[xr >> 1];
					const uint8_t nibble = (xr & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
					const bool useBg = useBackground(nibble, x, y);
					const uint16_t bgColor = useBg ? multipaletteRGB565At(x, y) : 0;
					
					compute_pixel_bytes(nibble, useBg, bgColor, cachedBytes);
					lastX = x;
					}

				dma_put_cached(dst, (size_t)c, cachedBytes);
				}

		#endif
		
		}
	
}

static size_t rect_fill_next_chunk(int idx)
{
	/*	Productor: rellena el buffer ping-pong 'idx' con el siguiente trozo de la lista de rectangulos.
		Un chunk NUNCA cruza de un rectangulo a otro. Devuelve los pixeles escritos (0 = no queda nada)
	*/
	
	if (s_prodJob >= s_rectCount) return 0;

	const RECT_JOB j = rect_job_make(s_prodJob, s_prodStageOff);
	const size_t rowBytes = (size_t)j.ow * TFT_BYTES_PER_PIXEL;

	// los buffers miden >= una linea de SALIDA completa (se comprueba en rect_begin) y ow <= TFT_OUT_WIDTH -> rows >= 1
	size_t rows = dma_buf_size(idx) / rowBytes;
	const size_t left = (size_t)j.oh - (size_t)s_prodRow;
	if (rows > left) rows = left;
	if (rows == 0) return 0;

	s_chunkStartsJob[idx] = (s_prodRow == 0);
	s_chunkJob[idx]       = s_prodJob;

	rect_convert_rows(j, s_prodRow, (int)rows, dma_buf_ptr(idx));

	s_prodRow += (int)rows;
	if (s_prodRow >= (int)j.oh)
		{
		s_prodStageOff += (uint32_t)j.stride * j.h;		// el siguiente rectangulo empieza justo detras
		s_prodJob++;
		s_prodRow = 0;
		}

	return rows * (size_t)j.ow;
}

// Copia el rect de ORIGEN (column-major, 2 filas/byte) a dst en row-major 4bpp.
// Escribe TODOS los bytes de stride*h (las columnas pares asignan, las impares hacen OR), no hace falta limpiar antes.
static void rect_stage_copy(const uint8_t *src, int bytesPerCol, int xi, int yi, int w, int h, uint8_t *dst)
{
	const int stride = (w + 1) >> 1;

	// recorrido por columnas de origen (lectura secuencial del formato nativo column-major de Arcade MakeCode)
	for (int xr = 0; xr < w; xr++)
		{
		
		const uint8_t *col = src + (size_t)(xi + xr) * bytesPerCol;
		
		uint8_t *d = dst + (xr >> 1);

		if (xr & 1)
			{
			for (int yr = 0; yr < h; yr++)
				{
				const int y = yi + yr;
				const uint8_t b = col[y >> 1];
				d[(size_t)yr * stride] |= (uint8_t)(((y & 1) ? (b & 0xf0) : (b << 4)) & 0xf0);
				}
			}
		else
			{
			for (int yr = 0; yr < h; yr++)
				{
				const int y = yi + yr;
				const uint8_t b = col[y >> 1];
				d[(size_t)yr * stride] = (uint8_t)((y & 1) ? (b >> 4) : (b & 0x0f));
				}
			}
		
		}
}

static void rect_send(int idx)
{
	/*	Encola el buffer 'idx' (ya relleno; su tamaño esta aparcado en s_dmaTrans[idx].length).
		Si abre un rectangulo, antes manda ventana + RAMWR por polling. Hay que llamarlo SOLO cuando no hay
		ninguna transaccion DMA en vuelo (parte sincrona del frame, o worker tras get_trans_result).
	*/
	
	const size_t n = s_dmaTrans[idx].length / TFT_BITS_PER_PIXEL;

	if (s_chunkStartsJob[idx])
		{
		const RECT_JOB j = rect_job_make(s_chunkJob[idx], 0);		// para la ventana no hace falta stageOff

		s_asyncBusy = false;			// los post_cb de estos comandos polling NO deben despertar al worker (dma_queue_send lo vuelve a poner a true)
		st7735_core_set_addr_window(j.ox0, j.oy0, j.ow, j.oh);
		st7735_hal_write_command(ST7735_RAMWR);
		dc_data();
		}

	dma_queue_send(idx, n);
}

/*	Parte SINCRONA del envio por rectangulos. Se llama con s_frameFreeSem YA tomado.
	Devuelve false si no se puede (no caben, lista incoherente...) -> el llamador manda frame completo.

	Reparto del buffer DMA grande:

		[ dmaA | dmaB | ...... libre ...... | HDR[n] (4B c/u) | DATOS 4bpp de los rects ]
		^base                                ^tail

	La cola (HDR + DATOS) ocupa justo lo necesario; todo lo que sobra se parte a medias en dmaA y dmaB.
	Solo aqui es seguro leer precalculatedscene.invrects (el siguiente CommitInvalidateRectangles
	puede machacar esa zona mientras el worker sigue enviando), por eso todo se copia ahora.
*/
static bool rect_begin(const uint8_t *packed4bpp, int width, int height)
{
	if (width != TFT_SRC_WIDTH || height != TFT_SRC_HEIGHT) return false;

	const int count = precalculatedscene.ir_back_count;
	const int start = precalculatedscene.ir_back_start;
	if (count <= 0 || start < 0 || start + count > MAXINVALIDATERECTS) return false;

	// PASADA 1: cuanto ocupa la cola (solo aritmetica, no se escribe nada)
	size_t dataBytes = 0;
	int njobs = 0;
	for (int i = 0; i < count; i++)
		{
		int xi, yi, w, h;
		if (!rect_clamp(precalculatedscene.invrects[start + i], xi, yi, w, h)) continue;
		dataBytes += (size_t)((w + 1) >> 1) * h;
		njobs++;
		}

	if (njobs == 0)
		{
		xSemaphoreGive(s_frameFreeSem);		// nada que pintar: liberamos el frame que acabamos de reservar
		return true;
		}

	const size_t hdrBytes  = (size_t)AlignUp((uintptr_t)njobs * sizeof(RECT_HDR), 4);
	const size_t tailBytes = hdrBytes + dataBytes;
	if (tailBytes >= (size_t)TFT_DMA_BUFFER_TOTAL_BYTES) return false;

	uint8_t *const base = s_dmaBigBuffer;
	uint8_t *const tail = (uint8_t *)AlignDown((uintptr_t)(base + TFT_DMA_BUFFER_TOTAL_BYTES - tailBytes), 4);

	const size_t avail = (size_t)(tail - base);
	const size_t half  = (size_t)AlignDown(avail / 2, 4);

	// cada ping-pong debe poder contener como minimo una linea de SALIDA completa; si no, frame completo
	if (half < out_line_bytes()) return false;

	// PASADA 2: cabeceras + copia de los 4bpp (el framebuffer de Arcade solo es valido ahora)
	const int bytesPerCol = (height + 1) >> 1;

	s_rectHdr  = (RECT_HDR *)tail;
	s_rectData = tail + hdrBytes;

	size_t off = 0;
	int k = 0;
	for (int i = 0; i < count; i++)
		{
		int xi, yi, w, h;
		if (!rect_clamp(precalculatedscene.invrects[start + i], xi, yi, w, h)) continue;

		s_rectHdr[k].xi = (uint8_t)xi;  s_rectHdr[k].yi = (uint8_t)yi;
		s_rectHdr[k].w  = (uint8_t)w;   s_rectHdr[k].h  = (uint8_t)h;
		k++;

		rect_stage_copy(packed4bpp, bytesPerCol, xi, yi, w, h, s_rectData + off);
		off += (size_t)((w + 1) >> 1) * h;
		}

	// ping-pong con todo el espacio libre repartido a medias (fijo durante todo el frame)
	s_bufPtr[0]  = base;         s_bufSize[0] = half;
	s_bufPtr[1]  = base + half;  s_bufSize[1] = half;

	s_rectCount    = njobs;
	s_prodJob      = 0;
	s_prodRow      = 0;
	s_prodStageOff = 0;
	s_readyIdx     = -1;
	s_rectMode     = true;

	const size_t n1 = rect_fill_next_chunk(1);	// primer trozo
	const size_t n0 = rect_fill_next_chunk(0);	// segundo trozo (puede ser ya del rectangulo siguiente)

	s_dmaTrans[1].length = n1 * TFT_BITS_PER_PIXEL;
	if (n0 > 0)
		{
		s_dmaTrans[0].length = n0 * TFT_BITS_PER_PIXEL;
		s_readyIdx = 0;
		}

	rect_send(1);
	
	return true;
}




/*	Callback de fin de transacción DMA. Corre en contexto de INTERRUPCIÓN,
	así que debe ser mínimo: nada de spi_device_queue_trans() ni
	spi_device_get_trans_result() aquí (no son ISR-safe). Solo despierta
	a la tarea worker, que es quien hace el trabajo real casi al instante.
*/
static void IRAM_ATTR dma_trans_done_cb(spi_transaction_t *trans)
{
	(void)trans;
	
	/*	Ignora los post_cb "colaterales" de transacciones POLLING (comandos/ datos fuera del envío de frame).
		Solo notificamos al worker si hay un envío async real en curso. */
	if (!s_asyncBusy) return;
	
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	vTaskNotifyGiveFromISR(s_dmaTaskHandle, &xHigherPriorityTaskWoken);
	if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

/*	st7735_dma_worker_task_step(): UN paso del worker, es decir, todo lo
	que hay que hacer cada vez que llega la 'interrupción' de fin de
	transacción DMA (dma_trans_done_cb). Es aquí donde de verdad se
	encola el buffer siguiente y se rellena el que acaba de quedar
	libre, continuando el envío del frame hasta terminarlo, sin que
	updateScreen() tenga que esperar nada de esto.

	Se extrae como función aparte (en vez de dejarla 'suelta' dentro
	del for(;;) de st7735_dma_worker_task) para que en Emscripten se
	pueda invocar DIRECTAMENTE desde el fake de vTaskNotifyGiveFromISR()
	(ver ESPIDFfake.cpp): ahí no hay una tarea FreeRTOS real esperando
	en ulTaskNotifyTake(), así que 'despertar la tarea' es simplemente
	ejecutar ya mismo este paso. Es la ÚNICA diferencia real de
	mecanismo entre plataformas; el resto (esta función, dma_queue_send,
	elastic_fill...) es exactamente el mismo código en ambas.
*/
extern "C" void st7735_dma_worker_task_step(void)
{
	//DMESG("    GURU - st7735_dma_worker_task_step()");
	
	uint32_t cycStart = XTHAL_GET_CCOUNT();
	
	int finishedIdx = s_sendIdx;

	spi_transaction_t *rtrans;
	spi_device_get_trans_result(s_spi, &rtrans, portMAX_DELAY);	// libera el slot en el driver SPI
	s_dmaTransPending[finishedIdx] = false;

	if (s_readyIdx >= 0)
		{
		// había un buffer ya preparado -> lo encolamos ya mismo
		int toSend = s_readyIdx;
		s_readyIdx = -1;
		
		// caso que estamos con INVALIDATE RECTANGLES
		if (s_rectMode)
			{
			
			// envio por rectangulos: ventana+RAMWR si el chunk abre rectangulo, y a la cola
			rect_send(toSend);

			// y el buffer recien liberado se rellena con el siguiente trozo (puede ser ya del rectangulo siguiente)
			size_t nr = rect_fill_next_chunk(finishedIdx);
			if (nr > 0)
				{
				s_dmaTrans[finishedIdx].length = nr * TFT_BITS_PER_PIXEL;
				s_readyIdx = finishedIdx;
				}

			s_cpuCycles += XTHAL_GET_CCOUNT() - cycStart;
			
			return;
			
			}
		
		// seguimos el camino de full frame
		
		dma_queue_send(toSend, s_dmaTrans[toSend].length / TFT_BITS_PER_PIXEL);

		// y el que acaba de quedar libre lo rellenamos con el
		// siguiente trozo, para tenerlo listo para la próxima IRQ
		if (s_frameCursor < s_frameTotalPixels)
			{
			size_t n = elastic_fill(finishedIdx);
			if (n > 0)
				{
				s_dmaTrans[finishedIdx].length = n * TFT_BITS_PER_PIXEL;	// aparcamos el tamaño para cuando toque encolarlo
				s_readyIdx = finishedIdx;
				}
			}
		
		s_cpuCycles += XTHAL_GET_CCOUNT() - cycStart;
		}
	else
		{
		// no quedaba nada más preparado -> el frame ha terminado del todo
		s_asyncBusy     = false;						// vuelve a bloquear los post_cb "colaterales" de la próxima ronda de comandos polling
		
		s_lastFrameCycles   = (XTHAL_GET_CCOUNT() - s_frameStartCycles);
		s_cpuCycles += XTHAL_GET_CCOUNT() - cycStart;
		s_lastCPUCycles		= s_cpuCycles;
		
		xSemaphoreGive(s_frameFreeSem);
		}
}

/*	Tarea worker: duerme en ulTaskNotifyTake() y despierta en cuanto el
	callback de IRQ le da la notificación (fin de una transacción DMA),
	para ejecutar un paso (st7735_dma_worker_task_step()).

	OJO Emscripten: esta función SOLO llega a ejecutarse de verdad en
	ESP32 real. En el simulador, xTaskCreatePinnedToCore() es un fake
	que no llega a lanzar ninguna tarea (ver ESPIDFfake.cpp), así que
	este for(;;) nunca corre allí; el 'despertar' pasa siempre por
	st7735_dma_worker_task_step() directamente, invocada desde el fake
	de vTaskNotifyGiveFromISR(). Se deja aquí, compilando igual en
	ambas plataformas, para que st7735_hal_init() (código compartido)
	no necesite ningún #ifdef para llamar a xTaskCreatePinnedToCore().
*/
static void st7735_dma_worker_task(void *pv)
{
	(void)pv;
	
	DMESG("FreeRTOS task st7735_dma_worker_task() prio=%d cpu=%d",uxTaskPriorityGet(NULL),xPortGetCoreID());
	
	for (;;)
		{
		// pdFALSE => se comporta como un semáforo contador: cada
		// notificación (cada IRQ) desbloquea una vuelta del bucle,
		// sin perder eventos aunque lleguen "pegados".
		ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
		
		st7735_dma_worker_task_step();
		}
}


void st7735_hal_write_command(uint8_t cmd)
{
	st7735_hal_dma_wait_idle();		// no se puede hacer polling con transacciones DMA en cola sin terminar
	
	spi_transaction_t t = {0};
	t.length = 8;
	t.tx_buffer = &cmd;
	dc_cmd();
	spi_device_polling_transmit(s_spi, &t);
}

void st7735_hal_write_data(const uint8_t *data, size_t len)
{
	if (!len) return;
	
	st7735_hal_dma_wait_idle();		// idem que en st7735_hal_write_command()
	
	spi_transaction_t t = {0};
	t.length = len * 8;
	t.tx_buffer = data;
	dc_data();
	spi_device_polling_transmit(s_spi, &t);
}

void st7735_core_set_addr_window(int x, int y, int w, int h)
{		
	x += TFT_XSTART;
	y += TFT_YSTART;

	int x2 = x + w - 1, y2 = y + h - 1;
	
	uint8_t caset[4] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x2 >> 8), (uint8_t)x2 };
	
	uint8_t raset[4] = { (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y2 >> 8), (uint8_t)y2 };
	
	// Orden estándar del datasheet (CASET=columna/x, RASET=fila/y). El chip
	// simulado en st7735-wokwi-chip-main.c también sigue este orden, así que
	// es el mismo camino tanto en el simulador como en el ESP32 real.
	st7735_hal_write_command(ST7735_CASET);
	st7735_hal_write_data(caset, sizeof(caset));
	st7735_hal_write_command(ST7735_RASET);
	st7735_hal_write_data(raset, sizeof(raset));
}

static inline void rst_set_level(int level)
{
	#ifdef __EMSCRIPTEN__
		gpio_set_level( (gpio_num_t)find_pin("RST") ,level);
	#else
		gpio_set_level((gpio_num_t)TFT_PIN_RST,level);
	#endif
}

/*	st7735_hal_init(): inicializa hardware real en el ESP32.

	1) Configura como salidas digitales los pines de control (DC, RST y,
	   opcionalmente, backlight).
	2) Hace un reset "duro" del panel por hardware (línea RST).
	3) Inicializa el bus SPI (spi_bus_initialize) con los pines SCK/MOSI y
	   lo "engancha" a un dispositivo SPI (spi_bus_add_device) con la
	   frecuencia y el modo SPI correctos para el ST7735, dejando el
	   handle en s_spi (la variable que ya usan st7735_hal_write_command()
	   / st7735_hal_write_data() / st7735_hal_write_pixels()).
	4) Envía la secuencia de comandos de arranque del controlador ST7735
	   (salir de reset/sleep, modo de color 16bpp, orientación, encender
	   panel).

	Se llama una única vez desde screen_init() (ver platform.cpp) antes de
	la primera actualización de pantalla.
*/
void st7735_hal_init(void)
{
	
	/*	Especifico para CRCbit-pixel
		
			GPIO26 -> salida motor vibrador (apagado)
			GPIO25 -> DAC audio a 0V
			GPIO2,5,27,33 -> botones con pull-up interno
			GPIO15 -> CS tarjeta SD desactivado
	*/
	#ifndef __EMSCRIPTEN__
	
		gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_26, 0);
		gpio_set_direction(GPIO_NUM_15, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_15, 1);
			// DAC GPIO25 = 0V
			// aunque lo trato como digital y pongo 0
		gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_25, 0);
			// Botones con pull-up
		gpio_config_t btn_conf = {};
		btn_conf.pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_27) | (1ULL << GPIO_NUM_33);
		btn_conf.mode = GPIO_MODE_INPUT;
		btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
		btn_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		btn_conf.intr_type = GPIO_INTR_DISABLE;
		gpio_config(&btn_conf);
	#endif
	
	
	/*	Gestion del buffer de DMA CAPABLE
	*/

	s_dmaBigBuffer = (uint8_t *)heap_caps_malloc(TFT_DMA_BUFFER_TOTAL_BYTES, MALLOC_CAP_DMA);
	if (!s_dmaBigBuffer)
		{
		DMESG("ERROR heap_caps_malloc() buffer DMA grande (%d bytes)", (int)TFT_DMA_BUFFER_TOTAL_BYTES);
		target_panic(PANIC_SCREEN_ERROR);
		}

	build_zoom_luts();		// LUT_X/LUT_Y/SRC_ROW_FIRST_OUT: hace falta ANTES de CalculateDMALayout(), que ya usa SRC_ROW_FIRST_OUT

	if (!CalculateDMALayout(s_dmaBigBuffer, TFT_DMA_BUFFER_TOTAL_BYTES, TFT_SRC_WIDTH, TFT_SRC_HEIGHT, dmalayout))
		{
		DMESG("ERROR CalculateDMALayout(): buffer DMA (%d bytes) demasiado pequeño para %dx%d", (int)TFT_DMA_BUFFER_TOTAL_BYTES, TFT_SRC_WIDTH, TFT_SRC_HEIGHT);
		target_panic(PANIC_SCREEN_ERROR);
		}
		
	DMESG("TFT %dx%d (zoom x%d/%d) <- framebuffer Arcade %dx%d",TFT_OUT_WIDTH, TFT_OUT_HEIGHT, TFT_ZOOM_NUM, TFT_ZOOM_DEN, TFT_SRC_WIDTH, TFT_SRC_HEIGHT);
	DMESG("DMA A -> Off. %p con %d lineas de salida (%d bytes)",dmalayout.dmaA,(unsigned)dmalayout.dmaLines,(unsigned)dmalayout.dmaSize);
	DMESG("DMA B -> Off. %p con %d lineas de salida (%d bytes)",dmalayout.dmaB,(unsigned)dmalayout.dmaLines,(unsigned)dmalayout.dmaSize);
	DMESG("Directas (sin copia) -> %d filas de origen (hasta byte-fila %d)",(unsigned)dmalayout.srcRowsDirect,(unsigned)dmalayout.srcFirstPendingYb);
	DMESG("Frame Buffer 4bpp -> Off. %p con %d lineas (%d bytes)",dmalayout.pending,(unsigned)(dmalayout.pendingSize * 2 / TFT_SRC_WIDTH),(unsigned)dmalayout.pendingSize);
	
	
	/*	Preparar interrupciones fin DMA y task nueva para atenderlas cuando suceden
	*/
	
	/*	Semáforo "frame libre": arranca DADO (no hay ningún frame en curso
		todavía) y tarea worker que continúa el envío async del frame,
		despertada por la IRQ de fin de DMA (dma_trans_done_cb). Se crean
		antes de spi_bus_add_device() porque el post_cb ya puede empezar a
		dispararse en cuanto haya transacciones en la cola. */
	s_frameFreeSem = xSemaphoreCreateBinary();
	xSemaphoreGive(s_frameFreeSem);

	xTaskCreatePinnedToCore(st7735_dma_worker_task,"st7735_dma", 3 * 1024, NULL,configMAX_PRIORITIES - 2, &s_dmaTaskHandle, DMA_CPU);
	
	
	/*	Configuracion para ST7735
	*/
	
	// 1) Pines de control como salidas digitales
	gpio_config_t io_conf = {};
	io_conf.pin_bit_mask = (1ULL << TFT_PIN_DC) | (1ULL << TFT_PIN_RST);
	#if TFT_PIN_BL >= 0
		io_conf.pin_bit_mask |= (1ULL << TFT_PIN_BL);
	#endif
	io_conf.mode         = GPIO_MODE_OUTPUT;
	io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.intr_type    = GPIO_INTR_DISABLE;
	gpio_config(&io_conf);
	#if TFT_PIN_BL >= 0
		gpio_set_level((gpio_num_t)TFT_PIN_BL, 1);			// backlight encendido
	#endif

	// 2) Reset hardware del panel (tiempos típicos del datasheet ST7735)
	rst_set_level(1);
	vTaskDelay(pdMS_TO_TICKS(10));
	rst_set_level(0);
	vTaskDelay(pdMS_TO_TICKS(20));
	rst_set_level(1);
	vTaskDelay(pdMS_TO_TICKS(150));

	// 3) Bus SPI: pines, y frecuencia/formato correctos para el ST7735
	#ifdef __EMSCRIPTEN__
	
		// no hay bus SPI real que inicializar: basta con un handle FAKE no-nulo,
		// que es lo único que usan spi_device_polling_transmit()/spi_device_queue_trans() (más arriba)
		s_spi = (spi_device_handle_t)1;
	
	#else
		
		spi_bus_config_t buscfg = {};
		buscfg.mosi_io_num     = TFT_PIN_MOSI;
		buscfg.miso_io_num     = -1;							// no se usa (el ST7735 aquí sólo escribe)
		buscfg.sclk_io_num     = TFT_PIN_SCK;
		buscfg.quadwp_io_num   = -1;
		buscfg.quadhd_io_num   = -1;
		buscfg.max_transfer_sz = TFT_DMA_BUFFER_TOTAL_BYTES;	// como el buffer va creciendo, marcamos el maximo por si acaso ...
		
		esp_err_t err = spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
		if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)		// INVALID_STATE = ya estaba inicializado antes
			{
			DMESG("ERROR spi_bus_initialize() = %d", (int)err);
			target_panic(PANIC_SCREEN_ERROR);
			}

		spi_device_interface_config_t devcfg = {};
		devcfg.clock_speed_hz = TFT_SPI_HZ;
		devcfg.mode           = 0;								// ST7735 -> SPI modo 0 (CPOL=0, CPHA=0)
		devcfg.spics_io_num   = TFT_PIN_CS;
		devcfg.queue_size     = 2;								// doble buffer DMA: hasta 2 transacciones en vuelo/en cola a la vez
		devcfg.flags          = SPI_DEVICE_NO_DUMMY;
		devcfg.post_cb        = dma_trans_done_cb;				// dispara interrupcion al finalizar envio SPI por DMA
		
		err = spi_bus_add_device(TFT_SPI_HOST, &devcfg, &s_spi);
		if (err != ESP_OK)
			{
			DMESG("ERROR spi_bus_add_device() = %d", (int)err);
			target_panic(PANIC_SCREEN_ERROR);
			}
	#endif
	
	// 4) Secuencia de arranque del controlador TFT hardware
	#if defined(TFT_ST7735_160x128)
	
		st7735_hal_write_command(0x01);					// SWRESET (software reset)
		vTaskDelay(pdMS_TO_TICKS(150));

		st7735_hal_write_command(0x11);					// SLPOUT (salir de modo sleep)
		vTaskDelay(pdMS_TO_TICKS(150));

		{
		uint8_t colmod = 0x05;							// 16 bits/pixel (RGB565)
		st7735_hal_write_command(0x3A);					// COLMOD
		st7735_hal_write_data(&colmod, 1);
		}
		vTaskDelay(pdMS_TO_TICKS(10));

		{
		// MADCTL: orientación horizontal 160x128 (landscape), origen arriba-izq.
		// Si la imagen sale rotada o en espejo, probar 0x60 / 0xC0 / 0x00 / 0x40.
		uint8_t madctl = 0x60;
		st7735_hal_write_command(0x36);					// MADCTL
		st7735_hal_write_data(&madctl, 1);
		}

		st7735_hal_write_command(0x13);					// NORON (modo de imagen normal)
		vTaskDelay(pdMS_TO_TICKS(10));

		st7735_hal_write_command(0x29);					// DISPON (encender panel)
		vTaskDelay(pdMS_TO_TICKS(100));

		DMESG("ST7735: SPI inicializado (SCK=%d MOSI=%d CS=%d DC=%d RST=%d @ %d Hz)",TFT_PIN_SCK, TFT_PIN_MOSI, TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST, TFT_SPI_HZ);
	
	#elif defined(TFT_ILI9341_320x240)
		
		/*	--- ILI9341, secuencia de arranque estándar (power control + gamma) ---
			Comandos y valores son los típicos "de fábrica" usados por la mayoría
			de librerías ILI9341 (Adafruit, TFT_eSPI, etc.); funcionan bien en la
			inmensa mayoría de módulos 320x240 con este chip.
		*/

		st7735_hal_write_command(0x01);					// SWRESET (software reset)
		vTaskDelay(pdMS_TO_TICKS(150));

		{ uint8_t d[] = {0x39,0x2C,0x00,0x34,0x02}; st7735_hal_write_command(0xCF); st7735_hal_write_data(d,3); }		// PWCTRLB (ojo: 3 primeros bytes)
		{ uint8_t d[] = {0x64,0x03,0x12,0x81};      st7735_hal_write_command(0xED); st7735_hal_write_data(d,4); }		// POSC
		{ uint8_t d[] = {0x85,0x00,0x78};           st7735_hal_write_command(0xE8); st7735_hal_write_data(d,3); }		// DTCA
		{ uint8_t d[] = {0x39,0x2C,0x00,0x34,0x02}; st7735_hal_write_command(0xCB); st7735_hal_write_data(d,5); }		// PWCTRLA
		{ uint8_t d[] = {0x00,0x00};                st7735_hal_write_command(0xEA); st7735_hal_write_data(d,2); }		// DTCB
		{ uint8_t d[] = {0x20};                     st7735_hal_write_command(0xF7); st7735_hal_write_data(d,1); }		// PRC (pump ratio)

		{ uint8_t d[] = {0x23}; st7735_hal_write_command(0xC0); st7735_hal_write_data(d,1); }		// PWCTR1
		{ uint8_t d[] = {0x10}; st7735_hal_write_command(0xC1); st7735_hal_write_data(d,1); }		// PWCTR2
		{ uint8_t d[] = {0x3E,0x28}; st7735_hal_write_command(0xC5); st7735_hal_write_data(d,2); }	// VMCTR1
		{ uint8_t d[] = {0x86}; st7735_hal_write_command(0xC7); st7735_hal_write_data(d,1); }		// VMCTR2

		{
		// MADCTL: orientación horizontal 320x240 (landscape), BGR (color habitual en estos módulos).
		// Si la imagen sale rotada, en espejo o con colores rojo/azul cambiados,
		// probar 0x08 / 0x48 / 0x88 / 0xC8 / 0x68 / 0xA8 / 0xE8 / 0x28.
		uint8_t madctl = 0xE8;
		st7735_hal_write_command(0x36);					// MADCTL
		st7735_hal_write_data(&madctl, 1);
		}

		{ uint8_t colmod = 0x55; st7735_hal_write_command(0x3A); st7735_hal_write_data(&colmod, 1); }	// COLMOD: 16 bits/pixel (RGB565)

		{ uint8_t d[] = {0x00,0x18}; st7735_hal_write_command(0xB1); st7735_hal_write_data(d,2); }		// FRMCTR1
		{ uint8_t d[] = {0x08,0x82,0x27}; st7735_hal_write_command(0xB6); st7735_hal_write_data(d,3); }	// DFC (display function control)

		{ uint8_t d[] = {0x00}; st7735_hal_write_command(0xF2); st7735_hal_write_data(d,1); }		// 3GAMMA_EN (desactivado)
		{ uint8_t d[] = {0x01}; st7735_hal_write_command(0x26); st7735_hal_write_data(d,1); }		// GAMSET (curva gamma 1)

		{ uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
		  st7735_hal_write_command(0xE0); st7735_hal_write_data(d,15); }							// GMCTRP1 (gamma +)
		{ uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
		  st7735_hal_write_command(0xE1); st7735_hal_write_data(d,15); }							// GMCTRN1 (gamma -)

		st7735_hal_write_command(0x11);					// SLPOUT (salir de modo sleep)
		vTaskDelay(pdMS_TO_TICKS(120));

		st7735_hal_write_command(0x29);					// DISPON (encender panel)
		vTaskDelay(pdMS_TO_TICKS(100));

		DMESG("ILI9341: SPI inicializado (SCK=%d MOSI=%d CS=%d DC=%d RST=%d @ %d Hz)",TFT_PIN_SCK, TFT_PIN_MOSI, TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST, TFT_SPI_HZ);
		
	#elif defined(TFT_ILI9488_480x320)

		/*	--- ILI9488, secuencia de arranque estándar (power control + gamma) ---
			COLMOD = 0x66 (18 bits/píxel, 3 bytes por píxel): en modo SPI de
			3/4 hilos la ILI9488 no acepta de forma fiable 0x55 (16 bits/píxel)
			en muchos módulos -> ver compute_pixel_bytes()/s_palette888 más arriba.
		*/

		st7735_hal_write_command(0x01);					// SWRESET (software reset)
		vTaskDelay(pdMS_TO_TICKS(150));

		st7735_hal_write_command(0x11);					// SLPOUT (salir de modo sleep)
		vTaskDelay(pdMS_TO_TICKS(120));

		{ uint8_t d[] = {0x0F,0x0F,0x0F}; st7735_hal_write_command(0xC0); st7735_hal_write_data(d,3); }	// PWCTR1
		{ uint8_t d[] = {0x42};           st7735_hal_write_command(0xC1); st7735_hal_write_data(d,1); }	// PWCTR2
		{ uint8_t d[] = {0x00,0x00};      st7735_hal_write_command(0xC5); st7735_hal_write_data(d,2); }	// VMCTR1

		{
		// MADCTL: orientación horizontal, BGR (habitual en estos módulos).
		// Si sale rotada/espejada o con rojo-azul cambiado, probar 0x28/0x68/0x88/0xC8/0xE8.
		uint8_t madctl = 0x28;
		st7735_hal_write_command(0x36);					// MADCTL
		st7735_hal_write_data(&madctl, 1);
		}

		{ uint8_t colmod = 0x66; st7735_hal_write_command(0x3A); st7735_hal_write_data(&colmod, 1); }	// COLMOD: 18 bits/pixel (RGB666, 3 bytes/pixel)

		{ uint8_t d[] = {0x00};      st7735_hal_write_command(0xB0); st7735_hal_write_data(d,1); }		// interface mode control
		{ uint8_t d[] = {0xB0,0x11}; st7735_hal_write_command(0xB1); st7735_hal_write_data(d,2); }		// frame rate (~70Hz)
		{ uint8_t d[] = {0x02};      st7735_hal_write_command(0xB4); st7735_hal_write_data(d,1); }		// display inversion control
		{ uint8_t d[] = {0x02,0x02,0x3B}; st7735_hal_write_command(0xB6); st7735_hal_write_data(d,3); }	// display function control

		{ uint8_t d[] = {0x0F,0x24,0x1C,0x0A,0x0F,0x08,0x43,0x88,0x32,0x0F,0x10,0x06,0x0F,0x07,0x00};
		  st7735_hal_write_command(0xE0); st7735_hal_write_data(d,15); }							// gamma +
		{ uint8_t d[] = {0x0F,0x38,0x30,0x09,0x0F,0x0F,0x4E,0x77,0x3C,0x07,0x10,0x05,0x23,0x1B,0x00};
		  st7735_hal_write_command(0xE1); st7735_hal_write_data(d,15); }							// gamma -

		st7735_hal_write_command(0x29);					// DISPON (encender panel)
		vTaskDelay(pdMS_TO_TICKS(100));

		DMESG("ILI9488: SPI inicializado (SCK=%d MOSI=%d CS=%d DC=%d RST=%d @ %d Hz)",TFT_PIN_SCK, TFT_PIN_MOSI, TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST, TFT_SPI_HZ);

	#endif
	
}

/*	st7735_core_update_screen() - ESP32 real, version asincrona

	Objetivo: devolver el control lo antes posible. Solo se hace de forma
	asincrona (bloqueando la CPU) lo estrictamente necesario:
	  1) esperar (si hiciera falta) a que el frame ANTERIOR haya terminado
	     de enviarse del todo -normalmente ya lo estara¡-,
	  2) copiar a 'dmalayout.pending' SOLO la parte del framebuffer de
	     MakeCode que todavia no se va a procesar en este mismo instante
	     (las primeras 'srcRowsDirect' filas se leen directas de packed4bpp,
	     sin copia; el resto si hace falta copiarlo porque Arcade puede
	     empezar a pintar el siguiente frame encima en cuanto volvamos),
	  3) rellenar los DOS buffers DMA (ping-pong),
	  4) encolar el envio del primero.

	A partir de aqui se devuelve el control. El resto del frame (encolar el
	segundo buffer y todos los siguientes, rellenando cada vez el que queda
	libre) lo continuara a la tarea worker, disparada por la interrupcion de fin de DMA
*/
void st7735_core_update_screen(const uint8_t *packed4bpp, int width, int height,const uint32_t palette_or_null[16])
{
	
	//DMESG("    GURU - st7735_core_update_screen()");
	
	if (!packed4bpp) return;

	if (palette_or_null)
		{
		// PENIENTE
		//st7735_core_set_palette(palette_or_null);
		}

	// 1) Nos aseguramos de que el frame ANTERIOR ya se envio por completo.
	uint32_t cycStart = XTHAL_GET_CCOUNT();
	xSemaphoreTake(s_frameFreeSem, portMAX_DELAY);
	s_dmaWaitStartCycles = XTHAL_GET_CCOUNT() - cycStart;

	cycStart = XTHAL_GET_CCOUNT();
	s_frameStartCycles = cycStart;
	
	// este frame (ya sea con invalidate o full franme copy) ya se esta enviando
	precalculatedscene.ir_unsent = false;
	
	
	/*	En este momento vemos si hay que hacer full frame o solo los invalidate rects
	*/
	
	#ifdef TFT_PARTIAL_UPDATE
		
		// hay invalidate rects
		if ( precalculatedscene.ir_back_count > 0)
			{
				
			// inicia el envio por invalidate rectangles, pero si algo no va bien, deja pasar a full frame
			if (rect_begin(packed4bpp, width, height))
				{
				s_cpuCycles = XTHAL_GET_CCOUNT() - cycStart;
				return;
				}
				
			}
		
	#endif
	
	// camino sin INVALIDATE RECTANGLES
	s_rectMode = false;
	
	
	// 2) Copiamos SOLO lo que aun no se va a procesar ya mismo ('srcRowsDirect' primeras filas -> se leeran directas de packed4bpp en el paso 3,
	//    todavia es valido). El resto se copia a 'pending' YA TRASPUESTO a row-major, para que dma_fill_pending() lo recorra secuencialmente.

	int bytesPerCol = (height + 1) >> 1;
	pending_copy_transposed(packed4bpp, width, bytesPerCol);
	
	s_frameSrc         = packed4bpp;										// valido solo durante esta llamada sincrona
	s_frameWidth       = width;												// dimensiones del framebuffer de Arcade (sin zoom)
	s_frameHeight      = height;
	s_frameBytesPerCol = bytesPerCol;
	s_frameCursor      = 0;
	s_frameTotalPixels = (size_t)TFT_OUT_WIDTH * (size_t)TFT_OUT_HEIGHT;		// píxeles realmente enviados (con zoom)
	s_readyIdx         = -1;
	
	elastic_reset_for_new_frame();											// dmaA/dmaB vuelven a los tamaños/punteros "de fábrica" de dmalayout para este frame nuevo

	st7735_core_set_addr_window(0, 0, TFT_OUT_WIDTH, TFT_OUT_HEIGHT);
	st7735_hal_write_command(ST7735_RAMWR);
	dc_data();			// a partir de aqui solo se mandan datos (pixeles) hasta el proximo comando
	
	/*	3) Rellenamos los DOS buffers
		OJO al orden: dmaB (idx 1) se rellena con el PRIMER trozo del frame (filas [0, dmaLines))
		y dmaA (idx 0) con el SEGUNDO (filas [dmaLines, 2*dmaLines)); dmaB es el
		que se manda primero.
	*/
	size_t n1 = dma_fill_chunk(1);											// dmaB: primer trozo a enviar
	size_t n0 = dma_fill_chunk(0);											// dmaA: segundo trozo a enviar (puede quedar en 0 si el frame cupiera en un unico chunk)

	// 4) Encolamos el envio del primero (dmaB) y devolvemos el control YA. El segundo (dmaA) ya queda "listo" (s_readyIdx)
	if (n0 > 0)
		{
		s_dmaTrans[0].length = n0 * TFT_BITS_PER_PIXEL;		// aparcado para cuando la tarea worker lo encole
		s_readyIdx = 0;
		}
	//DMESG("    GURU - primer DMA");
	dma_queue_send(1, n1);
	
	s_cpuCycles = XTHAL_GET_CCOUNT() - cycStart;
	
	//DMESG("    GURU - fin st7735_core_update_screen()");
}


/*	Funciones nativas en C++ que se pueden llamar desde la VM .PXT64
*/

//% expose
int setScreenBrightnessSupported() {
    return 0;
}

//% expose
void setScreenBrightness(int level) {
    // TODO
}

//% expose
void setPalette(Buffer buf)
{
	
	TRACE_LOOP("        Update Arcade palette");
	
    if (48 != buf->length) target_panic(PANIC_SCREEN_ERROR);
    
#ifdef __EMSCRIPTEN__
	for (int i = 0; i < 16; ++i)
		{
        
		uint8_t b = buf->data[i * 3];
        uint8_t g = buf->data[i * 3 + 1];
        uint8_t r = buf->data[i * 3 + 2];
        
		currPalette[i] = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
		
		}
#endif
	
}

//% expose
void setCRCmaps(Buffer buf , Buffer bufmappaths , Buffer buffg , Buffer bufsp)
{
	
	/*	Comprobaciones de integridad
	*/
	
	s_mpBuf = nullptr;
	s_mpathb = nullptr;
	s_fgMask = nullptr;
	s_mpathsp = nullptr;
	
	if (!buf || buf->length < 6)					return;
	if (!bufmappaths || bufmappaths->length < 6)	return;
	if (!buffg || buffg->length < 6)				return;
	
	const uint8_t *datamappaths = bufmappaths->data;

	uint16_t tipo  = datamappaths[0] | (datamappaths[1] << 8);
	uint16_t ancho = datamappaths[2] | (datamappaths[3] << 8);
	uint16_t alto  = datamappaths[4] | (datamappaths[5] << 8);

	if (tipo != 14 || ancho == 0 || alto == 0)
		{
		DMESG("ERROR setCRCmaps() - recurso invalido (no es CRC tipo 14) (ancho,alto)->(%d,%d)",ancho,alto);
		s_mpathb = nullptr;
		return;
		}
		
	// solo para EMSCRIPTEN y el sistema de DEBUG de MapPaths
	#ifdef __EMSCRIPTEN__
		
		s_mpathWidth  = ancho * MPATH_CELL;
		s_mpathHeight = alto  * MPATH_CELL;
		
		s_pathCacheWy = -1;
		
	#endif
	
	const uint8_t *data = buf->data;

	tipo  = data[0] | (data[1] << 8);
	ancho = data[2] | (data[3] << 8);
	alto  = data[4] | (data[5] << 8);

	if (tipo != 19 || ancho == 0 || alto == 0 || (ancho % 4) != 0 || (alto % 4) != 0)
		{
		DMESG("ERROR setCRCmaps() - recurso invalido (no es CRC tipo 19)");	
		s_mpBuf = nullptr;
		return;
		}
	
	
	/*	Recogemos el buffer CRC rsc tipo 19 multipaleta lo procesamos y guardamos en las estructiras
	*/
	
	int tilesAncho      = ancho / 4;
	int tilesAlto       = alto / 4;
	int tileTableBytes  = (tilesAncho * tilesAlto) / 2;

	s_mpWidth   = ancho;
	s_mpHeight  = alto;
	s_mpPalette = (const uint16_t *)(data + 6);				// 64 x uint16 LE
	s_mpTilePal = data + 6 + (64 * 2);
	s_mpPixels  = s_mpTilePal + tileTableBytes;

	// Resolvemos las 64 combinaciones de la paleta UNA sola vez aqui (no en
	// cada pixel de fondo): el generador guarda cada color con cr en bits0-4,
	// cg en bits5-10, cb en bits11-15, y ADEMAS le hace un intercambio de
	// bytes manual antes de guardarlo -> lo deshacemos aqui, reordenamos a
	// RGB565 estandar (s_mpPaletteStd) y de paso dejamos tambien la version
	// ya con el byte-swap final hecho (s_mpPaletteWire), lista para el
	// camino rapido zoom x2 RGB565 (ver multipaletteRGB565WireAt()).
	for (int i = 0; i < 64; i++)
		{
		uint16_t raw = s_mpPalette[i];
		uint16_t v   = (uint16_t)((raw << 8) | (raw >> 8));

		uint16_t r = v & 0x1F;
		uint16_t g = (v >> 5) & 0x3F;
		uint16_t b = (v >> 11) & 0x1F;

		uint16_t std = (uint16_t)((r << 11) | (g << 5) | b);

		s_mpPaletteStd[i]  = std;
		s_mpPaletteWire[i] = (uint16_t)((std << 8) | (std >> 8));
		}

	s_mpBuf     = data;											// marcamos como activo al final

	// invalida el cache de fila (recurso/dimensiones nuevas)
	s_mpCacheWy = -1;
	
	
	/*	Ahora recogemos el MapPaths y lo procesamos y guardamos
	*/
	
	// saltamos cabecera y hacemos que apunte directamente a la matriz de celdas (asi lo requieren las demas funciones)
	s_mpathb=datamappaths + 6;
	
	
	/*	Ahora recogemos el Foreground y lo procesamos y guardamos
	*/
	
	s_fgCacheCy = -1;
	s_fgMaskW = s_mpWidth  >> 1;
	s_fgMaskH = s_mpHeight >> 1;
	s_fgStride = (s_fgMaskW + 7) >> 3;
	s_fgMask  = buffg->data + 6;			// saltamos cabecera y hacemos que apunte directamente a la matriz de celdas (asi lo requieren las demas funciones)
	
	
	/*	Por ultimo el buffer de Special Zones
	*/
	s_mpathsp=bufsp->data;
	
}

//% expose
void setCRCbackgroundOffset(int offsetX, int offsetY)
{
	s_mpOffsetX = offsetX;
	s_mpOffsetY = offsetY;
}

// RIC esta es la funcion que llama Makecode cuando quiere actualizar visiblemente la pantalla
//% expose
void updateScreen(Image_ img)
{
	
	if (!img) return;

    if (img->bpp() != 4 || img->width() != TFT_SRC_WIDTH || img->height() != TFT_SRC_HEIGHT) target_panic(PANIC_SCREEN_ERROR);
	
	// FAKE para EMSCRIPTEN para esperar el final del DMA de pintar todo el framebuffer
	#if defined(__EMSCRIPTEN__) && !defined(EMSCRIPTEN_SIN_EMULACION_TFT)
	
		// definimos que esta en otro fichero
		void schedule();
		
		if (!xSemaphorePeek(s_frameFreeSem))
			{
			currentFiber->wakeTime = current_time_ms() + 0;
			schedule();
			return;
			}
		
	#endif
	
	/*	Simulacion del COSTE de CPU de la VM del ESP32
	*/	
	#ifdef VM_COST_ENABLED
		
		/*	Si el ESP32 habria tardado mas en ejecutar este frame que el tiempo real que ha pasado, esperamos
			lo que falta, SIN bloquear el navegador: marcamos wakeTime y llamamos a schedule(), que rompe
			exec_loop() y deja resumePC apuntando a ESTA misma llamada, asi que al despertar se re-ejecuta
			updateScreen() desde cero (por eso hace falta el estado 'static' de aqui abajo).
		*/
		
		{
		
		void schedule();										// definida en scheduler.cpp
	
		static bool     s_costWaiting = false;					// true mientras esperamos (la llamada se re-ejecuta tras cada schedule())
		static uint64_t s_costFrameT0Us = 0;					// inicio del frame actual (= fin del anterior)
		static uint64_t s_costDueUs = 0;						// instante en que 'termina' el frame en el ESP32 simulado
	
		static uint32_t s_costFrames = 0;						// estadisticas para DMESG
		static uint64_t s_costSumSimUs = 0;
		static uint64_t s_costSumPeriodUs = 0;
	
		uint64_t nowUs = (uint64_t)esp_timer_get_time();
	
		if (s_costFrameT0Us == 0) s_costFrameT0Us = nowUs;		// primer frame, aun no hay referencia
	
		// el limite se fija UNA vez por frame (no al volver de cada espera) para que no se vaya alejando
		if (!s_costWaiting)
			{
			s_costDueUs = s_costFrameT0Us + (::pxt::g_vmCostNs / 1000);
	
			// tope de seguridad: nunca esperamos mas de 0,5 s de golpe (p.ej. tras una carga larga sin pintar)
			if (s_costDueUs > nowUs + 500000) s_costDueUs = nowUs + 500000;
			}
	
		if (nowUs + 1000 <= s_costDueUs)						// faltan 1 ms o mas
			{
			s_costWaiting = true;
			currentFiber->wakeTime = current_time_ms() + (int)((s_costDueUs - nowUs + 999) / 1000);
			schedule();
			return;
			}
	
		// frame simulado completado, empezamos el siguiente. Si hemos salido con menos de 1 ms de adelanto
		// sobre el limite, el siguiente frame arranca en ese limite (asi el margen no se acumula como error)
		uint64_t endUs = (s_costDueUs > nowUs) ? s_costDueUs : nowUs;
	
		s_costSumSimUs    += ::pxt::g_vmCostNs / 1000;
		s_costSumPeriodUs += endUs - s_costFrameT0Us;
		if (++s_costFrames >= 100)
			{
			/*
			DMESG("VMcost: ESP32 simulado %d us/frame , periodo real %d us/frame",
				(int)(s_costSumSimUs / s_costFrames),
				(int)(s_costSumPeriodUs / s_costFrames));
			*/
			s_costFrames = 0;
			s_costSumSimUs = 0;
			s_costSumPeriodUs = 0;
			}
	
		s_costWaiting = false;
		::pxt::g_vmCostNs = 0;
		s_costFrameT0Us = endUs;
		
		}
	
	#endif
	
	// RIC DEBUG
	{
	TRACE_LOOP("        Update Arcade screen buffer -> to Hardware screen");
	static int countupdatesscreens=0;
	static uint64_t starttimeupdatesscreens=esp_timer_get_time();
	countupdatesscreens++;
	if (countupdatesscreens >= 100)
		{
		uint32_t elapsed=esp_timer_get_time() - starttimeupdatesscreens;
		elapsed=elapsed / 1000;		// en mseg.
		/*
		DMESG("FPS=%d  TFTsend %dms (CPU %d + DMA %d) , UpdateScreen waits DMA %dms",
			  (100 * 1000) / elapsed,
			  tft_cycles_to_ms(s_lastFrameCycles),
			  tft_cycles_to_ms(s_lastCPUCycles),
			  tft_cycles_to_ms(s_lastFrameCycles - s_lastCPUCycles),
			  tft_cycles_to_ms(s_dmaWaitStartCycles) );
		*/
		starttimeupdatesscreens = esp_timer_get_time();
		countupdatesscreens=0;
		}
	
	}
	
	#if defined(__EMSCRIPTEN__)
		
		// sistema directo de Javascript accediendo al frame buffer
		memcpy(screenBuf, img->pix(), img->pixLength());
		
	#endif
	
	/*	DEPRECATED
	// simulamos N mseg. de pintar el hardware de la pantalla
	uint64_t endp = esp_timer_get_time() + (MSEG_TFT * 1000);
	while (esp_timer_get_time() < endp)
		{}
	*/
	
	// envia via SPI al TFT ST7735 (debe estar inicializado previamente) 
	st7735_core_update_screen(img->pix(),TFT_SRC_WIDTH,TFT_SRC_HEIGHT,NULL);
	
}

// RIC
//% expose
void updateStats(String msg)
{
	DMESG("%s", msg->getUTF8Data());
}

} // namespace pxt