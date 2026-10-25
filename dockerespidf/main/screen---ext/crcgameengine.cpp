
/*	RIC como este .cpp no es originario de ArcadeMakecode,
	tengo que añadirlo a \screen---ext\pxt.json para que
	el compilador PXT encuentre las funciones %expose
	y ademas estan tienen que estar en una linea sin saltos, para que el parser lo detecte bien !!!
*/

#include "pxt.h"

#ifdef __EMSCRIPTEN__

	#include <emscripten.h>
	#include "ESPIDFfake.h"
	
	// para usar las pausas con sleep en EMSCRIPTEN
	#include <thread>
	#include <chrono>
	
#else
	
#endif

// Sistema de unicon de invalidate rectangles
#define INVALIDATE_RECT_APLICAR_ESTUDIO_COSTES_DE_TIEMPO

// Deben coincidir exactamente con las constantes entre crcgameengine.cpp <-> scene.ts
	/*	limites !!! son 32 bits que se guardan, pero en PXT smallint usa 31 bits,
		asi que para curarse en salud 30 bits -> 30 Events diferentes
		de 1 << 0 a 1 << 29 */
#define EVT_FFD							(1 << 0)		// Function Fall Down
#define EVT_FFS							(1 << 1)		// Function Fall Stops
#define EVT_FWS							(1 << 2)		// Function Wall Stops
#define EVT_FAF							(1 << 3)		// Function About To Fall
#define EVT_FSZ							(1 << 4)		// Function Special Zone
#define EVT_FTS							(1 << 5)		// Function Touch solid
#define EVT_FCS							(1 << 6)		// Funcion Ceil Stops
#define EVT_FSI							(1 << 7)		// Function Sprite Inclination -> ademas devuelve la inclinacion actual (1 plano, 2 subidita, 3 bajadita)
#define EVT_FZY							(1 << 8)		// Function Z-order on change Y -> la Y de los pies ha cambiado, TS recalcula el z
	// ...
#define FLG_PTFM						(1 << 29)		// bit de ESTADO (no es un callback) el SPRITE esta sobre una plataforma	

#ifdef PXT_VM				// namespace pxt
namespace pxt
{
#endif


ESTRUCTURA_PRECALCULATED_SCENE precalculatedscene;						// en C no se puede hacer estatica, pero creo que asi es global y es lo mismo
	
// definimos por que esta en otro fichero
extern const uint8_t *s_mpathb;
extern const uint8_t *s_mpathsp;

// variables que guardamos globalmente porque NO podemos pasar por parametros en PXT VM
int g_cellw=2;
int g_cellh=2;
int g_xsca;
int g_ysca;
float g_fvy;
bool g_fts;
bool g_controljs;
int g_comportamientoatravesable=0;
int g_comportamientospecial=0;
float g_fay=0.0;
int g_sprinc=0;						// 'inc' actual del SPRITE: 0 no sabemos, 1 plano, 20.. subidita, 30.. bajadita (lo pasa TS en GMA_VMprepareParameters)
bool g_fsi;
bool g_ptfm;
int g_ysuelominimo=0;
int g_niveldebug=5;					// 0: NO debug , 1 .... 5
int g_MAX_PIXELS_PELDANYO=4;
int g_MAX_CELDAS_CAIDA=4;

// gestion de los callbacks en PXT
uint32_t cbkflags=0;
uint32_t g_listenMask=0;

int contadordbg=0;


#ifdef PXT_VM

	/*	Los SPRITEs en Arcade Makecode tienen estos atributos:
	
		class Sprite extends sprites.BaseSprite
			{
				_x y _y: Almacenan internamente la posición de la esquina superior izquierda en punto fijo (Fx8)
				pero en main.TS cuando asignamos sprite.x o sprite.y es del centro, y se calcula automaticamente
			_x: Fx8
			_y: Fx8
			
			_vx: Fx8
			_vy: Fx8
			_ax: Fx8
			_ay: Fx8
			_fx: Fx8 // friction
			_fy: Fx8 // friction
			_sx: Fx8 // scale
			_sy: Fx8 // scale
			_width: Fx8 // scaled width
			_height: Fx8 // scaled height
			_rotatedBBox: sprites.RotatedBoundingBox;
			
			_kind: number;
			
			layer: number;

			_lastX: Fx8;
			_lastY: Fx8;

			_action: number; //Used with animation library
			
			lifespan: number;
			
			private _image: Image;
			private _obstacles: sprites.Obstacle[];

			private sayEndTime: number;
			private sayRenderer: sprites.BaseSpriteSayRenderer;

			_hitbox: game.Hitbox;
			_overlappers: number[];
			_alreadyChecked: number[];
			_kindsOverlappedWith: number[];

			flags: number
			
			...
			}
			
	*/
	
#endif


// Emulamos esta funcion que es de Espruino
void jsiConsolePrintf(const char *fmt, ...)
{
	// en emscripten el stdout esta redirigido a la console.log de javascript ...
	
	va_list args;          		// Define un objeto va_list para manejar los argumentos variables
	va_start(args,fmt);			// Inicializa args para apuntar al primer argumento variable
	
		vdmesg(fmt,args);
	
	va_end(args);           	// Limpia el objeto va_list
}

// RIC idea sea compatible CRC Game Engine
int GMA_MapaPathsFromIndex(int indice)
{

	// RIC elimino de momento la comprobacion del tipo de matriz (asumo siempre sera tipo 3)
	/*
	if (precalculatedscene.tipomatrizpath == 3)
		{	
		//jsiConsolePrintf("RIC debug : Indice = %d\n",indice);
		
		int bitdesplazar=( 6 - ((indice % 4) << 1) );
		return( ( precalculatedscene.matrizpath[ (indice >> 2) ] >> bitdesplazar ) & 0b00000011 );
		}
	
	return 0;
	*/
	
	#ifdef PXT_VM
	
		return( ( s_mpathb[ (indice >> 2) ] >> ( 6 - ((indice % 4) << 1) ) ) & 0b00000011 );
	
	#else
		
		return( ( precalculatedscene.matrizpath[ (indice >> 2) ] >> ( 6 - ((indice % 4) << 1) ) ) & 0b00000011 );
	
	#endif
	
}

// RIC idea sea compatible CRC Game Engine
#ifdef PXT_VM

//% expose
int GMA_SpriteInSpecialZone(TNumber _x,TNumber _y,int spw,int sph,int scw)
									  
#else
	
int GMA_SpriteInSpecialZone(JsVar *objspr)

#endif
{
	
	#ifdef PXT_VM
		
		// ya nos viene por parametros -> int scw=precalculatedscene.scw;
		int cellw=g_cellw;
		int cellh=g_cellh;
		
		// recojemos las posiciones del centro sin decimales (que es como trabajamos aqui en los calculos)
		int xsc=(int)( toFloat(_x) + (spw >> 1) );
		int ysc=(int)( toFloat(_y) + (sph >> 1) );
		
		// calculo para movernos por el Map Path
		int cellperline=(scw / cellw);

		// ya nos viene por parametros -> int spw=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"ancho",0));
		// ya nos viene por parametros -> int sph=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"alto",0));
		
	# else
		
		int scw=precalculatedscene.scw;
		int cellw=precalculatedscene.cellw;
		int cellh=precalculatedscene.cellh;
		
		// recojemos las posiciones del centro sin decimales (que es como trabajamos aqui en los calculos)
		int xsc=(int)jsvGetFloatAndUnLock(jsvObjectGetChild(objspr,"xsc",0));
		int ysc=(int)jsvGetFloatAndUnLock(jsvObjectGetChild(objspr,"ysc",0));
		
		// calculo para movernos por el Map Path
		int cellperline=(scw / cellw);
		
		int spw=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"ancho",0));
		int sph=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"alto",0));
		
	#endif
	
	// nuevas posiciones (respecto a la escena , NO a la TFT)
		int xoi=xsc - (spw >> 1);								// valores originales recordamos
		int yoi=ysc - (sph >> 1);								// valores originales recordamos
	int xi=xoi;
	int xf=xoi + spw - 1;
	int yi=yoi;
	int yf=yoi + sph - 1;
	
	// variables para gestionar los pixels de Map Paths del SPRITE
	int valorini,valormed,valorfin;
	int indiceini,indicemed,indicefin;
	
	
	/*	Hacemos un recorrido en cruz horizontal y vertical que pase por el centro para ver si hay Special Zone dentro del SPRITE
	*/
	
	
	/*	Primero vertical ...
	*/
	
	bool abajodelospies=false;
	indiceini=( (yoi / cellh) * cellperline ) + (xsc / cellw);
	indicefin=( ( (yf+1) / cellh) * cellperline ) + (xsc / cellw);			// hacemos un pixels mas abajo de los pies, para saber si debajo SI que hay SPECIAL ZONE
	while ( (indiceini <= indicefin) && (GMA_MapaPathsFromIndex(indiceini) != 3) )
		{
		indiceini+=cellperline; /* una fila por abajo mas */
		yi+=cellh;				// la Y en pixels se incrementa en multiplos del MapPath
		}
	
	// si ya hay SPECIAL ZONE
	if (indiceini <= indicefin)
		{
			
		if (indiceini == indicefin)								// caso que debajo de los pies tenemos SPECIAL ZONE
			{
			abajodelospies=true;
			}
		
		// como hemos encontrado SPECIAL ZONE verticalmente, usaremos esta Y en vez de la del centro del SPRITE
		ysc=yi;
		
		}
	
	
	/*	Ahora horizontal ...
	*/
	
	int xszini=0;
	int xszfin=0;
	
	indiceini=( (ysc / cellh) * cellperline ) + (xoi / cellw);
	indicefin=( (ysc / cellh) * cellperline ) + (xf / cellw);
	while (indiceini <= indicefin)
		{
		
		if (xszini == 0)
			{
			if (GMA_MapaPathsFromIndex(indiceini) == 3) xszini=xszfin=xi;
			}
		else
			{
			if (GMA_MapaPathsFromIndex(indiceini) == 3) xszfin=xi;
			}
		
		indiceini++;
		xi+=cellw;				// la X en pixels se incrementa en multiplos del MapPath
		}
	
	// si ya hay SPECIAL ZONE
	if (xszini > 0)
		{
		//jsiConsolePrintf("RIC debug : xszini=%d , xszfin=%d , xf=%d \n",xszini,xszfin,xf);
		
		// si es contenido en el SPRITE devolvemos positivo, si es debajo de los pies devolvemos negativo la posX central
		if (abajodelospies)
			{
			return( -( (xszini + xszfin) / 2 ) );
			}
		else
			{
			return( +( (xszini + xszfin) / 2 ) );
			}
		}
	
	// NO estamos en una SPECIAL ZONE
	return 0;
}

// RIC idea sea compatible CRC Game Engine
#ifdef PXT_VM

//% expose
int GMA_SpriteSpecialZoneNumber(TNumber _x,TNumber _y,int spw,int sph)
									  
#else
	
int GMA_SpriteSpecialZoneNumber(JsVar *objspr)

#endif
{
	/*	Devuelve el NUMERO de SPECIAL ZONE (1, 2, 3 ...) que toca el SPRITE, o 0 si no toca ninguna.
		Las zonas estan en s_mpathsp (6 bytes cada una : X 2 bytes, Y 2 bytes (byte ALTO primero), ancho 1 byte, alto 1 byte; pixels de la escena),
		en el mismo orden que su numero. El buffer SIEMPRE termina con 0xFF : si el primer byte de una zona es 0xFF no hay mas zonas
		(el byte alto de una coordenada nunca es 0xFF). Una escena sin zonas es solo 0xFF.
		Si el SPRITE toca varias, devolvemos la primera (la de menor numero) y no seguimos comprobando.
	*/
	
	if (s_mpathsp == NULL) return 0;
	
	// rectangulo del SPRITE respecto a la escena (mismo calculo que GMA_SpriteInSpecialZone)
	int xi=(int)( toFloat(_x) + (spw >> 1) ) - (spw >> 1);
	int xf=xi + spw - 1;
	int yi=(int)( toFloat(_y) + (sph >> 1) ) - (sph >> 1);
	int yf=yi + sph - 1;
	
	/*	Por cada zona hacemos las comprobaciones de menos a mas calculo, y en cuanto una falla pasamos a la siguiente zona sin calcular nada mas.
		Miramos el area del SPRITE ampliada con 1 pixel por debajo de sus pies (yf + 1).
		1) La Y va PRIMERO porque las zonas vienen ordenadas por su Y superior (el generador las busca de arriba a abajo), asi que si la zona
		   empieza por debajo de esa fila ya NINGUNA de las siguientes puede tocar al SPRITE -> terminamos (break).
		2) Despues solo necesitamos la X inicial de la zona, y por ultimo los anchos/altos.
		3) Si la zona empieza por encima o en la ultima fila del SPRITE hay colision (+). Si empieza justo en yf + 1 solo esta debajo de sus pies (-).
		   Como estan ordenadas, una zona que solo esta debajo de los pies siempre va despues de las que colisionan, asi que ya podemos devolver.
	*/
	int z=1;
	for (const uint8_t *p=s_mpathsp; p[0] != 0xFF; p+=6, z++)
		{
		
		// El break de la comprobación 1 depende de que los hex vengan ordenados por Y superior, algo que el generador ya cumple porque numera las áreas de arriba a abajo. Si alguna vez construyes un mapsp a mano en otro orden, esa comprobación debería ser un continue
		int zyi=( (p[2] << 8) | p[3] );
		if (zyi > (yf + 1))						// la zona empieza por debajo de los pies del SPRITE (y las siguientes tambien)
			break;
		
		int zxi=( (p[0] << 8) | p[1] );
		if (xf < zxi)							// la zona empieza a la derecha del SPRITE
			continue;
		
		if (xi >= (zxi + p[4]))					// la zona acaba a la izquierda del SPRITE
			continue;
		
		if (yi >= (zyi + p[5]))					// la zona acaba por encima del SPRITE
			continue;
		
		// colisiona (positivo) o esta justo debajo de los pies (negativo)
		return ( (zyi <= yf) ? z : -z );
		}
	
	// no estamos en ninguna SPECIAL ZONE
	return 0;
}

bool GMA_EsPeldanyoDeBajada(int indicecol1,int indicecol2,int cellperline,int maxfilas,int maxceldas)
{
	/*	Comprueba si por debajo de dos columnas de celdas VACIAS hay un peldaño de bajada (topamos con solido en <= maxceldas filas)
		o si por el contrario es una caida real (mas de maxceldas filas, o nos salimos de la escena por abajo).
		Devuelve true si es peldaño (NO es About To Fall), false si es caida real.
		indicecol1 / indicecol2 son los indices de las celdas (fila de justo debajo de los pies) de las columnas a comprobar.
	*/	
	
	for(int alturaceldas=1;alturaceldas <= maxceldas;alturaceldas++)
		{
		indicecol1+=cellperline;										// una fila mas por debajo
		indicecol2+=cellperline;
		
		// de momento OPTIMIZO velocidad, cuento con que esto NO pasa (MapPaths lo protege)
		//if ( (indicecol1 / cellperline) >= maxfilas ) return false;		// nos salimos de la escena por abajo, es caida
		
		if (GMA_MapaPathsFromIndex(indicecol1) != 0) return true;		// topamos con solido cerca, es peldaño
		if (GMA_MapaPathsFromIndex(indicecol2) != 0) return true;
		}
	
	return false;
}

static const uint32_t COSTE_FRAME_COMPLETO_THRESHOLD = (uint32_t)CALCULO_DE_VALOR_CONSTANTE;

static inline uint32_t CalcularCosteRect(int xi, int yi, int xf, int yf)
{
	int w = (xf - xi + 1);
    int h = (yf - yi + 1);
    if (w <= 0 || h <= 0) return 0;

    uint32_t area = (uint32_t)w * h;
    uint64_t ciclos_transmision = ((uint64_t)area * BYTES_PER_PIXEL * 8 * CPU_FREQ_HZ) / SPI_BUS_HZ;
	
	return (uint32_t)( CICLOS_TFT_SET_COMANDS + ciclos_transmision);
}

// Comprueba si dos rectángulos están solapados o son adyacentes (contiguos a 1 px)
static inline bool EsSolapadoOContiguo(int a_xi, int a_yi, int a_xf, int a_yf,int b_xi, int b_yi, int b_xf, int b_yf)
{
	if (a_xi > b_xf + 1 || b_xi > a_xf + 1 || a_yi > b_yf + 1 || b_yi > a_yf + 1)
		{
        return false;
		}
    return true;
}

// RIC idea sea compatible CRC Game Engine
static inline void AddInvRect(int xi,int yi,int xf,int yf)
{
	// RIC DEBUG
	//jsiConsolePrintf("RIC debug : AddInvRect xi %d yi %d xf %d yf %d\n",xi,yi,xf,yf);
	
	if (xi > SCREEN_WIDTH - 1 || xf < 0 || yi > SCREEN_HEIGHT - 1 || yf < 0) return;

    if (xi < 0) xi = 0;
    if (yi < 0) yi = 0;
    if (xf > SCREEN_WIDTH - 1)  xf = SCREEN_WIDTH - 1;
    if (yf > SCREEN_HEIGHT - 1) yf = SCREEN_HEIGHT - 1;

    auto *r = precalculatedscene.invrects;
    int n = precalculatedscene.ir_front;
	
    // Si ya estamos en modo Full Frame (porque ya no sale a cuenta los invalidate rectangles) no procesamos más
    if (n == 1 && r[0].xi == 0 && r[0].yi == 0 &&  r[0].xf == SCREEN_WIDTH - 1 && r[0].yf == SCREEN_HEIGHT - 1)
		{
        return;
		}
		
	uint32_t coste_acumulado = 0;
    uint32_t coste_nuevo = CalcularCosteRect(xi, yi, xf, yf);		// coste de este nuevo rectangulo que intentamos añadir
	
	// Intentar fusionar solo con rectángulos SOLAPADOS O CONTIGUOS (mirando el coste en tiempo fusionado o separados)
    for (int i = 0; i < n; )
		{
        
		// Verificar únicamente si están solapados o pegados (contiguos)
        if ( EsSolapadoOContiguo(xi, yi, xf, yf, r[i].xi, r[i].yi, r[i].xf, r[i].yf) )
			{
            
			// Bounding box de la unión
            int u_xi = xi < r[i].xi ? xi : r[i].xi;
            int u_yi = yi < r[i].yi ? yi : r[i].yi;
            int u_xf = xf > r[i].xf ? xf : r[i].xf;
            int u_yf = yf > r[i].yf ? yf : r[i].yf;
			
			#ifdef INVALIDATE_RECT_APLICAR_ESTUDIO_COSTES_DE_TIEMPO

				uint32_t coste_a = 		CalcularCosteRect(r[i].xi, r[i].yi, r[i].xf, r[i].yf);
				uint32_t coste_union =	CalcularCosteRect(u_xi, u_yi, u_xf, u_yf);

				// Comprobar coste: fusionar si coste(unión) <= coste(a) + coste(b)
				if ( coste_union <= (coste_a + coste_nuevo) )
			#endif
					{
			
					xi = u_xi;
					yi = u_yi;
					xf = u_xf;
					yf = u_yf;
					
					// El nuevo coste del rectángulo resultante es el de la unión
					coste_nuevo = coste_union;
					
					r[i] = r[--n];
					i = 0; 
					coste_acumulado = 0;			// Reiniciar acumulación al cambiar la lista
					continue;
					}
			
			}
			
		// Sumamos el coste del rectángulo r[i] existente
        coste_acumulado += CalcularCosteRect(r[i].xi, r[i].yi, r[i].xf, r[i].yf);
		
		i++;
		
		}
	
	// Sumamos el coste del rectángulo entrante (o fusionado) al coste de la lista
    coste_acumulado += coste_nuevo;
	
	// Comprobación contra el 75% del coste del frame
	if (coste_acumulado >= COSTE_FRAME_COMPLETO_THRESHOLD)
		{
		jsiConsolePrintf("RIC debug : Pasamos a Update Full Frame -> invalidate rectangles too time\n");
			
		// ya no hacemos invalidate rectangles, pasamos a update frame full
        r[0] = {0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1};
        precalculatedscene.ir_front = 1;
        return;
		}
	
    // Evita invadir la zona reservada del consumidor
    int max_allowed = MAXINVALIDATERECTS - precalculatedscene.ir_back_count;
    if (n < max_allowed)
		{
        r[n] = {xi, yi, xf, yf};
        precalculatedscene.ir_front = n + 1;
		}
	else
		{
		jsiConsolePrintf("RIC debug : Pasamos a Update Full Frame -> invalidate rectanbgles too much\n");
		
		// si no nos caben, hay demasiados, hacemos update frame full y listos
		r[0] = {0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1};
        precalculatedscene.ir_front = 1;
		}
	
}

//% expose
void WRAPPER_AddInvRect(int x,int y,int w,int h)
{
	AddInvRect(x, y, x + w - 1, y + h - 1);
}

// RIC idea sea compatible CRC Game Engine
void SPRITE_CalculateInvalidateRectangles2(int xi,int yi,int ancho,int alto,int xai,int yai,int anchoa,int altoa)
{
	int xf=xi+ancho-1,			yf=yi+alto-1;
    int xaf=xai+anchoa-1,		yaf=yai+altoa-1;
	
    if (xi > xaf || xf < xai || yi > yaf || yf < yai)
        {
        AddInvRect(xai,yai,xaf,yaf);
        AddInvRect(xi,yi,xf,yf);
        }
    else
        {
        AddInvRect(xi < xai ? xi : xai, yi < yai ? yi : yai,xf > xaf ? xf : xaf, yf > yaf ? yf : yaf);
        }
}

// RIC idea sea compatible CRC Game Engine
void SPRITE_CalculateInvalidateRectangles(int xi,int yi,int xai,int yai,int ancho,int alto,bool animated)
{
	if (xi == xai && yi == yai)
        {
        if (animated) AddInvRect(xi, yi, xi+ancho-1, yi+alto-1);
        return;
        }
    SPRITE_CalculateInvalidateRectangles2(xi,yi,ancho,alto,xai,yai,ancho,alto);
}

//% expose
void CommitInvalidateRectangles()
{
	// copia del buffer productor al buffer consumidor
	
	// Si hacemos un nuevo CommitInvalidateRectangles() pero el anterior aun NO se envio, estamos framedrop, forzamos full frame update
	if (precalculatedscene.ir_unsent)
		{
		#ifndef __EMSCRIPTEN__
			jsiConsolePrintf("RIC debug : Pasamos a Update Full Frame -> drop frames\n");
		#endif
		
		// Reseteamos el contador frontal para el siguiente frame de lógica
		precalculatedscene.ir_front = 0;
		
		precalculatedscene.ir_back_count = 0;
		
		return;
		}
		
	// lo activamos, todo va en tiempo
	precalculatedscene.ir_unsent = true;
	
	int count = precalculatedscene.ir_front;
	
    if (count == 0)
		{
		// Reseteamos el contador frontal para el siguiente frame de lógica
			// ya es 0
		
        precalculatedscene.ir_back_count = 0;
        return;
		}
		
	// "el unico rectangulo es la pantalla entera" = bgmove full
	if (count == 1)
		{
		const auto &r = precalculatedscene.invrects[0];
		if (r.xi <= 0 && r.yi <= 0 && r.xf >= TFT_SRC_WIDTH - 1 && r.yf >= TFT_SRC_HEIGHT - 1)
			{
			// Reseteamos el contador frontal para el siguiente frame de lógica
			precalculatedscene.ir_front = 0;
			
			precalculatedscene.ir_back_count = 0;
			return;
			}
		}
	
	// RIC DEBUG
	//jsiConsolePrintf("Hay %u invalidate rectangles\n",count);
	
    auto *r = precalculatedscene.invrects;
    int back_start = MAXINVALIDATERECTS - count;

    // Copia rápida en bloque utilizando memmove por seguridad de solapamiento
    memmove(&r[back_start], &r[0], count * sizeof(RECTANGULO));

    // Publicamos los nuevos índices para el hilo consumidor
    precalculatedscene.ir_back_start = back_start;
    precalculatedscene.ir_back_count = count;

    // Reseteamos el contador frontal para el siguiente frame de lógica
    precalculatedscene.ir_front = 0;
}

/*	DEPRECATED
void SCENE_SimplifyInvalidateRectangles()
{
}
*/

//% expose
void GMA_VMprepareParameters(TNumber _lastX, TNumber _lastY, TNumber _vy, TNumber listenMask, int inc, int mpp, int mcc, int controljs)
{
	/*	esta optimizacion que hice de acceder con los punteros me daba problemas ... mirarlo bien si hace falta
		g_xsca=(int)( ( (BoxedNumber *)_lastX )->num );
	*/
	
	g_xsca=toInt(_lastX);
	
	g_ysca=toInt(_lastY);
	
	g_fvy=toFloat(_vy);
	
	g_listenMask=(uint32_t)toInt(listenMask);
		// flags que dependen del SPRITE actual y que se derivan del mismo listenMask
		g_fts	= ( (g_listenMask & EVT_FTS) != 0 );			// si tenemos el touch solid activado (toque  tipo 1 o 2 se activa, aunque lo pise)
		g_fsi	= ( (g_listenMask & EVT_FSI) != 0 );			// si gestionamos la inclinacion del personaje cuando hay subidas y bajadas pronunciadas
		g_ptfm	= ( (g_listenMask & FLG_PTFM) != 0 );			// cuando hay plataformas
		
	g_sprinc=inc;												// atributo 'inc' actual del SPRITE (inclinacion)
	
	g_MAX_PIXELS_PELDANYO=mpp;
	g_MAX_CELDAS_CAIDA=mcc;
	
	g_controljs=controljs;
	
	// RIC DEBUG
	contadordbg++;
	if (g_niveldebug > 4) DMESG("xsca %u ysca %u vy %f listenMask %u",g_xsca,g_ysca,g_fvy,g_listenMask);
	
}

//% expose
void WRAPPER_SPRITE_CalculateInvalidateStatic(int x,int y,int ancho,int alto,int xant,int yant,int anchoold,int altoold)
{
	
}

//% expose
void WRAPPER_SPRITE_CalculateInvalidateAnimated(int x,int y,int ancho,int alto,int xant,int yant,int anchoold,int altoold)
{
	
	if ( ancho != anchoold || alto != altoold )
		{
		// hay cambio de tamaños entre el costume anterior y el actual
		SPRITE_CalculateInvalidateRectangles2(x,y,ancho,alto,xant,yant,anchoold,altoold);
		}
	else
		{
		SPRITE_CalculateInvalidateRectangles(x,y,xant,yant,ancho,alto,true);
		}
	
}

// GMA -> Game Move Analisys
// RIC idea sea compatible CRC Game Engine
#ifdef PXT_VM

	// RIC - OJO que para que las funciones las vea el compilador PXT no deben partirse en lineas...

//% expose
RefCollection* GMA_EstudioMovimiento_Obstaculos(TNumber _x,TNumber _y,int spw,int sph,int scx,int scy,int scw,int sch)
									  
#else
	
void GMA_EstudioMovimiento_Obstaculos(JsVar *context,JsVar *objspr,JsVar *objp,JsVarFloat *fxsc,
									  JsVarFloat *fysc,int spw,int sph,int scx,int scy,int scw,
									  int sch,int cellw,int cellh,bool controljs,int numsprite)	

#endif
{
	
		// RIC DEBUG para PXT VM
		#ifdef PXT_VM
			if (contadordbg <= 2)
				{
				g_niveldebug=1;
				//g_niveldebug=3;
				}
			else
				{
				g_niveldebug=1;
				//g_niveldebug=3;
				}
		#endif
	
	#ifdef PXT_VM
		#define define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS			if (0 == 1) { }
	#else
		#define define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS			if (debugconsole) { if (!initdebug) { initdebug=true; if (subiendo) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> UP xsc(%d -> %d) ysc(%d -> %d)\n",xsca,xscdebug,ysca,yscdebug); else if (bajando) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> DOWN xsc(%d -> %d) ysc(%d -> %d)\n",xsca,xscdebug,ysca,yscdebug); else if (xscdebug > xsca) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> HORZ DER xsc(%d -> %d) ysc(%d -> %d)\n",xsca,xscdebug,ysca,yscdebug); else if (xscdebug < xsca) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> HORZ IZQ xsc(%d -> %d) ysc(%d -> %d)\n",xsca,xscdebug,ysca,yscdebug); else if (!nosmovemos) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> STOP xsc(%d -> %d) ysc(%d -> %d)\n",xsca,xscdebug,ysca,yscdebug); } }
	#endif

		// en PXT VM hay que reajustar las variables a CRC Game Engine
		#ifdef PXT_VM
			
			float f_x=toFloat(_x);
			float f_y=toFloat(_y);
			
			float memoria_para_fxsc=f_x + (spw >> 1);
			float *fxsc=&memoria_para_fxsc;
			
			float memoria_para_fysc=f_y + (sph >> 1);
			float *fysc=&memoria_para_fysc;
			
			int cellw=g_cellw;
			int cellh=g_cellh;
			
			bool controljs=g_controljs;
			
			// en PXT VM necesitamos saber si hemos de devolver los valores modificados o quedan igual
			bool update=false;
			int fsiinc=-1;										// -1: el 'inc' del SPRITE NO cambia
			cbkflags=0;
			
		#endif
	
	// recojemos las posiciones del centro sin decimales (que es como trabajamos aqui en los calculos)
	int xsc=(int)(*fxsc);
	int ysc=(int)(*fysc);
	
	// si el SPRITE esta fuera de la escena ??? NO hacemos nada
	bool finalrapido=false;
	if (xsc >= scw) finalrapido=true;
	if ( (xsc + spw) < 0 ) finalrapido=true;
	if (ysc >= sch) finalrapido=true;
	if ( (ysc + sph) < 0 ) finalrapido=true;
	if (finalrapido == true)
		{
		// RIC DEBUG
		//jsiConsolePrintf("RIC debug : SPRITE out of scene DO NOTHING\n");
		
		#ifdef PXT_VM
			return NEW_GC(RefCollection);
		#else
			return;
		#endif
		}
	
	// calculo para movernos por el Map Path
	int cellperline=(scw / cellw);
	
	// nuevas posiciones (respecto a la escena , NO a la TFT) y normalizadas por si se salen !!!
		int xoi=xsc - (spw >> 1);								// valores originales recordamos
		int yoi=ysc - (sph >> 1);								// valores originales recordamos
	int xi=xoi;				if (xi < 0) { xi=0; }
	int xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
	int yi=yoi;				if (yi < 0) { yi=0; }
	int yf=yoi + sph - 1;	if (yf >= sch) { yf=sch - 1; }
	
	// variables para gestionar los pixels de Map Paths del SPRITE
	int valorini,valormed,valorfin;
	int indiceini,indicemed,indicefin;
	
	
	/*	Recogemos informacion para hacer un primer analisis de movimiento ...
	*/
	
	// valores guardados en la escena y sprite ...
	#ifdef PXT_VM
		int xsca=g_xsca + (spw >> 1);
		int ysca=g_ysca + (sph >> 1);
		float fvy=g_fvy;
	#else
		int xsca=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"xsca",0));
		int ysca=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"ysca",0));
		JsVarFloat fvy=jsvGetFloatAndUnLock(jsvObjectGetChild(objspr,"vy",0));
	#endif
	
	// variables de estudio movimiento calculadas
	bool subiendo=false; if (ysca > (ysc+1)) subiendo=true; if (fvy < 0) subiendo=true;
	bool bajando=false; if (ysc > (ysca+1)) bajando=true; if (fvy > 0) bajando=true;
	bool movhorz=false; if (xsc != xsca) movhorz=true; 
	bool embarrado=false;
	bool nosmovemos=false; if ( (xsc != xsca) || (ysc != ysca) ) nosmovemos=true;
	bool activocbksolido=false;
		#ifdef PXT_VM
			if (g_fts) activocbksolido=true;
		#else
			JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);				// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
			if (jsvIsFunction(tmpvar)) activocbksolido=true;
			jsvUnLock(tmpvar);
		#endif
	int tipomovhorz=0;						// 0: no sabemos, 1: plano , 2: subidita , 3: bajadita
	
	
	/*	solo para DEBUG
	*/
	#ifdef PXT_VM
	
		int niveldebug=g_niveldebug;
		
	#else
		
		#ifdef __EMSCRIPTEN__
			precalculatedscene.dbglev=(uint8_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(context,"debug",0));
		#endif
		int niveldebug=precalculatedscene.dbglev;
		#ifdef __EMSCRIPTEN__
			// si hemos de hacer DEBUG solo de un SPRITE en concreto por su 'name'
			JsVar *tmpvar1=jsvObjectGetChild(context,"debugsprname",0);
			if (tmpvar1)
				{
				JsVar *tmpvar2=jsvObjectGetChild(objspr,"name",0);
				
				// OJO esto no existe en Espruino -> jsvIsEqualDebugSprite
				if ( jsvIsEqualDebugSprite(tmpvar1,tmpvar2) )
					{
												// NO hacemos nada SI hay debug
					}
				else
					{
					niveldebug=0;				// anulamos debug
					}
				
				jsvUnLock(tmpvar2);
				}
			jsvUnLock(tmpvar1);
		#endif
		
	#endif
	bool initdebug=false;
	bool debugconsole=false;
	bool debugconditions=false;
	if (niveldebug == 2)
		{
		debugconsole=controljs;
		}
	else if (niveldebug == 3)
		{
		debugconsole=true;
		}
	else if (niveldebug == 4)
		{
		debugconsole=true;
		debugconditions=controljs;
		}
	else if (niveldebug == 5)
		{
		debugconsole=true;
		debugconditions=true;
		}
	//if (!nosmovemos) debugconditions=false;					// si no nos movemos no mostramos tanto DEBUG por pantalla repetido
	if (debugconditions) debugconsole=true;
	int xscdebug=xsc; int yscdebug=ysc;	
	#ifdef PXT_VM
		if (debugconditions) DMESG("_x %f _y %f fxsc %f fysc %f spw %u sph %u scx %u scy %u scw %u sch %u",f_x,f_y,*fxsc,*fysc,spw,sph,scx,scy,scw,sch);
	#endif
	
	
	/*	primero es un caso especial que el SPRITE este EMBARRADO en solido... lo interpretamos como que
	    debemos subirlo arriba hasta tocar solido... es una manera facil de hacer nacer SPRITES de enemigos
		en cualquier posicion horizontal
		
				Esquema:		
									  +------------------+                                                   
									  |                  |                                                   
									  |                  |                                                   
									  |                  |                                                   
									  |                  |                                                   
						┌────────────────────────────────────────────┐                                       
						│             |xxxxxxxxxxxxxxxxxx|           │                                       
						│             +------------------+           │                                       
						│             ^        ^        ^            │                                       
						└────────────────────────────────────────────┘                                       
									  |        |        |                                                    
   								  	  |        |        |     
   								  	  |        |        |     
   								  	  |        |        |     
   								  indiceini    |     indicefin
   								  	  	       |              
   								  	  	  indicemed
	*/
	
	// hacemos los calculos de todo lo que hay en la posicion de los pies del SPRITE ...
	indiceini=( (yf / cellh) * cellperline ) + (xi / cellw);	valorini=GMA_MapaPathsFromIndex(indiceini);
	indicefin=( (yf / cellh) * cellperline ) + (xf / cellw);	valorfin=GMA_MapaPathsFromIndex(indicefin);
	indicemed=indiceini + ( (indicefin -  indiceini) / 2 );		valormed=GMA_MapaPathsFromIndex(indicemed);
	
	if ( (controljs == 0) && (bajando == false) &&										// estamos embarrados ? (si NO hay control movimiento JS , o estamos bajando ...)
		 (valorini == 1) && (valormed == 1) && (valorfin == 1) )						
		{
		
		// DEBUG
		define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
		if (debugconsole) jsiConsolePrintf("RIC debug :                              -> SPRITE esta embarrado\n");
			
		// lo subimos a la superficie solida mas cercana ...
		embarrado=true;
		bajando=true;				// asi garantizamos que entraremos en la rama condicional que lo hace ...
		}
	else																				// esta rama solo es para DEBUG
		{
		
		// DEBUG
		if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : NO embarrado\n"); }
		
		}

	
	/* vemos si estamos BAJANDO, SUBIENDO u HORIZONTAL ...
	*/
	
	if (bajando)																		// estamos BAJANDO
		{
			
		// DEBUG
		if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : bajando\n"); }
		
		/*	primero miramos el suelo si podemos bajar mas, si no nos posicionamos en el solido inferior ... 
			luego si tenemos que retroceder horizontalmente porque topamos con algun tipo de pared
			
					Esquema:
									bajando                                                  
																							 
									   |                                                     
									   |                                                     
									   |                                                     
									   |                                                     
									   |     +----------------+                              
									   |     |                |<------- valor ini            
									   |     |                |                              
									   |     |                |                              
									   v     |                |                              
											 |                |                              
											 |                |<------- valor med            
											 |                |                              
											 |                |                              
											 |            +---|-----------------------------+
											 |            |xxx|<-------- valor peldanyo     |
											 |            |xxx|                             |
											 |            |xxx|                             |
											 +----------------+                             |
														  +---------------------------------+
		*/
		
		/*	miramos si tocamos suelo, nos posicionamos en el solido ...
				Esquema:		
								+------------------+    
								|                  |    
								|                  |    
								|                  |    
								|                  |    
								|                  |    
								|xxxxxxxxxxxxxxxxxx|    
								+------------------+    
								^        ^        ^     
								|        |        |     
								|        |        |     
								|        |        |     
							indiceini    |     indicefin
										 |              
									 indicemed		
		*/
		
		// si estamos embarrados NO hay que calcular nada ...
		if (!embarrado)
			{
			
			// hacemos los calculos (miramos todos las coordenadas que ha bajado el SPRITE) ...
			// 'yfant' la comprobacion de donde estabamos antes , y 'yf' la comprobacion de donde estaremos ahora
			int yfant=ysca - (sph >> 1) + sph - 1;	if (yfant >= sch) { yfant=sch - 1; }
			indiceini=( (yfant / cellh) * cellperline ) + (xi / cellw);
			indicefin=( (yfant / cellh) * cellperline ) + (xf / cellw);
			indicemed=indiceini + ( (indicefin -  indiceini) / 2 );
				// DEBUG
				//if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             yfant = %d yf = %d\n",yfant,yf);
				//if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             xi = %d xf = %d\n",xi,xf);
				//if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             indices (3) (%d , %d , %d)\n",indiceini,indicemed,indicefin);
			valorini=GMA_MapaPathsFromIndex(indiceini); valormed=GMA_MapaPathsFromIndex(indicemed); valorfin=GMA_MapaPathsFromIndex(indicefin);
			
			// segun el comportamiento del atravesable ?
			#ifdef PXT_VM
				if (g_comportamientoatravesable == 1)
			#else
				if (precalculatedscene.comportamientoatravesable == 1)
			#endif
				{
				// si es 0 o 2 (atravesable) la bajada es valida ...
			
				while ( (valorini == 0 || valorini == 2) && (valormed == 0 || valormed == 2) && (valorfin == 0 || valorfin == 2) && (yfant <= yf) )
					{
					
					// una fila mas abajo y calculamos nuevos valores
					indiceini+=cellperline;		valorini=GMA_MapaPathsFromIndex(indiceini);
					indicemed+=cellperline;		valormed=GMA_MapaPathsFromIndex(indicemed);
					indicefin+=cellperline;		valorfin=GMA_MapaPathsFromIndex(indicefin);
					
					yfant+=2;						// a cada celda son 2 pixels de alto de linea
					}
				
				}
			else																// por defecto
				{
				
				while ( !(valorini || valormed || valorfin) && (yfant <= yf) )
					{
					
					// una fila mas abajo
					indiceini+=cellperline; indicemed+=cellperline; indicefin+=cellperline;
					
					// nuevos valores
					valorini=GMA_MapaPathsFromIndex(indiceini); valormed=GMA_MapaPathsFromIndex(indicemed); valorfin=GMA_MapaPathsFromIndex(indicefin);
					
					yfant+=2;						// a cada celda son 2 pixels de alto de linea
					}
				
				}
			
			}
		
		// aqui  los valores de los indices ( valorini ... ) son o de justo los pies del SPRITE o debajo de el ...
		
		if ( (valorini != 0) || (valormed != 0) || (valorfin != 0) )					// hemos topado por abajo ?
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : topamos por abajo\n"); }
			
			bool calcularsubirasolido=true;
			
			if ( (valorini == 3) || (valormed == 3) || (valorfin == 3) )				// es Special Zone ?
				{
				// podemos seguir bajando, pero alertamos al CALLBACK
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     es Special Zone\n"); }
				
				
				/*	miramos los casos de callbacks ...
				*/
				
				// DEBUG
				define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
				if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Callback (Sprites Special Zone) (1)\n");
				
				// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
				#ifdef PXT_VM
				
					// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
					if (g_listenMask & EVT_FSZ)
						{
						cbkflags |= EVT_FSZ;	update=true;
						}
				
				#else
				
					JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
					if (jsvIsFunction(tmpvar))
						{
						// si que tiene la funcion definida de callback
						
						// creamos el objeto de callback:
						JsVar *tmpobj=jsvNewObject();
						jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
						jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
						jsvObjectSetChild(tmpobj,"args",objspr);
						
						// lo añadimos a la lista
						tmpvar=jsvObjectGetChild(context,"cbk",0);
						jsvArrayPush(tmpvar,tmpobj);
						
						jsvUnLock(tmpobj);
						}
					jsvUnLock(tmpvar);
					
				#endif
				
				// he puesto aqui porque si no en las escaleras el calcular subir a solido la liaba desde que lo rehice !!! OJO efectos colaterales
				// parece que entonces al saltar y caer sobre solido, NO nos paramos !!!
				calcularsubirasolido=false;
				
				}
			else if ( valorini == 1 || valormed == 1 || valorfin == 1 )					// si alguna parte de la que topamos es solido ?
				{
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     es solido\n"); }
				
				// si estamos en control de movimientos JS (tipo un salto) miramos bien porque igual estamos bajando en diagonal !!
				if (controljs == 1)
					{
					
					if (xsc > xsca)														// vamos a la DER ?
						{
							
						// PENDIENTE aqui en vez de pared podria ser que justo nos subieramos a un podio !!!
						
						// miramos si estamos bajando hacia la derecha y hay pared, pues solo bajamos, volvemos a la x anterior
						if ( valorini != 1  || valormed != 1 || valorfin == 1 )
							{
							
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             miramos si topamos con pared al bajar hacia la DER\n");
							
							// LIMITACION !!! NO subir mas de las celdas del alto de un peldanyo
							//int limitecellssubir=( (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mpp",0)) ) /  cellh;
							int limitecellssubir=4;
							
							int indice=indicefin; int alturacell=0; 
							while( (indice >= 0) && (GMA_MapaPathsFromIndex(indice) != 0) && (alturacell < limitecellssubir) ) { indice-=cellperline; /* una fila anterior */ alturacell++; }
							
							// Hay pared, asi que por un lado volvemos a la X anterior (mas rapido que encontrar cuantos pixels retroceder)
							// y recalcularemos los indices de esta nueva pos, para ver si ademas subimos a solido
							if (alturacell >= limitecellssubir) calcularsubirasolido=false;
							
							}
						
						}
					else if (xsc < xsca)												// vamos a la IZQ?
						{
							
						// PENDIENTE aqui en vez de pared podria ser que justo nos subieramos a un podio !!!
						
						// estamos bajando y hacia la izquierda y hay pared, pues solo bajamos, volvemos a la x anterior
						if ( valorini == 1  || valormed != 1 || valorfin != 1 )
							{
							
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             miramos si topamos con pared al bajar hacia la IZQ\n");
							
							// LIMITACION !!! NO subir mas de las celdas del alto de un peldanyo
							//int limitecellssubir=( (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mpp",0)) ) /  cellh;
							int limitecellssubir=4;
							
							int indice=indiceini; int alturacell=0; 
							while( (indice >= 0) && (GMA_MapaPathsFromIndex(indice) != 0) && (alturacell < limitecellssubir) ) { indice-=cellperline; /* una fila anterior */ alturacell++; }
							
							// Hay pared, asi que por un lado volvemos a la X anterior (mas rapido que encontrar cuantos pixels retroceder)
							// y recalcularemos los indices de esta nueva pos, para ver si ademas subimos a solido
							if (alturacell >= limitecellssubir) calcularsubirasolido=false;
							
							}
						}
						
					// retrocedemos a la X anterior (ya no chocamos) pero queda ver si al bajar activamos el calcularsubirasolido
					if (calcularsubirasolido == false)
						{
						
						/*	Primero volvemos a la X anterior
						*/
						
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             hemos topado con pared al bajar\n");
						
						*fxsc=xsca;
						#ifdef PXT_VM
							update=true;
						#else
							jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
						#endif
						
						// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
						int desfasecellsX=(xsca - xsc) / cellw;
						xsc=xsca;
						xoi=xsc - (spw >> 1);
						xi=xoi;				if (xi < 0) { xi=0; }
						xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
						
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
						
						
						/*	Ahora recalculamos las indices ya que ha cambiado la posicion
						*/

						indiceini+=desfasecellsX;
						indicemed+=desfasecellsX;
						indicefin+=desfasecellsX;
						
						
						/*	Volvemos a mirar si hay que subir ha solido
						*/
						
						if ( GMA_MapaPathsFromIndex(indiceini) == 1 || GMA_MapaPathsFromIndex(indicemed) == 1 || GMA_MapaPathsFromIndex(indicefin) == 1 )
							{
							calcularsubirasolido=true;
							}
						
						}
					
					}
				
				}
			else																		// topamos con atravesable
				{
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     es atravesable\n"); }
				
				// segun el comportamiento de zona ANCHA ...
				#ifdef PXT_VM
					if (g_comportamientoatravesable == 1)
				#else
					if (precalculatedscene.comportamientoatravesable == 1)
				#endif
					{
					
					calcularsubirasolido=false;
					
					// dejamos bajar siempre y cuando todo por abajo los pies sea atravesable ...
					if ( valorini == 2 && valormed == 2 && valorfin == 2 )					
						{						
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : podemos seguir bajando\n"); }
						
						}
					else
						{
						
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHO no podemos salirnos por abajo\n"); }
						
						
						// NUEVA fysc
						*fysc=ysca;
						#ifdef PXT_VM
							update=true;
						#else
							jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromInteger(ysca));			// ysca es INT
						#endif
						
						// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
						ysc=ysca;
						yoi=ysc - (sph >> 1);
						yi=yoi;				if (yi < 0) { yi=0; }
						yf=yoi + sph - 1;	if (yf >= sch) { yf=sch - 1; }
						
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la ysc ( %d )\n",ysc);
						
						// y si ademas de bajar nos desplazavamos horizontalmente tambien la restablecemos la X
						if (movhorz)
							{
							
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ademas bajamos en DIAGONAL !\n"); }
							
							// NUEVA fxsc
							*fxsc=xsca;
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=xsca;
							xoi=xsc - (spw >> 1);
							xi=xoi;				if (xi < 0) { xi=0; }
							xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
							
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
							
							}
						
						}
					
					}
				
				}
					
			if (calcularsubirasolido)
				{
				
				/*	nos colocamos en la altura que justo NO topamos ( podemos estar planos, pendiente, o pilon en medio ....)
						

																	 
																						SPRITE   
												  +---------------------+                 |      
												  |                     |<----------------+      
												  |                     |                        
												  |                     |           
												  |                     |           
											  ^   |                     |           
									----------+---|--+                  |           
											  |   |xx|                  |           
											  |   |xx|                  | ^         
											  |   |xx|               +--|-+-------- 
											  |   |xx|               |xx| |         
											  |   |xx|      ^        |xx| |         
											  |   +---------+-----------+ |         
									  alturacellini  |alturacellmedio| alturacellfin
													 |    es 0       |              
													 |               |              
													 |               |              
													 +---------------+

				*/
				
				// segun el comportamiento de atravesable y special ... calculamos hasta donde subir ...
				//int limitecellssubir=( (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mpp",0)) ) /  cellh;
				int limitecellssubir=4;
				int alturacellini=0;
				int alturacellmedio=0;
				int alturacellfin=0;
				#ifdef PXT_VM
					if ( g_comportamientoatravesable == 1 || g_comportamientospecial == 1 )
				#else
					if ( precalculatedscene.comportamientoatravesable == 1 || precalculatedscene.comportamientospecial == 1 )
				#endif
					{
					// subimos  hasta el solido, el atravesable y el special NO se cuenta como solido
					while( (indiceini >= 0) && (GMA_MapaPathsFromIndex(indiceini) == 1) && (alturacellini < limitecellssubir) )		{ indiceini-=cellperline; /* una fila anterior */ alturacellini++; }
					while( (indicemed >= 0) && (GMA_MapaPathsFromIndex(indicemed) == 1) && (alturacellmedio < limitecellssubir) )	{ indicemed-=cellperline; /* una fila anterior */ alturacellmedio++; }
					while( (indicefin >= 0) && (GMA_MapaPathsFromIndex(indicefin) == 1) && (alturacellfin < limitecellssubir) )		{ indicefin-=cellperline; /* una fila anterior */ alturacellfin++; }
					}
				else		// por defecto subimos a solido o atravesable o special
					{					
					while( (indiceini >= 0) && (GMA_MapaPathsFromIndex(indiceini) != 0) && (alturacellini < limitecellssubir) )		{ indiceini-=cellperline; /* una fila anterior */ alturacellini++; }
					while( (indicemed >= 0) && (GMA_MapaPathsFromIndex(indicemed) != 0) && (alturacellmedio < limitecellssubir) )	{ indicemed-=cellperline; /* una fila anterior */ alturacellmedio++; }
					while( (indicefin >= 0) && (GMA_MapaPathsFromIndex(indicefin) != 0) && (alturacellfin < limitecellssubir) )		{ indicefin-=cellperline; /* una fila anterior */ alturacellfin++; }					
					}
				
				// de esta manera indicamos que mas delante ya restablecemos la coord Y, NO dejamos bajar
				//jsiConsolePrintf("RIC debug : Values (%d , %d , %d , %d)\n",limitecellssubir,alturacellini,alturacellmedio,alturacellfin);
				if ( !( alturacellini < limitecellssubir && alturacellmedio < limitecellssubir && alturacellfin < limitecellssubir ) ) indiceini=-1;
				
				// si no hemos podido subir a solido (ERROR)
				if ( (indiceini < 0) || (indicemed < 0) || (indicefin < 0) )			// anulamos este movimiento hacia abajo y dejamos la posicion anterior ...
					{
					
					// NUEVA fysc
					// asi que no dejamos avanzar a la coord Y , la dejamos como estaba...
					*fysc=ysca;
					#ifdef PXT_VM
						update=true;
					#else
						jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromInteger(ysca));			// ysca es INT
					#endif
					
					// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
					ysc=ysca;
					yoi=ysc - (sph >> 1);
					yi=yoi;				if (yi < 0) { yi=0; }
					yf=yoi + sph - 1;	if (yf >= sch) { yf=sch - 1; }
					
					// DEBUG
					define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 ERROR SPRITE no se puede posicionar ...\n");
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 indices (3) (%d , %d , %d)\n",indiceini,indicemed,indicefin);
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 restablecemos la ysc ( %d )\n",ysc);
					
					}
				else																	// vemos sobre que nos subimos (por si estamos encima de pilon)
					{
					
					// ahora tenemos tres alturas iguales o diferentes, y hemos de ver donde nos posicionamos, lo guardaremos en indiceok
					int indiceok;
					if (alturacellini > alturacellfin)											// estamos en la bajada de una pendiente
						{
						
						if (alturacellmedio > alturacellini)									// hay un pilon en medio, nos colocamos encima de el
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Topamos con un pilon, subimos encima\n");
							
							indiceok=indicemed;
							}
						else																	// nos colocamos en la parte mas alta de la bajada
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Topamos con una bajada, subimos a solido\n");
							
							indiceok=indiceini;
							}
						
						}
					else																		// estamos en una subida de una pendiente , o planos
						{
							
						if (alturacellmedio > alturacellfin)									// hay un pilon en medio, nos colocamos encima de el
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Topamos con un pilon, subimos encima\n");
							
							indiceok=indicemed;
							}
						else																	// nos colocamos en la parte mas alta
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Topamos con una subida o plano, subimos a solido\n");
								
							indiceok=indicefin;
							}
						
						}
					
					// calculamos que Y final es y modificamos la nueva Y del Sprite
					int nuevayfinal=( (indiceok / cellperline) * cellh ) + 1;
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     yf = %d , nuevayfinal = %d\n",yf,nuevayfinal);
					yf=yoi + sph - 1;															// hemos de usar la yf real (no crop de outscreen)
					int diferencia=yf -	nuevayfinal;
						// NUEVA fysc
					*fysc=ysc - diferencia;														// actualizamos para los siguientes calculos de las funciones de arriba					
					#ifdef PXT_VM
						update=true;
					#else
						jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
					#endif
					
					// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
					ysc=(int)(*fysc);
					yoi=ysc - (sph >> 1);
					yi=yoi;
					yf=yoi + sph - 1;
					if (yi < 0) { yi=0; }
					if (yf >= sch) { yf=sch - 1; }
					
					if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              ->     ysc ( subimos de %d a %d )\n",yscdebug,ysc);
					
					
					/*	miramos los casos de callbacks ...
					*/
					
					// (Sprites Fall Stop) bajando hemos tocado solido, debemos activar el callback, en caso que vengamos de embarrado NO lo tenemos en cuenta
					if (!embarrado)
						{
							
						if (!activocbksolido)
							{
								
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Callback (Sprites Fall Stop) (1)\n");
							
							// vemos si el sprite tiene CALLBACK de este evento ... ffs -> Funcion Fall Stops
							#ifdef PXT_VM
								
								// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
								if (g_listenMask & EVT_FFS)
									{
									cbkflags |= EVT_FFS;	update=true;
									}
								
							#else
								
								JsVar *tmpvar=jsvObjectGetChild(objspr,"ffs",0);
								if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffs",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
								if (jsvIsFunction(tmpvar))
									{
									// si que tiene la funcion definida de callback
									
									// creamos el objeto de callback:
									JsVar *tmpobj=jsvNewObject();
									jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFS));
									jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
									jsvObjectSetChild(tmpobj,"args",objspr);
									
									// lo añadimos a la lista
									tmpvar=jsvObjectGetChild(context,"cbk",0);
									jsvArrayPush(tmpvar,tmpobj);
									
									jsvUnLock(tmpobj);
									}
								jsvUnLock(tmpvar);
							
							#endif
							
							}
						else
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
							
							// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
							#ifdef PXT_VM
								
								// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
								cbkflags |= EVT_FTS;	update=true;
								
							#else
								
								JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
								if (jsvIsFunction(tmpvar))
									{
									// si que tiene la funcion definida de callback
								
									// creamos el objeto de callback:
									JsVar *tmpobj=jsvNewObject();
									jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
									jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
									jsvObjectSetChild(tmpobj,"args",objspr);
								
									// lo añadimos a la lista
									tmpvar=jsvObjectGetChild(context,"cbk",0);
									jsvArrayPush(tmpvar,tmpobj);
								
									jsvUnLock(tmpobj);
									}
								jsvUnLock(tmpvar);
								
							#endif
							
							}
						
						}
					
					}
				
				}
			
			}
		#ifdef PXT_VM
			else if (g_comportamientoatravesable == 1)									// en comportamiento de zona ANCHA, no podemos estar volando !!
		#else
			else if (precalculatedscene.comportamientoatravesable == 1)					// en comportamiento de zona ANCHA, no podemos estar volando !!
		#endif
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHO no podemos salirnos por abajo!\n"); }
			
			// NUEVA fysc
			*fysc=ysca;
			#ifdef PXT_VM
				update=true;
			#else
				jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromInteger(ysca));			// ysca es INT
			#endif
			
			// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
			ysc=ysca;
			yoi=ysc - (sph >> 1);
			yi=yoi;				if (yi < 0) { yi=0; }
			yf=yoi + sph - 1;	if (yf >= sch) { yf=sch - 1; }
			
			if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la ysc ( %d )\n",ysc);
			
			}
		else																			// esta rama solo es para DEBUG
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : podemos seguir bajando NO hemos tocado suelo\n"); }
			
			}
		
		
		/*	si por la DER o IZQ topamos, retrocedemos, pero solo si ha habido desp. de la X
						Esquema:
												  xf                     
																		 
							 dir. move sprite      |                     
						   -------------------->   |                     
												   |                     
												   v                     
										+-----------+<------ indiceini   
										|x         x|                    
										|x         x|                    
										|x         x|                    
										|x         x|                    
										|x         x|                    
										|x         x|<-------indicemed
										|x         x|                    
										|x         x|                    
										|x         x|                    
										|x         x|<-------indicepelda 
										|x         x|                    
										+-----------+<-------indicefin   
										 ^                               
										 |    dir. move sprite           
										 | <-------------------          
										xi
		*/
	
		if (movhorz)
			{
		
			/*	en el caso que no hubieramos topado con suelo, debe estar pasando que estamos bajando (cayendo) y aun estamos lejos del suelo ...
				en esta rama de comprobacion tanto DER o IZQ no contemplamos el indifin ya que se hubiera detectado antes al detectar el solido por debajo
			*/
			
			int xc;
			if (xsc > xsca)																// vamos a la DER ?
				{
				
				// primero derecha
				xc=xf;
				
				//calculamos los indices de las cells del lateral derecha del SPRITE y si en estas celdas topamos con algo solido
				indiceini=( (yi / cellh) * cellperline ) + (xc / cellw);							valorini=GMA_MapaPathsFromIndex(indiceini);
				indicemed=( ( ( yi + ( (yf-yi) / 2 ) ) / cellh ) * cellperline ) + (xc / cellw);	valormed=GMA_MapaPathsFromIndex(indicemed);
				
				// hemos topado por la DER ?
				if ( (valorini != 0) || (valormed != 0) )								// topamos con algo
					{
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : topamos DER (1)\n"); }
					
					if	( ( valorini == 2 && valormed != 1 ) || ( valormed == 2 && valorini != 1 ) )	// si era solido atravesable NO es topar ...
						{
						// no hacemos nada ...
						
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Bajando topamos DER con atravesable\n");
						
						}
					else if ( valorini == 3 && valormed == 3 )							// si era SPECIAL ZONE -> default atravesable , NO es topar ...
						{
						// no hacemos nada ...
						
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Bajando topamos DER con SPECIAL ZONE es atravesable\n");
						
						}
					else if ( valorini == 0 && valormed != 3 && valorfin != 0 ) 		// si la parte de arriba aun no toca solido, mas posible estemos llegando a una plataforma solida, nos subimos (el SPECIAL ZONE se considera 0 en esta caso)
						{
							
						// segun comportamientoatravesable
						#ifdef PXT_VM
							if (g_comportamientoatravesable == 1)
						#else
							if (precalculatedscene.comportamientoatravesable == 1)
						#endif
							{
							// en comportamientoatravesable de zona ANCHA este caso no existe !!
							}
						else
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              -> bajando y por la DER topamos, nos subimos a un solido\n");
							
							// vamos subiendo hasta que celda ya no tenemos solido (usamos medio que es la unica que esta tocando)
							do	{
								indicemed-=cellperline;			// una fila mas por encima

								} while ( GMA_MapaPathsFromIndex(indicemed) != 0 );
							
							// calculamos que Y final es y modificamos la nueva Y del Sprite
							int nuevayfinal=( (indicemed / cellperline) * cellh ) + 1;
							yf=yoi + sph - 1;															// hemos de usar la yf real (no crop de outscreen)
							int diferencia=yf -	nuevayfinal;
								// NUEVA fysc
							*fysc=ysc - diferencia;														// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
							#endif
							
							// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
							ysc=(int)(*fysc);
							yoi=ysc - (sph >> 1);
							yi=yoi;
							yf=yoi + sph - 1;
							if (yi < 0) { yi=0; }
							if (yf >= sch) { yf=sch - 1; }
							
							if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              -> ysc ( %d )\n",ysc);
							
							
							/*	miramos los casos de callbacks ...
							*/
							
							if (!activocbksolido)
								{
								
								// como estamos bajando y hemos topado por la DER y hemos conseguido subir a un solido ... activamos callback siempre
								// (Sprites Fall Stop)
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Fall Stop)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... ffs -> Funcion Fall Stops
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FFS)
										{
										cbkflags |= EVT_FFS;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objspr,"ffs",0);
									if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffs",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
								#ifdef PXT_VM
									
									// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
									cbkflags |= EVT_FTS;	update=true;
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						
						}
					else																// SI que topamos con algo
						{
						
						// segun comportamientoatravesable						
						#ifdef PXT_VM
							if (g_comportamientoatravesable == 1)
						#else
							if (precalculatedscene.comportamientoatravesable == 1)
						#endif
							{
							// en comportamiento de zona ANCHA no hay el retrocder hacia atras, es restablecer la X anterior ...
							
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHA no salimos por la DER\n"); }
							
							// NUEVA fxsc
							*fxsc=xsca;
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=xsca;
							xoi=xsc - (spw >> 1);
							xi=xoi;				if (xi < 0) { xi=0; }
							xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
							
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
							
							}
						else
							{
								
							// vemos que indice usamos para retroceder
							int indiceok; int offset;
							if (valorini != 0) { indiceok=indiceini; offset=0; }
							else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }
							
							// calculamos el indice 0 de esta fila (es lo maximo que podemos retroceder)
							int indiceminimo=( ( ( yi + offset ) / cellh ) * cellperline ) + 0;
							
							// nos colocamos al maximo pero que justo NO topamos
							do	{
								indiceok--;				// una X anterior
								} while( (GMA_MapaPathsFromIndex(indiceok) != 0) && (indiceok > indiceminimo) );
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Bajando topamos DER y retrocedemos a IZQ\n");
							
							// DEBUG
							if (indiceok == indiceminimo) { jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING topamos DER y hemos retrocedido hasta la X=0\n"); }
											
							// calculamos que X final es
							int nuevaxfinal=(indiceok % cellperline) * cellw;
							
							// calculamos la diferencia y modificamos la nueva X del Sprite
							nuevaxfinal+=(cellw-1);													// las celdas son multiplo 2 siempre, y me coloco la xf justo al final del ultimo pixel de la celda
							xf=xoi + spw - 1;														// hemos de usar la xf real (no crop de outscreen)
							int diferencia=xf -	nuevaxfinal;
								// NUEVA fxsc
							*fxsc=xsc - diferencia;													// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromFloat(*fxsc));
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=(int)(*fxsc);
							xoi=xsc - (spw >> 1);
							xi=xoi;
							xf=xoi + spw - 1;
							if (xi < 0) { xi=0; }
							if (xf >= scw) { xf=scw - 1; }
							
							if ( (debugconsole) && (xscdebug != xsc) ) jsiConsolePrintf("RIC debug :                              ->             xsc ( %d )\n",xsc);
							
							
							/*	miramos los casos de callbacks ...
							*/
							
							if ( (valorini == 3) || (valormed == 3) )
								{
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Callback (Sprites Special Zone) (2)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FSZ)
										{
										cbkflags |= EVT_FSZ;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
									
								if (!activocbksolido)
									{
									
									// hemos retrocedido porque hemos chocado con algo ... vemos si el sprite tiene CALLBACK de este evento ... fws -> Function Wall Stops									
									#ifdef PXT_VM
										
										// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
										if (g_listenMask & EVT_FWS)
											{
											cbkflags |= EVT_FWS;	update=true;
											}
										
									#else
										
										JsVar *tmpvar=jsvObjectGetChild(objp,"fws",0);
										if (jsvIsFunction(tmpvar))
											{
											// si que tiene la funcion definida de callback
										
											// creamos el objeto de callback:
											JsVar *tmpobj=jsvNewObject();
											jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FWS));
											jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
											jsvObjectSetChild(tmpobj,"args",objspr);
										
											// lo añadimos a la lista
											tmpvar=jsvObjectGetChild(context,"cbk",0);
											jsvArrayPush(tmpvar,tmpobj);
										
											jsvUnLock(tmpobj);
											}
										jsvUnLock(tmpvar);
										
									#endif
									
									}
								else
									{
										
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
									
									// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
									#ifdef PXT_VM
									
										// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
										cbkflags |= EVT_FTS;	update=true;
										
									#else
										
										JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
										if (jsvIsFunction(tmpvar))
											{
											// si que tiene la funcion definida de callback
										
											// creamos el objeto de callback:
											JsVar *tmpobj=jsvNewObject();
											jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
											jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
											jsvObjectSetChild(tmpobj,"args",objspr);
										
											// lo añadimos a la lista
											tmpvar=jsvObjectGetChild(context,"cbk",0);
											jsvArrayPush(tmpvar,tmpobj);
										
											jsvUnLock(tmpobj);
											}
										jsvUnLock(tmpvar);
										
									#endif
									
									}
								
								}
							
							}
						
						}
					
					}
				#ifdef PXT_VM
					else if (g_comportamientoatravesable == 1)								// en comportamiento de zona ANCHA no podemos estar los pies en el aire
				#else
					else if (precalculatedscene.comportamientoatravesable == 1)				// en comportamiento de zona ANCHA no podemos estar los pies en el aire
				#endif
					{

					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHA no salimos por la DER\n"); }
					
					// NUEVA fxsc
					*fxsc=xsca;
					#ifdef PXT_VM
						update=true;
					#else
						jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
					#endif
					
					// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
					xsc=xsca;
					xoi=xsc - (spw >> 1);
					xi=xoi;				if (xi < 0) { xi=0; }
					xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
					
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
					
					}
				
				}
			else																		// vamos a la IZQ ?
				{
				
				// ahora izquierda
				xc=xi;
				
				//calculamos los indices de las cells del lateral izquierda del SPRITE y si en estas celdas topamos con algo solido
				indiceini=( (yi / cellh) * cellperline ) + (xc / cellw);							valorini=GMA_MapaPathsFromIndex(indiceini);
				indicemed=( ( ( yi + ( (yf-yi) / 2 ) ) / cellh ) * cellperline ) + (xc / cellw);	valormed=GMA_MapaPathsFromIndex(indicemed);
				
				// hemos topado por la IZQ ?
				if ( (valorini != 0) || (valormed != 0) )								// topamos con algo
					{
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : topamos IZQ (1)\n"); }
					
					if	( ( valorini == 2 && valormed != 1 ) || ( valormed == 2 && valorini != 1 ) )	// si era solido atravesable NO es topar ...
						{
						// no hacemos nada ...
					
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Bajando topamos IZQ con atravesable\n");
						
						}
					else if ( valorini == 3 && valormed == 3 )							// si era SPECIAL ZONE -> default atravesable , NO es topar ...
						{
						// no hacemos nada ...
					
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Bajando topamos IZQ con SPECIAL ZONE es atravesable\n");
					
						}
					else if ( valorini == 0 && valormed != 3 && valorfin != 0 )			// si la parte de arriba aun no toca solido, mas posible estemos llegando a una plataforma solida, nos subimos (el SPECIAL ZONE se considera 0 en esta caso)
						{
						
						// segun comportamientoatravesable
						#ifdef PXT_VM
							if (g_comportamientoatravesable == 1)
						#else
							if (precalculatedscene.comportamientoatravesable == 1)
						#endif
							{
							// en comportamientoatravesable de zona ANCHA este caso no existe !!
							}
						else
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              -> bajando hacia IZQ nos subimos a un solido\n");
							
							// vamos subiendo hasta que celda ya no tenemos solido (usamos medio que es la unica que esta tocando)
							do	{
								indicemed-=cellperline;			// una fila mas por encima

								} while( GMA_MapaPathsFromIndex(indicemed) != 0 );
							
							// calculamos que Y final es y modificamos la nueva Y del Sprite
							int nuevayfinal=( (indicemed / cellperline) * cellh ) + 1;
							yf=yoi + sph - 1;															// hemos de usar la yf real (no crop de outscreen)
							int diferencia=yf -	nuevayfinal;
							
							// NUEVA fysc
							*fysc=ysc - diferencia;														// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
							#endif
							
							// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
							ysc=(int)(*fysc);
							yoi=ysc - (sph >> 1);
							yi=yoi;
							yf=yoi + sph - 1;
							if (yi < 0) { yi=0; }
							if (yf >= sch) { yf=sch - 1; }
							
							if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              -> ysc ( %d )\n",ysc);
							
							
							/*	miramos los casos de callbacks ...
							*/
							
							if (!activocbksolido)
								{
													
								// como estamos bajando y hemos topado por la IZQ y hemos conseguido subir a un solido ... activamos callback siempre
								// (Sprites Fall Stop)
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Fall Stop)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... ffs -> Funcion Fall Stops
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FFS)
										{
										cbkflags |= EVT_FFS;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objspr,"ffs",0);
									if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffs",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
									
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
								
								#ifdef PXT_VM
									// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
									cbkflags |= EVT_FTS;	update=true;
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						
						}
					else																// SI que topamos con algo
						{
						
						// segun comportamientoatravesable
						#ifdef PXT_VM
							if (g_comportamientoatravesable == 1)
						#else
							if (precalculatedscene.comportamientoatravesable == 1)
						#endif
							{
							// en comportamiento de zona ANCHA no hay el retrocder hacia atras, es restablecer la X anterior ...
							
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHA no salimos por la IZQ\n"); }
							
							// NUEVA fxsc
							*fxsc=xsca;
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
							#endif
								
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=xsca;
							xoi=xsc - (spw >> 1);
							xi=xoi;				if (xi < 0) { xi=0; }
							xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
							
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
							
							}
						else
							{
							
							// vemos que indice usamos para retroceder hacia DER
							int indiceok; int offset;
							if (valorini != 0) { indiceok=indiceini; offset=0; }
							else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }
							
							// calculamos el indice MAX de esta fila (es lo maximo que podemos retroceder hacia DER)
							int indicemaximo=( ( ( ( yi + offset ) / cellh ) + 1 ) * cellperline ) + 0;
							indicemaximo-=1;
							
							// nos colocamos al maximo pero que justo NO topamos
							do	{
								indiceok++;				// una X posterior
								} while( (GMA_MapaPathsFromIndex(indiceok) != 0) && (indiceok < indicemaximo) );
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Topamos IZQ (1) y retrocedemos a DER\n");
							
							// DEBUG
							if (indiceok == indicemaximo) { jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING topamos DER y hemos retrocedido hasta la X=0\n"); } 
											
							// calculamos que X final es
							int nuevaxfinal=(indiceok % cellperline) * cellw;
							
							// calculamos la diferencia y modificamos la nueva X del Sprite
							xi=xoi;																// hemos de usar la xi real (no crop de outscreen)
							int diferencia=xi -	nuevaxfinal;							
								// NUEVA fxsc
							*fxsc=xsc - diferencia;												// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromFloat(*fxsc));
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=(int)(*fxsc);
							xoi=xsc - (spw >> 1);
							xi=xoi;
							xf=xoi + spw - 1;
							if (xi < 0) { xi=0; }
							if (xf >= scw) { xf=scw - 1; }
							
							// DEBUG
							if ( (debugconsole) && (xscdebug != xsc) ) jsiConsolePrintf("RIC debug :                              ->                 xsc ( %d )\n",xsc);
							
							
							/*	miramos los casos de callbacks ...
							*/
							
							if ( (valorini == 3) || (valormed == 3) )
								{
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Callback (Sprites Special Zone) (3)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FSZ)
										{
										cbkflags |= EVT_FSZ;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
									
								if (!activocbksolido)
									{
								
									// hemos retrocedido porque hemos chocado con algo ... vemos si el sprite tiene CALLBACK de este evento ... fws -> Function Wall Stops										
									#ifdef PXT_VM
										
										// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
										if (g_listenMask & EVT_FWS)
											{
											cbkflags |= EVT_FWS;	update=true;
											}
										
									#else
										
										JsVar *tmpvar=jsvObjectGetChild(objp,"fws",0);
										if (jsvIsFunction(tmpvar))
											{
											// si que tiene la funcion definida de callback
										
											// creamos el objeto de callback:
											JsVar *tmpobj=jsvNewObject();
											jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FWS));
											jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
											jsvObjectSetChild(tmpobj,"args",objspr);
										
											// lo añadimos a la lista
											tmpvar=jsvObjectGetChild(context,"cbk",0);
											jsvArrayPush(tmpvar,tmpobj);
										
											jsvUnLock(tmpobj);
											}
										jsvUnLock(tmpvar);
										
									#endif
									
									}
								else
									{
										
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
									
									// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
									#ifdef PXT_VM
										// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
										cbkflags |= EVT_FTS;	update=true;
									#else
										
										JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
										if (jsvIsFunction(tmpvar))
											{
											// si que tiene la funcion definida de callback
										
											// creamos el objeto de callback:
											JsVar *tmpobj=jsvNewObject();
											jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
											jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
											jsvObjectSetChild(tmpobj,"args",objspr);
										
											// lo añadimos a la lista
											tmpvar=jsvObjectGetChild(context,"cbk",0);
											jsvArrayPush(tmpvar,tmpobj);
										
											jsvUnLock(tmpobj);
											}
										jsvUnLock(tmpvar);
										
									#endif
									
									}
								
								}
							
							}
						
						}
					
					}
				#ifdef PXT_VM
					else if (g_comportamientoatravesable == 1)								// en comportamiento de zona ANCHA no podemos estar los pies en el aire
				#else
					else if (precalculatedscene.comportamientoatravesable == 1)				// en comportamiento de zona ANCHA no podemos estar los pies en el aire
				#endif
					{

					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en comportamiento zona ANCHA no salimos por la IZQ\n"); }
					
					// NUEVA fxsc
					*fxsc=xsca;
					#ifdef PXT_VM
						update=true;
					#else
						jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
					#endif
						
					// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
					xsc=xsca;
					xoi=xsc - (spw >> 1);
					xi=xoi;				if (xi < 0) { xi=0; }
					xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
					
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
					
					}
				
				}
			
			}
		
		}
	else																				// estamos SUBIENDO o moviendo HORIZONTAL o NO nos movemos
		{
		
		// DEBUG
		if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : NO bajamos\n"); }
		
		// Chocamos con la cabeza del SPRITE ? y no podemos subir mas ...
		if (subiendo)
			{
				
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : SI subiendo\n"); }
			
			
			/*	miramos que no choquemos con la cabeza con algo por encima, ...
			*/
			
			// puede ser que subamos y nos movamos DER o IZQ a la vez, subir en diagonal !!
			// hacemos los calculos (miramos todos las coordenadas que ha subido el SPRITE) ...
			// 'yiant' la comprobacion de donde estabamos antes , y 'yi' la comprobacion de donde estaremos ahora
			int valorcualquiera;
			int yiant=ysca - (sph >> 1);
			indiceini=( (yiant / cellh) * cellperline ) + (xi / cellw);
			indicefin=( (yiant / cellh) * cellperline ) + (xf / cellw);
			indicemed=indiceini + ( (indicefin -  indiceini) / 2 );
			int hastaindiceini=( (yi / cellh) * cellperline ) + (xi / cellw);
			do
				{
					
				if ( ( valorcualquiera=GMA_MapaPathsFromIndex(indicemed) ) != 0 ) break;
				if (xsc > xsca)																// UP-DER 
					{
					// la esquina sup-der (indicefin) no comprobamos, ya se comprobara al mirar mov. DER
					
					if ( ( valorcualquiera=GMA_MapaPathsFromIndex(indiceini) ) != 0 ) break;
					}
				else if (xsc < xsca)														// UP-IZQ
					{
					// la esquina sup-izq (indiceini) no comprobamos, ya se comprobara al mirar mov. IZQ
					
					if ( ( valorcualquiera=GMA_MapaPathsFromIndex(indicefin) ) != 0 ) break;
					}
				else																		// solo UP
					{
					if ( ( valorcualquiera=GMA_MapaPathsFromIndex(indiceini) ) != 0 ) break;
					if ( ( valorcualquiera=GMA_MapaPathsFromIndex(indicefin) ) != 0 ) break;
					}
				
				/* una fila mas arriba */
				indiceini-=cellperline;
				indicemed-=cellperline;
				indicefin-=cellperline;
				
				} while ( indiceini >= hastaindiceini );
			
			// hemos topado con la cabeza ?
			if (valorcualquiera != 0)
				{
					
				// DEBUG
				define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     topado con la cabeza\n"); }
				
				if (valorcualquiera == 2)												// si es solido atravesable lo ignoramos
					{
					// no hacemos nada, ya seguimos con las demas comprobaciones
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     atravesable y NO topamos con la cabeza\n"); }
					
					}
				else if (valorcualquiera == 3)											// si es zona especial por defecto atravesamos ...
					{
					// hemos tocado con la cabeza algo especial y debemos comprobar que pasa
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     SPECIAL ZONE atravesable y NO topamos con la cabeza\n"); }
					
					// DEBUG
					define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
					if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Callback (Sprites Special Zone) (4)\n");
					
					// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
					#ifdef PXT_VM
						
						// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
						if (g_listenMask & EVT_FSZ)
							{
							cbkflags |= EVT_FSZ;	update=true;
							}
						
					#else
						
						JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
						if (jsvIsFunction(tmpvar))
							{
							// si que tiene la funcion definida de callback
							
							// creamos el objeto de callback:
							JsVar *tmpobj=jsvNewObject();
							jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
							jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
							jsvObjectSetChild(tmpobj,"args",objspr);
							
							// lo añadimos a la lista
							tmpvar=jsvObjectGetChild(context,"cbk",0);
							jsvArrayPush(tmpvar,tmpobj);
							
							jsvUnLock(tmpobj);
							}
						jsvUnLock(tmpvar);
						
					#endif
					
					}
				else																	// chocamos con solido ...				
					{
					
					// si estamos con movimiento por JS y hay callback de topar cabeza, dejamos seguir subiendo ...
					#ifdef PXT_VM
					
						if ( controljs && (g_listenMask & EVT_FCS) )
						
					#else
						
						JsVar *tmpvar=jsvObjectGetChild(objp,"fcs",0);
						if ( controljs && jsvIsFunction(tmpvar) )
						
					#endif
						{
						// no tocamos las coordenadas ...
							// NO recuerdo este caso porque lo puse, en que condicion es bueno no tocar las coordenadas !!!
						
						// DEBUG
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     ... dejamos seguir subiendo, CBK se encarga\n");
						
						}
					else
						{
						
						// DEBUG
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     ... no podemos subir\n");
						
						// vamos bajando hasta encontrar donde no topamos con la cabeza
						do	{
							indicemed+=cellperline;			/* una fila mas abajo */
							} while( GMA_MapaPathsFromIndex(indicemed) != 0 );
						
						// calculamos que Y inicial es y modificamos la nueva Y del Sprite
						int nuevayinicial=( (indicemed / cellperline) * cellh );
						int diferencia=yoi - nuevayinicial;									// hemos de usar la yi real (no crop de outscreen)
						*fysc=ysc - diferencia;												// actualizamos para los siguientes calculos de las funciones de arriba
						#ifdef PXT_VM
							update=true;
						#else
							jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
						#endif
						
						// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
						ysc=(int)(*fysc);
						yoi=ysc - (sph >> 1);
						yi=yoi;
						yf=yoi + sph - 1;
						if (yi < 0) { yi=0; }
						if (yf >= sch) { yf=sch - 1; }
						
						if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              ->     ysc ( bajamos de %d a %d )\n",yscdebug,ysc);
						
						}
					
					
					/*	chocamos con techo programamos callback por si se quiere hacer algo especial
					*/
					
					if (!activocbksolido)
						{
							
						// DEBUG
						define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Callback (Sprites Ceil Stop)\n");
				
						// vemos si el sprite tiene CALLBACK de este evento ... fcs -> Funcion Ceil Stops
						#ifdef PXT_VM
						
							if (g_listenMask & EVT_FCS)
								{
								cbkflags |= EVT_FCS;	update=true;
								}
							
						#else
							
						// ya esta hecho antes -> tmpvar=jsvObjectGetChild(objp,"fcs",0);
							if (jsvIsFunction(tmpvar))
								{
								// si que tiene la funcion definida de callback
								
								// creamos el objeto de callback:
								JsVar *tmpobj=jsvNewObject();
								jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FCS));
								jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
								jsvObjectSetChild(tmpobj,"args",objspr);
								
								// lo añadimos a la lista
								tmpvar=jsvObjectGetChild(context,"cbk",0);
								jsvArrayPush(tmpvar,tmpobj);
								
								jsvUnLock(tmpobj);
								}
							// se libera mas adelante -> jsvUnLock(tmpvar);
							
						#endif
						
						}			
					else
						{
							
						// DEBUG
						define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
						if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
						
						// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)
						#ifdef PXT_VM
						
							// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
							cbkflags |= EVT_FTS;	update=true;
							
						#else
							
							jsvUnLock(tmpvar);
							tmpvar=jsvObjectGetChild(objp,"fts",0);
							if (jsvIsFunction(tmpvar))
								{
								// si que tiene la funcion definida de callback
								
								// creamos el objeto de callback:
								JsVar *tmpobj=jsvNewObject();
								jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
								jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
								jsvObjectSetChild(tmpobj,"args",objspr);
								
								// lo añadimos a la lista
								tmpvar=jsvObjectGetChild(context,"cbk",0);
								jsvArrayPush(tmpvar,tmpobj);
								
								jsvUnLock(tmpobj);
								}
							// se libera mas adelante -> jsvUnLock(tmpvar);
							
						#endif
						
						}
					
					#ifndef PXT_VM
						jsvUnLock(tmpvar);
					#endif
					
					}
				
				}
			else																		// esta rama solo es para DEBUG
				{
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     NO topamos con la cabeza\n"); }
				
				// PENDIENTE en el caso de comportamiento en zona ANCHA, no podemos subir por el aire ?
				#ifdef PXT_VM
					if (g_comportamientoatravesable == 1)
				#else
					if (precalculatedscene.comportamientoatravesable == 1)
				#endif
					{		
				
					// PENDIENTE tambien en zona ancha he hecho muchos casos que retrocedo a las coordenadas anteriores, y lo mas preciso seria colocarse al limite de las zonas tipo 2 atravesables !!!

					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     PENDIENTE en comportamiento zona ANCHA mirar que no subamos al aire\n"); }
					
					}
				
				}
			
			}
		else																			// esta rama solo es para DEBUG
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : NO subimos\n"); }
			
			}
	
		// Caso en que NO NOS MOVEMOS, miramos si estamos cayendo ...
		bool caemos=false;
		if ( !nosmovemos && !controljs )												// NO nos movemos (y no hay control de mov. por JS)
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : NO nos movemos\n"); }
			
			// caso especial, si el SPRITE esta en una plataforma, NO miramos debajo nuestro !!!
			#ifdef PXT_VM
				if (g_ptfm == false)
			#else
				JsVar *objptfm=jsvObjectGetChild(objspr,"ptfm",0);							// o es el objeto plataforma o es una JsVar NULL que no es lo mismo que NULL en C++
				if ( !jsvIsObject(objptfm) )
			#endif
				{
				
				/*	primero aprovechamos que ya tenemos los valores de las cells de los pies del SPRITE, para comprobar una casuistica :
						- que es que debajo de los pies hay solido atravesable (indice 2) , y en los pies tambien lo hay,
						  es decir estamos en medio de solido atravesable y debemos caer !!!
						- en cambio si debajo de los pies hay solido atravesable pero en los pies hay nada (indice 0 o depende tambien 3) entonces
						  es que ya estamos en una plataforma y NO hay que caer
				*/
				bool piesatravesable=false;
				#ifdef PXT_VM
					if ( g_comportamientospecial == 1 )											// el special se considera atravesable
				#else
					if (precalculatedscene.comportamientospecial == 1)							// el special se considera atravesable
				#endif
					{
					
					if ( valorini != 1 && valormed != 1 && valorfin != 1
										&&
						 ( valorini == 2 || valormed == 2 || valorfin == 2 )				// pero alguno ha de ser 2 (atravesable) ; pero todo 0's NO es atravesable en los pies
					   )
						{
						piesatravesable=true;
						
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en los pies hay solido atravesable o special atravesable\n"); }
						}
					
					}
				else
					{
					
					if ( ( valorini == 0 || valorini == 2 )
										  &&
						 ( valormed == 0 || valormed == 2 )
										  &&
						 ( valorfin == 0 || valorfin == 2 )
										  &&
						 ( valorini == 2 || valormed == 2 || valorfin == 2 )				// pero alguno ha de ser 2 (atravesable) ; pero todo 0's NO es atravesable en los pies
					   )
						{	
						piesatravesable=true;
						
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en los pies hay solido atravesable\n"); }
						}
					
					}
				
				// DEBUG (NO sirve para nada mas , se puede optimizar eliminando)
				if ( (valorini == 3) || (valormed == 3) || (valorfin == 3) )
					{
					if ( (valorini != 1) && (valormed != 1) && (valorfin != 1) )
						{
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en los pies hay SPECIAL ZONE (1)\n"); }
						}
					}
				
				// miramos que no caigamos hacia abajo, hemos de mirar debajo nuestro
				// si la Y que tenemos que comprobar esta offscreen NO podemos calcularlo
				int yfabajo=yoi + sph - 1;						// yf original
				yfabajo+=1;										// 1 pixel por debajo del sprite
				if (yfabajo >= sch)								// NO podemos calcular !!
					{
					// NO podemos calcular si caemos, estamos fuera de la escena !!!
					
					// DEBUG
					define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
					jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> WARNING caso que no deberia darse (1)\n");
					
					}
				else
					{
					
					/*	miramos si en estas nuevas celdas no hay solido y puede ser que haya un agujero y caemos, o un peldaño de bajada
								Esquema:
									
									( lo he generado con -> https://asciiflow.com/#/ )
																			
										+------------------+    
										|                  |    
										|                  |    
										|                  |    
										|                  |
										|                  |    
										+------------------+    
										xxxxxxxxxxxxxxxxxxxx
										^        ^        ^     
										|        |        |     
										|        |        |     
										|        |        |     
									indiceini    |     indicefin
												 |              
											 indicemed			
					*/	
					indiceini=( (yfabajo / cellh) * cellperline ) + (xi / cellw);	valorini=GMA_MapaPathsFromIndex(indiceini);
					indicefin=( (yfabajo / cellh) * cellperline ) + (xf / cellw);	valorfin=GMA_MapaPathsFromIndex(indicefin);
					indicemed=indiceini + ( (indicefin -  indiceini) / 2 );			valormed=GMA_MapaPathsFromIndex(indicemed);
					
					// si aparentemente en estos tres puntos equidistantes NO hay nada debajo, hacemos una comprobacion precisa
					// por defecto la zona especial SI es solido
					bool nadadebajo=false;
					#ifdef PXT_VM
						if ( g_comportamientoatravesable == 1 )										// el special se considera atravesable
					#else
						if (precalculatedscene.comportamientospecial == 1)							// el special se considera atravesable
					#endif
						{
						
						if 	( ( valorini == 0 || valorini == 3 )
											  &&
							  ( valormed == 0 || valormed == 3 ) 
											  &&
							  ( valorfin == 0 || valorfin == 3 )
							)
							{
							nadadebajo=true;
							}
						
						}
					else
						{
						
						if ( (valorini == 0) && (valormed == 0) && (valorfin == 0) ) nadadebajo=true;
						
						}

						
					if (nadadebajo == true)
						{
						
						for(int i=(indiceini+1);i < (indicefin-1);i++)
							{
							int valor=GMA_MapaPathsFromIndex(i);
							
							#ifdef PXT_VM
								if ( g_comportamientospecial == 1 )							// el special se considera atravesable
							#else
								if (precalculatedscene.comportamientospecial == 1)			// el special se considera atravesable
							#endif
								{
								if ( valor != 0 && valor != 3 ) nadadebajo=false;
								}
							else
								{
								if (valor != 0) nadadebajo=false;
								}
							
							}
							
						// DEBUG
						if (nadadebajo == false)
							{
							if (xsca != xscdebug)												// solo lo mostramos si estamos desplazandonos ...
								{
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Comprobacion precisa para ver que estamos encima solido\n");
								}
							}
						}
					
					if (nadadebajo == true)														// si NO topamos con nada debajo nuestro !!	
						{
						
						#ifdef PXT_VM
							int MAX_CELDAS_CAIDA=g_MAX_CELDAS_CAIDA;
						#else
							int MAX_CELDAS_CAIDA=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mcc",0));
						#endif
						
						// vemos hasta cuando tenemos un solido debajo (contamos cuantas celdas hay vacias por debajo)
						int indiceok;
						int alturaceldas=0;
						
						#ifdef PXT_VM
							if ( g_comportamientospecial == 1 )									// el special se considera atravesable
						#else
							if (precalculatedscene.comportamientospecial == 1)					// el special se considera atravesable
						#endif
							{
							
							do	{
								alturaceldas++;
								
								int v;
								
								indiceini+=cellperline;											// una fila mas por debajo
								if ( (v=GMA_MapaPathsFromIndex(indiceini)) != 0 && v != 3)		// ya hemos topado
									{
									indiceok=indiceini;
									break;
									}
								
								indicemed+=cellperline;											// una fila mas por debajo
								if ( (v=GMA_MapaPathsFromIndex(indicemed)) != 0 && v != 3)		// ya hemos topado
									{
									indiceok=indicemed;
									break;
									}
								
								indicefin+=cellperline;											// una fila mas por debajo
								if ( (v=GMA_MapaPathsFromIndex(indicefin)) != 0 && v != 3)		// ya hemos topado
									{
									indiceok=indicefin;
									break;
									}
								
								} while( alturaceldas <= MAX_CELDAS_CAIDA );
							
							}
						else
							{
						
							do	{
								alturaceldas++;
								
								indiceini+=cellperline;							// una fila mas por debajo
								if (GMA_MapaPathsFromIndex(indiceini) != 0)		// ya hemos topado
									{
									indiceok=indiceini;
									break;
									}
								
								indicemed+=cellperline;							// una fila mas por debajo
								if (GMA_MapaPathsFromIndex(indicemed) != 0)		// ya hemos topado
									{
									indiceok=indicemed;
									break;
									}
								
								indicefin+=cellperline;							// una fila mas por debajo
								if (GMA_MapaPathsFromIndex(indicefin) != 0)		// ya hemos topado
									{
									indiceok=indicefin;
									break;
									}
								
								} while( alturaceldas <= MAX_CELDAS_CAIDA );
								
							}
						
						if (alturaceldas <= MAX_CELDAS_CAIDA)									// peldaño de bajada
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Bajamos al peldanyo proximo (%d celdas)\n",alturaceldas);
							
							tipomovhorz=3;
							
							// nos colocamos en la altura que justo NO topamos, una fila anterior
							indiceok-=cellperline;
							
							// calculamos que Y final es, y modificamos la nueva Y del Sprite
							int nuevayfinal=( (indiceok / cellperline) * cellh ) + 1;
							yf=yoi + sph - 1;													// hemos de usar la yf real (no crop de outscreen)
							int diferencia=yf -	nuevayfinal;
								// NUEVA fysc
							*fysc=ysc - diferencia;												// actualizamos para los siguientes calculos
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
							#endif
							
							// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
							ysc=(int)(*fysc);
							yoi=ysc - (sph >> 1);
							yi=yoi;
							yf=yoi + sph - 1;
							if (yi < 0) { yi=0; }
							if (yf >= sch) { yf=sch - 1; }
							
							if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              ->     ysc ( bajamos de %d a %d )\n",yscdebug,ysc);
							
							}
						else																	// caida libre hacia abajo
							{
							caemos=true;
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Caida libre (%d celdas) (1)\n",alturaceldas);
								
							// empezamos a caer ... vemos si el sprite tiene CALLBACK de este evento ... ffd -> Function Fall Down
							#ifdef PXT_VM
								
								// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
								if (g_listenMask & EVT_FFD)
									{
									cbkflags |= EVT_FFD;	update=true;
									}
								else
									{
										
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");

									//jsiConsolePrintf("g_listenMask %u EVT_FFD %u\n",g_listenMask,EVT_FFD);
									
									}
								
							#else
								
								JsVar *tmpvar=jsvObjectGetChild(objspr,"ffd",0);
								if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffd",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
								if (jsvIsFunction(tmpvar))
									{
									// si que tiene la funcion definida de callback
									
									/*	creamos el objeto de callback:
											scene["cbk"].push(	{
																"k":		2,				<- un numero de una enumeracion de kinds (tipos)
																"args":		e,
																"acti":		action
																}
															 );
									*/
									JsVar *tmpobj=jsvNewObject();
									jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFD));
									jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
									jsvObjectSetChild(tmpobj,"args",objspr);
									
									// lo añadimos a la lista
									tmpvar=jsvObjectGetChild(context,"cbk",0);
									jsvArrayPush(tmpvar,tmpobj);
									
									jsvUnLock(tmpobj);
									}
								else
									{
									
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");
									
									}
								jsvUnLock(tmpvar);
								
							#endif
							
							}
						
						}
					else																		// caso que estamos en medio de solido atravesable, pero no tocamos suelo aun
						{
						
						// debajo nuestro hay aun solido atravesable, todo o un lado...
						if ( ((valorini == 0) || (valorini == 2))
											  &&
							 ((valormed == 0) || (valormed == 2))
											  &&
							 ((valorfin == 0) || (valorfin == 2)) )
							{
								
							// y justo en los pies tambien hay solido atravesable, pues hemos de caer ...
							if (piesatravesable)
								{
								
								// DEBUG
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             debajo de los pies hay solido atravesable\n"); }
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             estamos en medio de solido atravesable\n"); }
								
								caemos=true;
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Caida libre.\n");
									
								// empezamos a caer ... vemos si el sprite tiene CALLBACK de este evento ... ffd -> Function Fall Down
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FFD)
										{
										cbkflags |= EVT_FFD;	update=true;
										}
									else
										{
										
										// DEBUG
										define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
										if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");
										
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objspr,"ffd",0);
									if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffd",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFD));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									else
										{
									
										// DEBUG
										define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
										if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");
									
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						
						}
					
					}
				
				}
			else																		// esta rama solo es para DEBUG
				{
			
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : no porque estamos en PLATFORM (1)\n"); }
					
				}
			#ifndef PXT_VM
				jsvUnLock(objptfm);
			#endif
			
			}
			
		// si NO estamos cayendo comprobamos HORIZONTAL DER o IZQ
		if (!caemos)
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : NO nos caemos\n"); }
			
			
			/*	si por la DER o IZQ topamos, miramos que hacer ...
							Esquema:
													  xf                     
																			 
								 dir. move sprite      |                     
							   -------------------->   |                     
													   |                     
													   v                     
											+-----------+<------ indiceini   
											|x         x|                    
											|x         x|                    
											|x         x|                    
											|x         x|                    
											|x         x|                    
											|x         x|<-------indicemed
											|x         x|                    
											|x         x|                    
											|x         x|                    
											|x         x|<-------indicepelda 
											|x         x|                    
											+-----------+<-------indicefin   
											 ^                               
											 |    dir. move sprite           
											 | <-------------------          
											xi
			*/
		
			// primero comprobamos el lado hacia el cual nos dirigimos ...
			if (xsc > xsca)																// hacia la DER nos estamos moviendo
				{
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : SI movemos DER\n"); }
				
				// derecha
				int xc=xf;
			
				//calculamos los indices de las cells del lateral derecha del SPRITE y si en estas celdas topamos con algo solido
				indiceini=( (yi / cellh) * cellperline ) + (xc / cellw);							valorini=GMA_MapaPathsFromIndex(indiceini);
				indicemed=( ( ( yi + ( (yf-yi) / 2 ) ) / cellh ) * cellperline ) + (xc / cellw);	valormed=GMA_MapaPathsFromIndex(indicemed);
				indicefin=( (yf / cellh) * cellperline ) + (xc / cellw);							valorfin=GMA_MapaPathsFromIndex(indicefin);
				
				// SI topamos por la DER
				if ( (valorini != 0) || (valormed != 0) || (valorfin != 0) )
					{
					bool retrocederX=false;
					int indiceok; int offset;		// vemos que indice usamos para retroceder
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     SI topamos DER\n"); }
					
					// miramos la colision de mitad superior del SPRITE
					if ( (valorini == 1) || (valormed == 1) ) 									// si ini o medio topa (no es atravesable) retrocedemos ...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ini o medio topa (no es atravesable) retrocedemos\n"); }
						
						// no hacemos nada , ya retrocedemos mas adelante
						retrocederX=true;
						if (valorini == 1) { indiceok=indiceini; offset=0; }
						else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }
						}
					else if ( valorini == 3 || valormed == 3 ||  valorfin == 3) 				// si topa con especial, POR DEFECTO dejamos atravesar , pero activamos CALLBACK...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             topa con especial, default atravesable\n"); }
						
						/*	antes no dejabamos pasar y retrocediamos ...
						// no hacemos nada , ya retrocedemos mas adelante
						retrocederX=true;
						if (valorini == 3) { indiceok=indiceini; offset=0; }
						else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }
						*/
						
						// DEBUG
						define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             Callback (Sprites Special Zone) (5)\n");
						
						// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
						#ifdef PXT_VM
							
							// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
							if (g_listenMask & EVT_FSZ)
								{
								cbkflags |= EVT_FSZ;	update=true;
								}
							
						#else
							
							JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
							if (jsvIsFunction(tmpvar))
								{
								// si que tiene la funcion definida de callback
								
								// creamos el objeto de callback:
								JsVar *tmpobj=jsvNewObject();
								jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
								jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
								jsvObjectSetChild(tmpobj,"args",objspr);
								
								// lo añadimos a la lista
								tmpvar=jsvObjectGetChild(context,"cbk",0);
								jsvArrayPush(tmpvar,tmpobj);
								
								jsvUnLock(tmpobj);
								}
							jsvUnLock(tmpvar);
							
						#endif
						
						}
					else if ( (valorini == 2) || (valormed == 2) ) 								// si ini o medio topa con atravesable ...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ini o medio topa con atravesable\n"); }
						
						}
						
					// si NO hemos de retroceder , ahora miramos los PIES
					if ( !retrocederX && ( (valorfin == 1) || (valorfin == 2) ) )				// ahora o topamos por abajo o es peldaño o atravesamos
						{

						// si hay control de movimiento de JS
						if ( controljs && (subiendo || (valorfin == 2) ) ) 						// estamos subiendo o es atravesable ...
							{
							// no hacemos nada , podemos atravesar
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             atravesamos\n");
							
							}
						else																	// ahora o topamos por abajo o es peldaño
							{
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     o topamos por abajo o es peldaño\n"); }
							
							// comprobaremos que no sea PELDAÑO
							#ifdef PXT_VM
								int MAX_PIXELS_PELDANYO=g_MAX_PIXELS_PELDANYO;
							#else
								int MAX_PIXELS_PELDANYO=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mpp",0));
							#endif
							int indicepelda=( ( (yf-MAX_PIXELS_PELDANYO) / cellh ) * cellperline ) + (xc / cellw);
							int valorpelda=GMA_MapaPathsFromIndex(indicepelda);
							
							if (valorpelda == 0)												// si es peldaño nos subimos hacia arriba del peldaño, NO hemos de retrodecer la X ...
								{
								// podemos seguir adelante subiendo ...
								
								// vamos subiendo hasta una celda que ya no tenemos solido
								int alturaceldas=0;
								do	{
									alturaceldas++;
									
									indicefin-=cellperline;			// una fila mas por encima
									} while( GMA_MapaPathsFromIndex(indicefin) != 0 );
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Peldanyo de subida DER (%d celdas)\n",alturaceldas);
								
								tipomovhorz=2;
								
								// calculamos que Y final es y modificamos la nueva Y del Sprite
								int nuevayfinal=( (indicefin / cellperline) * cellh ) + 1;
								yf=yoi + sph - 1;															// hemos de usar la yf real (no crop de outscreen)
								int diferencia=yf -	nuevayfinal;
									// NUEVA fysc
								*fysc=ysc - diferencia;														// actualizamos para los siguientes calculos de las funciones de arriba								
								#ifdef PXT_VM
									update=true;
								#else
									jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
								#endif
								
								// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
								ysc=(int)(*fysc);
								yoi=ysc - (sph >> 1);
								yi=yoi;
								yf=yoi + sph - 1;
								if (yi < 0) { yi=0; }
								if (yf >= sch) { yf=sch - 1; }
								
								if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              ->                 ysc ( %d )\n",ysc);
								
								}							
							else if (valorpelda == 2)											// es atravesable con lo que topamos ...
								{
								// NO hacemos nada dejamos avanzar ...
								// OJO esta condicion la añadi para que podamos andar por una calle ancha, tipo DoubleDragon
								
								// PENDIENTE ver si da problemas con otros tipos de Map Paths
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Altura de los pies atravesable, dejamos avanzar\n");
								}
							else																// hemos de retroceder
								{
								// no hacemos nada , ya retrocedemos mas adelante
								retrocederX=true;
								indiceok=indicefin; offset=(yf - yi);							// aqui el offset tiene truco es para eliminar la yi
								}
							
							}
						
						}
					
					// si hay que retroceder la X hacia la IZQ
					if (retrocederX)
						{
							
						// caso especial , SI no tenemos solido debajo nuestro caemos , NO retrocedemos ...
						// si la Y que tenemos que comprobar esta offscreen NO podemos calcularlo
						// y si no hay funcion de caida tampoco lo aplicamos !!!
							// caemos YA es false cuando llegamos aqui
							// OJO - si estamos en controjs NO comprobamos porque el movimiento es libre, no hay caida !!!
						if (!controljs )
							{
								
							#ifdef PXT_VM
							
								if (g_listenMask & EVT_FFD)
								
							#else
								
								tmpvar=jsvObjectGetChild(objspr,"ffd",0); if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffd",0);		// vemos si el sprite tiene CALLBACK de este evento ... ffd -> Function Fall Down
								if (jsvIsFunction(tmpvar))
									
							#endif
								{
								
								int yfabajo=yoi + sph - 1;						// yf original
								yfabajo+=1;										// 1 pixel por debajo del sprite
								if (yfabajo >= sch)								// NO podemos calcular !!
									{
									// NO podemos calcular si caemos, estamos fuera de la escena !!!
									
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> WARNING caso que no deberia darse (2)\n");
									
									}
								else
									{
									
									indiceini=( (yfabajo / cellh) * cellperline ) + (xi / cellw);	valorini=GMA_MapaPathsFromIndex(indiceini);
									indicefin=( (yfabajo / cellh) * cellperline ) + (xf / cellw);	valorfin=GMA_MapaPathsFromIndex(indicefin);
									indicemed=indiceini + ( (indicefin -  indiceini) / 2 );			valormed=GMA_MapaPathsFromIndex(indicemed);
									
									// debajo nuestro hay aun solido atravesable, todo o un lado...
									if ( ((valorini == 0) || (valorini == 2))
														  &&
										 ((valormed == 0) || (valormed == 2))
														  &&
										 ((valorfin == 0) || (valorfin == 2)) )
										{
										// NO retrocedemos nada , mas adelante la condicion de caer ya actuara
										caemos=true;
										}
									
									}
								
								}
							#ifndef PXT_VM
								jsvUnLock(tmpvar);
							#endif
							
							}
						
						// si NO se ha dado el caso especial de caer, SI retrocedemos
						if (!caemos)
							{
							
							// calculamos el indice 0 de esta fila (es lo maximo que podemos retroceder)
							int indiceminimo=( ( ( yi + offset ) / cellh ) * cellperline ) + 0;
							
							// nos colocamos al maximo pero que justo NO topamos
							do	{
								indiceok--;				// una X anterior
								} while( GMA_MapaPathsFromIndex(indiceok) == 1 && indiceok > indiceminimo );
							// y luego habia esto pero me deba error porque atravesable hay que dejar retroceder si no es en los pies ->	} while( (GMA_MapaPathsFromIndex(indiceok) != 0 && GMA_MapaPathsFromIndex(indiceok) != 3) && (indiceok > indiceminimo) );
							// antes habia (pero daba error el las escaleras) ->	} while( (GMA_MapaPathsFromIndex(indiceok) != 0) && (indiceok > indiceminimo) );
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Topamos DER y retrocedemos a IZQ\n");
							
							// DEBUG
							if (indiceok == indiceminimo) { jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING topamos DER y hemos retrocedido hasta la X=0\n"); } 
							
							// calculamos que X final es
							int nuevaxfinal=(indiceok % cellperline) * cellw;
							
							// calculamos la diferencia y modificamos la nueva X del Sprite
							nuevaxfinal+=(cellw-1);													// las celdas son multiplo 2 siempre, y me coloco la xf justo al final del ultimo pixel de la celda
							xf=xoi + spw - 1;														// hemos de usar la xf real (no crop de outscreen)
							int diferencia=xf -	nuevaxfinal;
								// NUEVA fxsc
							*fxsc=xsc - diferencia;													// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromFloat(*fxsc));
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=(int)(*fxsc);
							xoi=xsc - (spw >> 1);
							xi=xoi;
							xf=xoi + spw - 1;
							if (xi < 0) { xi=0; }
							if (xf >= scw) { xf=scw - 1; }
							
							if ( (debugconsole) && (xscdebug != xsc) ) jsiConsolePrintf("RIC debug :                              -> xsc ( %d )\n",xsc);
							
							if (!activocbksolido)
								{
								
								// hemos retrocedido porque hemos chocado con algo ... vemos si el sprite tiene CALLBACK de este evento ... fws -> Function Wall Stops
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FWS)
										{
										cbkflags |= EVT_FWS;	update=true;
										}
								
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fws",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
										
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FWS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
										
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
										
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
									
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)								
								#ifdef PXT_VM
									
									// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
									cbkflags |= EVT_FTS;	update=true;
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						else																	// esta rama solo es para DEBUG
							{
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             anulamos retroceder porque caeremos !!!\n"); }
							}
						
						}
					
					}
				else																			// esta rama solo es para DEBUG
					{
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     NO topamos DER\n"); }
					}
				
				}
			else if (xsc < xsca)														// hacia la IZQ nos estamos moviendo
				{
				
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : SI movemos IZQ\n"); }
				
				// ahora izquierda
				int xc=xi;
				
				//calculamos los indices de las cells del lateral izquierda del SPRITE y si en estas celdas topamos con algo solido
				indiceini=( (yi / cellh) * cellperline ) + (xc / cellw);							valorini=GMA_MapaPathsFromIndex(indiceini);
				indicemed=( ( ( yi + ( (yf-yi) / 2 ) ) / cellh ) * cellperline ) + (xc / cellw);	valormed=GMA_MapaPathsFromIndex(indicemed);
				indicefin=( (yf / cellh) * cellperline ) + (xc / cellw);							valorfin=GMA_MapaPathsFromIndex(indicefin);
				
				// SI topamos por la IZQ
				if ( (valorini != 0) || (valormed != 0) || (valorfin != 0) )
					{
					bool retrocederX=false;
					int indiceok; int offset;		// vemos que indice usamos para retroceder
					
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     topamos IZQ (2)\n"); }
					
					// miramos la colision de mitad superior del SPRITE
					if ( (valorini == 1) || (valormed == 1) ) 									// si ini o medio topa (no es atravesable) retrocedemos ...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ini o medio topa (no es atravesable) retrocedemos\n"); }
						
						// no hacemos nada , ya retrocedemos mas adelante
						retrocederX=true;
						if (valorini == 1) { indiceok=indiceini; offset=0; }
						else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }						
						}
					else if ( valorini == 3 || valormed == 3 ||  valorfin == 3) 				// si topa con especial, POR DEFECTO dejamos atravesar , pero activamos CALLBACK...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ini o medio topa con especial, default atravesable\n"); }
						
						/*	antes no dejabamos pasar y retrocediamos ...
						// no hacemos nada , ya retrocedemos mas adelante
						retrocederX=true;
						if (valorini == 3) { indiceok=indiceini; offset=0; }
						else { indiceok=indicemed; offset=( (yf-yi) / 2 ); }
						*/
						
						// DEBUG
						define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
						if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Callback (Sprites Special Zone) (7)\n");
						
						// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
						#ifdef PXT_VM
							
							// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
							if (g_listenMask & EVT_FSZ)
								{
								cbkflags |= EVT_FSZ;	update=true;
								}
							
						#else
							
							JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
							if (jsvIsFunction(tmpvar))
								{
								// si que tiene la funcion definida de callback
								
								// creamos el objeto de callback:
								JsVar *tmpobj=jsvNewObject();
								jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
								jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
								jsvObjectSetChild(tmpobj,"args",objspr);
								
								// lo añadimos a la lista
								tmpvar=jsvObjectGetChild(context,"cbk",0);
								jsvArrayPush(tmpvar,tmpobj);
								
								jsvUnLock(tmpobj);
								}
							jsvUnLock(tmpvar);
							
						#endif
						
						}
					else if ( (valorini == 2) || (valormed == 2) ) 								// si ini o medio topa con atravesable ...
						{
						// DEBUG
						if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             ini o medio topa con atravesable\n"); }
						
						}
					
					// si NO hemos de retroceder , ahora miramos los PIES
					if ( !retrocederX && ( (valorfin == 1) || (valorfin == 2) ) )				// ahora o topamos por abajo o es peldaño o atravesamos
						{

						// si hay control de movimiento de JS
						if ( controljs && (subiendo || (valorfin == 2) ) ) 						// estamos subiendo o es atravesable ...
							{
							// no hacemos nada , podemos atravesar
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             atravesamos\n");
							
							}
						else																	// ahora o topamos por abajo o es peldaño
							{
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     o topamos por abajo o es peldaño\n"); }
							
							// comprobaremos que no sea PELDAÑO
							#ifdef PXT_VM
								int MAX_PIXELS_PELDANYO=g_MAX_PIXELS_PELDANYO;
							#else
								int MAX_PIXELS_PELDANYO=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mpp",0));
							#endif
							int indicepelda=( ( (yf-MAX_PIXELS_PELDANYO) / cellh ) * cellperline ) + (xc / cellw);
							int valorpelda=GMA_MapaPathsFromIndex(indicepelda);
							
							if (valorpelda == 0)												// si es peldaño nos subimos hacia arriba del peldaño, NO hemos de retrodecer la X ...
								{
								// podemos seguir adelante subiendo ...
								
								// vamos subiendo hasta que celda ya no tenemos solido
								int alturaceldas=0;
								do	{
									alturaceldas++;
									
									indicefin-=cellperline;			// una fila mas por encima
									} while( GMA_MapaPathsFromIndex(indicefin) != 0 );
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Peldanyo de subida IZQ (%d celdas)\n",alturaceldas);
								
								tipomovhorz=2;
								
								// calculamos que Y final es y modificamos la nueva Y del Sprite
								int nuevayfinal=( (indicefin / cellperline) * cellh ) + 1;
								yf=yoi + sph - 1;															// hemos de usar la yf real (no crop de outscreen)
								int diferencia=yf -	nuevayfinal;
									// NUEVA fysc
								*fysc=ysc - diferencia;														// actualizamos para los siguientes calculos de las funciones de arriba
								#ifdef PXT_VM
									update=true;
								#else
									jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
								#endif
								
								// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
								ysc=(int)(*fysc);
								yoi=ysc - (sph >> 1);
								yi=yoi;
								yf=yoi + sph - 1;
								if (yi < 0) { yi=0; }
								if (yf >= sch) { yf=sch - 1; }
								
								if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              -> ysc ( %d )\n",ysc);
								
								}
							else if (valorpelda == 2)											// es atravesable con lo que topamos ...
								{
								// NO hacemos nada dejamos avanzar ...
								// OJO esta condicion la añadi para que podamos andar por una calle ancha, tipo DoubleDragon
								// PENDIENTE ver si da problemas con otros tipos de Map Paths
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Altura de los pies atravesable, dejamos avanzar\n");
								}
							else
								{
								// no hacemos nada , ya retrocedemos mas adelante
								retrocederX=true;
								indiceok=indicefin; offset=(yf - yi);							// aqui el offset tiene truco es para eliminar la yi
								}
							
							}
						
						}
					
					// si hay que retroceder la X hacia la DER
					if (retrocederX)
						{
							
						// caso especial , SI no tenemos solido debajo nuestro caemos , NO retrocedemos ...
						// si la Y que tenemos que comprobar esta offscreen NO podemos calcularlo
						// y si no hay funcion de caida tampoco lo aplicamos !!!
							// caemos YA es false cuando llegamos aqui
							// OJO - si estamos en controjs NO comprobamos porque el movimiento es libre, no hay caida !!!							
						if (!controljs )
							{
								
							#ifdef PXT_VM
								
								if (g_listenMask & EVT_FFD)
								
							#else
								
								tmpvar=jsvObjectGetChild(objspr,"ffd",0); if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffd",0);		// vemos si el sprite tiene CALLBACK de este evento ... ffd -> Function Fall Down
								if (jsvIsFunction(tmpvar))
								
							#endif
								{
								
								int yfabajo=yoi + sph - 1;						// yf original
								yfabajo+=1;										// 1 pixel por debajo del sprite
								if (yfabajo >= sch)								// NO podemos calcular !!
									{
									// NO podemos calcular si caemos, estamos fuera de la escena !!!
									
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> WARNING caso que no deberia darse (3)\n");
									
									}
								else
									{
									
									indiceini=( (yfabajo / cellh) * cellperline ) + (xi / cellw);	valorini=GMA_MapaPathsFromIndex(indiceini);
									indicefin=( (yfabajo / cellh) * cellperline ) + (xf / cellw);	valorfin=GMA_MapaPathsFromIndex(indicefin);
									indicemed=indiceini + ( (indicefin -  indiceini) / 2 );			valormed=GMA_MapaPathsFromIndex(indicemed);
									
									// debajo nuestro hay aun solido atravesable, todo o un lado...
									if ( ((valorini == 0) || (valorini == 2))
														  &&
										 ((valormed == 0) || (valormed == 2))
														  &&
										 ((valorfin == 0) || (valorfin == 2)) )
										{
										// NO retrocedemos nada , mas adelante la condicion de caer ya actuara
										caemos=true;
										}
									
									}
								
								}
							#ifndef PXT_VM
								jsvUnLock(tmpvar);
							#endif
							
							}
						
						// si NO se ha dado el caso especial de caer, SI retrocedemos
						if (!caemos)
							{
							
							// calculamos el indice MAX de esta fila (es lo maximo que podemos retroceder hacia DER)
							int indicemaximo=( ( ( ( yi + offset ) / cellh ) + 1 ) * cellperline ) + 0;
							indicemaximo-=1;
							
							// nos colocamos al maximo pero que justo NO topamos
							do	{
								indiceok++;				// una X posterior
								} while( GMA_MapaPathsFromIndex(indiceok) == 1 && indiceok < indicemaximo );
							// y luego habia esto pero me deba error porque atravesable hay que dejar retroceder si no es en los pies ->	} while( (GMA_MapaPathsFromIndex(indiceok) != 0 && GMA_MapaPathsFromIndex(indiceok) != 3) && (indiceok < indicemaximo) );
							// antes habia (pero daba error el las escaleras) ->	} while( (GMA_MapaPathsFromIndex(indiceok) != 0) && (indiceok < indicemaximo) );
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 topamos IZQ (2) y retrocedemos a DER\n");
							
							// DEBUG
							if (indiceok == indicemaximo) { jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING topamos DER y hemos retrocedido hasta la X=0\n"); }
											
							// calculamos que X final es
							int nuevaxfinal=(indiceok % cellperline) * cellw;
							
							// calculamos la diferencia y modificamos la nueva X del Sprite
							xi=xoi;																// hemos de usar la xi real (no crop de outscreen)
							int diferencia=xi -	nuevaxfinal;
								// NUEVA fxsc
							*fxsc=xsc - diferencia;												// actualizamos para los siguientes calculos de las funciones de arriba
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromFloat(*fxsc));
							#endif
							
							// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
							xsc=(int)(*fxsc);
							xoi=xsc - (spw >> 1);
							xi=xoi;
							xf=xoi + spw - 1;
							if (xi < 0) { xi=0; }
							if (xf >= scw) { xf=scw - 1; }
							
							if ( (debugconsole) && (xscdebug != xsc) ) jsiConsolePrintf("RIC debug :                              ->                 xsc ( %d )\n",xsc);
							
							if (!activocbksolido)
								{
							
								// hemos retrocedido porque hemos chocado con algo ... vemos si el sprite tiene CALLBACK de este evento ... fws -> Function Wall Stops
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FWS)
										{
										cbkflags |= EVT_FWS;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fws",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
										
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FWS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
										
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
										
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							else
								{
									
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Callback (Sprites Touch Solid)\n");
								
								// vemos si el sprite tiene CALLBACK de este evento ... fts	-> Function Touch solid			( es prioritaria, se activa cuando toca algun tipo de solido el SPRITE)									
								#ifdef PXT_VM
									
									// activocbksolido ya indica que hay un callback fts registrado (del SPRITE o del Kind SPRITE)
									cbkflags |= EVT_FTS;	update=true;
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objp,"fts",0);
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
									
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FTS));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
									
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
									
										jsvUnLock(tmpobj);
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						else																	// esta rama solo es para DEBUG
							{
							// DEBUG
							if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             anulamos retroceder porque caeremos !!!\n"); }
							}
						
						}
					
					}
				else																			// esta rama solo es para DEBUG
					{
					// DEBUG
					if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION :     NO topamos IZQ\n"); }
					}
				
				
				}
			
			}
		
		
		/*	Por ultimo miramos si caemos hacia abajo, caida libre o es un peldaño de bajada ...
		*/
		
		// vemos si hay aceleracion para volar ?
		#ifdef PXT_VM
			float fay=g_fay;
		#else		
			JsVarFloat fay=jsvGetFloatAndUnLock(jsvObjectGetChild(objspr,"ay",0));
				if ( isnan(fay) ) fay=0.0;								// OJO !!! en los FLOAT si no estuviera la propiedad "ay" devuleve NaN y lo pasamos a 0 que es lo que esperamos ...
			//jsiConsolePrintf("RIC debug : ay = %f\n",fay);
		#endif
		
		// DEBUG
		define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
		if (debugconsole && (fay < 0) ) jsiConsolePrintf("RIC debug :                              -> Hay aceleracion de volar\n");
		
		// si nos estamos moviendo, pero si el movimiento se controla desde Javascript ignoramos ...
		// o si esta activada la aceleracion Y del sprite hacia arriba, tambien ignoramos ... (permitimos el FLY , volar)
		if ( (nosmovemos && !controljs) && (fay >= 0) )
			{
			
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : Nos movemos mirar si caemos\n"); }
			
			// caso especial, si el SPRITE esta en una plataforma, NO miramos debajo nuestro !!!
			#ifdef PXT_VM
				if (g_ptfm == false)
			#else
				JsVar *objptfm=jsvObjectGetChild(objspr,"ptfm",0);							// o es el objeto plataforma o es una JsVar NULL que no es lo mismo que NULL en C++
				if ( !jsvIsObject(objptfm) )
			#endif
				{
					
				// hemos de mirar debajo nuestro
				// si la Y que tenemos que comprobar esta offscreen NO podemos calcularlo
				int yfabajo=yoi + sph - 1;						// yf original
				yfabajo+=1;										// 1 pixel por debajo del sprite
				if (yfabajo >= sch)								// NO podemos calcular !!
					{
					// NO podemos calcular si caemos, estamos fuera de la escena !!!
					
					// DEBUG
					define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
					jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos -> WARNING caso que no deberia darse (4)\n");
					
					}
				else
					{
					
					/*	miramos si en estas nuevas celdas no hay solido y puede ser que haya un agujero y caemos, o un peldaño de bajada
								Esquema:
									
									( lo he generado con -> https://asciiflow.com/#/ )
																			
										+------------------+    
										|                  |    
										|                  |    
										|                  |    
										|                  |
										|                  |    
										+------------------+    
										xxxxxxxxxxxxxxxxxxxx
										^        ^        ^     
										|        |        |     
										|        |        |     
										|        |        |     
									indiceini    |     indicefin
												 |              
											 indicemed			
					*/	
					indiceini=( (yfabajo / cellh) * cellperline ) + (xi / cellw);	valorini=GMA_MapaPathsFromIndex(indiceini);
					indicefin=( (yfabajo / cellh) * cellperline ) + (xf / cellw);	valorfin=GMA_MapaPathsFromIndex(indicefin);
					indicemed=indiceini + ( (indicefin -  indiceini) / 2 );			valormed=GMA_MapaPathsFromIndex(indicemed);
					
					// si aparentemente en estos tres puntos equidistantes NO hay nada debajo, hacemos una comprobacion precisa
					bool nadadebajo=false;
					if ( (valorini == 0) && (valormed == 0) && (valorfin == 0) )
						{
						
						nadadebajo=true;
						
						for(int i=(indiceini+1);i < (indicefin-1);i++)
							{
							if (GMA_MapaPathsFromIndex(i) != 0) nadadebajo=false;
							}
							
						// DEBUG
						if (nadadebajo == false)
							{
							if (xsca != xscdebug)				// solo lo mostramos si estamos desplazandonos ...
								{
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Comprobacion precisa para ver que estamos encima solido\n");
								}
							}
						
						}
					
					if (nadadebajo == true)														// si NO topamos con nada debajo nuestro !!
						{
							
						#ifdef PXT_VM
							int MAX_CELDAS_CAIDA=g_MAX_CELDAS_CAIDA;
						#else
							int MAX_CELDAS_CAIDA=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mcc",0));
						#endif
						
						// vemos hasta cuando tenemos un solido debajo (contamos cuantas celdas hay vacias por debajo)
						int indiceok;
						int alturaceldas=0;
						do	{
							alturaceldas++;
							
							indiceini+=cellperline;							// una fila mas por debajo
							if (GMA_MapaPathsFromIndex(indiceini) != 0)		// ya hemos topado
								{
								indiceok=indiceini;
								break;
								}
							
							indicemed+=cellperline;						// una fila mas por debajo
							if (GMA_MapaPathsFromIndex(indicemed) != 0)		// ya hemos topado
								{
								indiceok=indicemed;
								break;
								}
							
							indicefin+=cellperline;							// una fila mas por debajo
							if (GMA_MapaPathsFromIndex(indicefin) != 0)		// ya hemos topado
								{
								indiceok=indicefin;
								break;
								}
							
							} while( alturaceldas <= MAX_CELDAS_CAIDA );
							
						if (alturaceldas <= MAX_CELDAS_CAIDA)									// peldaño de bajada
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              -> Peldanyo de bajada (%d celdas)\n",alturaceldas);
							
							tipomovhorz=3;
							
							// nos colocamos en la altura que justo NO topamos, una fila anterior
							indiceok-=cellperline;
							
							// calculamos que Y final es, y modificamos la nueva Y del Sprite
							int nuevayfinal=( (indiceok / cellperline) * cellh ) + 1;
							yf=yoi + sph - 1;													// hemos de usar la yf real (no crop de outscreen)
							int diferencia=yf -	nuevayfinal;
								// NUEVA fysc
							*fysc=ysc - diferencia;												// actualizamos para los siguientes calculos
							#ifdef PXT_VM
								update=true;
							#else
								jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromFloat(*fysc));
							#endif
							
							// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
							ysc=(int)(*fysc);
							yoi=ysc - (sph >> 1);
							yi=yoi;
							yf=yoi + sph - 1;
							if (yi < 0) { yi=0; }
							if (yf >= sch) { yf=sch - 1; }
							
							if ( (debugconsole) && (yscdebug != ysc) ) jsiConsolePrintf("RIC debug :                              -> ysc ( %d )\n",ysc);
							
							}
						else																	// caida libre hacia abajo
							{
							
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     Caida libre (%d celdas) (2)\n",alturaceldas);
								
							// empezamos a caer ... vemos si el sprite tiene CALLBACK de este evento ... ffd -> Function Fall Down
							#ifdef PXT_VM
								
								// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
								if (g_listenMask & EVT_FFD)
									{
									cbkflags |= EVT_FFD;	update=true;
									}
								else
									{
									
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");
									
									}
								
							#else
								
								JsVar *tmpvar=jsvObjectGetChild(objspr,"ffd",0);
								if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"ffd",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
								if (jsvIsFunction(tmpvar))
									{
									// si que tiene la funcion definida de callback
								
									// creamos el objeto de callback:
									JsVar *tmpobj=jsvNewObject();
									jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FFD));
									jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
									jsvObjectSetChild(tmpobj,"args",objspr);
								
									// lo añadimos a la lista
									tmpvar=jsvObjectGetChild(context,"cbk",0);
									jsvArrayPush(tmpvar,tmpobj);
								
									jsvUnLock(tmpobj);
									}
								else
									{
								
									// DEBUG
									define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
									if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de caida libre 'ffd'\n");
								
									}
								jsvUnLock(tmpvar);
								
							#endif
							
							}
						
						}
					else																		// en algunos puntos hay solido, pero miramos que no estemos a punto de CAER
						{
							
						// Caso de que debajo de los pies haya SPECIAL ZONE ?
						if ( (valorini == 3) || (valormed == 3) || (valorfin == 3) )
							{
							
							// PENDIENTE - esta comprobacion NO la entiendo, con la anterior ya basta creo !!!
							if ( (valorini != 1) && (valormed != 1) && (valorfin != 1) )
								{
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             en los pies hay SPECIAL ZONE (default es solido) (2)\n"); }
								}
								
							// DEBUG
							define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
							if (debugconsole) jsiConsolePrintf("RIC debug :                              ->                 Callback (Sprites Special Zone) (9)\n");
							
							// vemos si el sprite tiene CALLBACK de este evento ... fsz -> Function Special Zone
							#ifdef PXT_VM
							
								// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
								if (g_listenMask & EVT_FSZ)
									{
									cbkflags |= EVT_FSZ;	update=true;
									}
							
							#else
								
								JsVar *tmpvar=jsvObjectGetChild(objp,"fsz",0);
								if (jsvIsFunction(tmpvar))
									{
									// si que tiene la funcion definida de callback
									
									// creamos el objeto de callback:
									JsVar *tmpobj=jsvNewObject();
									jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSZ));
									jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
									jsvObjectSetChild(tmpobj,"args",objspr);
									
									// lo añadimos a la lista
									tmpvar=jsvObjectGetChild(context,"cbk",0);
									jsvArrayPush(tmpvar,tmpobj);
									
									jsvUnLock(tmpobj);
									}
								jsvUnLock(tmpvar);
								
							#endif
							
							}
						
						// si estamos en comportamientoatravesable de zona ANCHA no dejamos que el personaje salga por DER o IZQ de sus limites
						#ifdef PXT_VM
							if (g_comportamientoatravesable == 1)
						#else
							if (precalculatedscene.comportamientoatravesable == 1)
						#endif
							{
							
							if ( (valorini == 1 || valorini == 2) && (valormed == 1 || valormed == 2) && (valorfin == 1 || valorfin == 2) )
								{
								// si todo debajo nuestro es atravesable, OK
								}
							else
								{
								// en este caso nos estamos moviendo por atravesable y a punto de salir de el y quedar volando, 
								// asi que no dejamos avanzar a la coord X y la coord Y, la dejamos como estaba...
								
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              ->             nos salimos de la zona ANCHA definida por el atravesable !\n"); }
								
								// NUEVA fxsc
								*fxsc=xsca;
								#ifdef PXT_VM
									update=true;
								#else
									jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
								#endif
									
								// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
								xsc=xsca;
								xoi=xsc - (spw >> 1);
								xi=xoi;				if (xi < 0) { xi=0; }
								xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
								
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
								
								// NUEVA fysc
								*fysc=ysca;
								#ifdef PXT_VM
									update=true;
								#else
									jsvObjectSetChildAndUnLock(objspr,"ysc",jsvNewFromInteger(ysca));			// ysca es INT
								#endif
								
								// actualizamos los valores de la Y para los siguientes calculos (ya que pueden haberse cambiado) ...
								ysc=ysca;
								yoi=ysc - (sph >> 1);
								yi=yoi;				if (yi < 0) { yi=0; }
								yf=yoi + sph - 1;	if (yf >= sch) { yf=sch - 1; }
								
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la ysc ( %d )\n",ysc);
								
								}
							
							}
						else
							{
							
							#ifdef PXT_VM
								int MAX_CELDAS_CAIDA=g_MAX_CELDAS_CAIDA;
							#else
								int MAX_CELDAS_CAIDA=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objp,"mcc",0));
							#endif
							int maxfilas=(sch / cellh);
							
							bool apunto=false;
							if ( (valorini == 0) && (valormed == 0) )								// a punto CAER por la IZQ ?
								{
									
								// pero si nos movemos DER entonces no !!!
								if (xsc < xsca)
									{
									// nos movemos hacia la IZQ (lado vacio): si hay poca distancia hasta el solido es un peldaño de bajada, NO About To Fall
									if (GMA_EsPeldanyoDeBajada(indiceini,indicemed,cellperline,maxfilas,MAX_CELDAS_CAIDA))
										{
										if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : por la IZQ es peldanyo de bajada, NO About To Fall\n"); }
										}
									else
										{
										apunto=true;													// caida real (mucha distancia)
										}
									}
								
								// DEBUG
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : a punto CAER por la IZQ ?  \n"); }
								
								}
							else if ( (valormed == 0) && (valorfin == 0) )							// a punto CAER por la DER ?
								{
								
								// pero si nos movemos IZQ entonces no !!!
								if (xsc > xsca)
									{
									// nos movemos hacia la DER (lado vacio): si hay poca distancia hasta el solido es un peldaño de bajada, NO About To Fall
									if (GMA_EsPeldanyoDeBajada(indicemed,indicefin,cellperline,maxfilas,MAX_CELDAS_CAIDA))
										{
										if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : por la DER es peldanyo de bajada, NO About To Fall\n"); }
										}
									else
										{
										apunto=true;													// caida real (mucha distancia)
										}
									}
								
								// DEBUG
								if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : a punto CAER por la DER ?  \n"); }
								
								}
							
							// SI que estamos a punto de caer  ...
							if (apunto == true)
								{
									
								// segun el comportamiento del atravesable ?
								#ifdef PXT_VM
									if (g_comportamientoatravesable == 1)
								#else
									if (precalculatedscene.comportamientoatravesable == 1)
								#endif
									{
									// en este caso nos estamos moviendo por atravesable y a punto de salir de el y quedar volando, 
									// asi que no dejamos avanzar a la coord X , la dejamos como estaba...
									
									// NUEVA fxsc
									*fxsc=xsca;
									#ifdef PXT_VM
										update=true;
									#else
										jsvObjectSetChildAndUnLock(objspr,"xsc",jsvNewFromInteger(xsca));		// xsca es INT
									#endif
										
									// actualizamos los valores de la X para los siguientes calculos (ya que pueden haberse cambiado) ...
									xsc=xsca;
									xoi=xsc - (spw >> 1);
									xi=xoi;				if (xi < 0) { xi=0; }
									xf=xoi + spw - 1;	if (xf >= scw) { xf=scw - 1; }
									
									if (debugconsole) jsiConsolePrintf("RIC debug :                              ->             restablecemos la xsc ( %d )\n",xsc);
									
									}
								else
									{
									// NO hacemos nada en este caso ...	
									}
								
								
								/*	creamos el callback si lo hay ...
								*/
								
								// DEBUG
								define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
								if (debugconsole) jsiConsolePrintf("RIC debug :                              ->     About To Fall\n");
								
								// ... vemos si el sprite tiene CALLBACK de este evento ... faf	-> Function About To Fall
								#ifdef PXT_VM
									
									// si tenemos registrado un callback, ya sea del SPRITE o del Kind SPRITE
									if (g_listenMask & EVT_FAF)
										{
										cbkflags |= EVT_FAF;	update=true;
										}
									
								#else
									
									JsVar *tmpvar=jsvObjectGetChild(objspr,"faf",0);
									if (!tmpvar) tmpvar=jsvObjectGetChild(objp,"faf",0);		// si NO tiene funcion propia, miramos si hay funcion generica para todos los CLONES
									if (jsvIsFunction(tmpvar))
										{
										// si que tiene la funcion definida de callback
										
										// creamos el objeto de callback:
										JsVar *tmpobj=jsvNewObject();
										jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FAF));
										jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
										jsvObjectSetChild(tmpobj,"args",objspr);
										
										// lo añadimos a la lista
										tmpvar=jsvObjectGetChild(context,"cbk",0);
										jsvArrayPush(tmpvar,tmpobj);
										
										jsvUnLock(tmpobj);
										}
									else
										{
										
										// DEBUG
										define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS
										if (debugconsole) jsiConsolePrintf("RIC debug : EstudioMovimiento_Obstaculos WARNING no hay callback de About to FALL 'faf'\n");
										
										}
									jsvUnLock(tmpvar);
									
								#endif
								
								}
							
							}
						
						}
						
					}
				
				}
			else																		// esta rama solo es para DEBUG
				{
			
				// DEBUG
				if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : no porque estamos en PLATFORM (2)\n"); }
					
				}
			#ifndef PXT_VM
				jsvUnLock(objptfm);
			#endif
			
			}
		else if (nosmovemos) 															// esta rama solo es para DEBUG
			{
				
			// DEBUG
			if (debugconditions) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS jsiConsolePrintf("RIC debug :                              -> CONDITION : IGNORAMOS al movernos DER o IZQ mirar si caemos\n"); }
			
			}
		
		}
	
	
	/*	Control de callback de si subidita, bajadita o plano
	*/

	if (tipomovhorz == 0) tipomovhorz=1;
	
	/*	vemos si el sprite tiene CALLBACK de este evento ... fsi -> Function Sprite Inclination
		
			el propio sprite tiene un atributo que lo indica , spr_new["inc"]
				0				: no sabemos
				1				: plano
				20				: acabamos de empezar subidita
				21,22,23 ...	: indica que estamos subidita y las unidades indica cuantos planos llevamos seguidos... (para no hacer movimiento escalera...
				30				: acabamos de empezar bajadita
				31,32,33 ...	: indica que estamos bajadita y las unidades indica cuantos planos llevamos seguidos... (para no hacer movimiento escalera...				
	*/
	#ifdef PXT_VM
		
		if (g_fsi == true)
		
	#else
		
		tmpvar=jsvObjectGetChild(objp,"fsi",0);					// OJO es funcion solo del padre ...
		if (jsvIsFunction(tmpvar))
		
	#endif
		{
		// si que tiene la funcion definida de callback
		
		#ifdef PXT_VM
			
			// 'inc' actual del SPRITE nos lo pasa TS (g_sprinc) y devolvemos el nuevo en data[3] (-1 si no cambia)
			int sprinc=g_sprinc;
			int nuevoinc=sprinc;
			bool dispararfsi=false;
			if (tipomovhorz == 1)												// viene plano (1)
				{
				
				// estamos bajadita o subidita, contamos los planos
				if (sprinc >= 20)
					{
					sprinc++;
					nuevoinc=sprinc;
					}
				
				// N planos seguidos ya consideramos estamos en plano ...
				#define PLANOS_THRESHOLD			3
				if ( (sprinc == (20+PLANOS_THRESHOLD)) || (sprinc == (30+PLANOS_THRESHOLD)) )
					{
					nuevoinc=tipomovhorz;
					dispararfsi=true;
					}
				
				}
			else																// subidita o bajadita (2 o 3)
				{
				
				// (asi reseteamos el contador de planos)
				nuevoinc=tipomovhorz*10;
				
				// si NO estabamos ya
				if ( (sprinc / 10) != tipomovhorz ) dispararfsi=true;
				
				}
			
			if (nuevoinc != g_sprinc)
				{
				fsiinc=nuevoinc;		update=true;
				}
			if (dispararfsi)
				{
				cbkflags |= EVT_FSI;	update=true;
				}
			
		#else
			
			// si la inclinacion ha cambiado ?
			int sprinc=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(objspr,"inc",0));
			if (tipomovhorz == 1)															// viene plano (1)
				{
				
				// estamos bajadita o subidita, contamos los planos
				if (sprinc >= 20)
					{
					sprinc++;
					
					// guardamos en el sprite el tipo de inclinacion actual
					jsvObjectSetChildAndUnLock(objspr,"inc",jsvNewFromInteger(sprinc));
					}
				
				// N planos seguidos ya consideramos estamos en plano ...
				#define PLANOS_THRESHOLD			3
				if ( (sprinc == (20+PLANOS_THRESHOLD)) || (sprinc == (30+PLANOS_THRESHOLD)) )
					{
					
					// guardamos en el sprite el tipo de inclinacion actual
					jsvObjectSetChildAndUnLock(objspr,"inc",jsvNewFromInteger(tipomovhorz));
						
					// creamos el objeto de callback:
					JsVar *tmpobj=jsvNewObject();
					jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSI));
					jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
					jsvObjectSetChild(tmpobj,"args",objspr);
					
					// lo añadimos a la lista
					tmpvar=jsvObjectGetChild(context,"cbk",0);
					jsvArrayPush(tmpvar,tmpobj);
					
					jsvUnLock(tmpobj);					
					
					// DEBUG
					//jsiConsolePrintf("RIC debug : Sprite Inclination = %d\n",tipomovhorz);
					}
				
				}
			else																			// subidita o bajadita (2 o 3)
				{
				
				// guardamos en el sprite el tipo de inclinacion actual (asi reseteamos el contador de planos)
				jsvObjectSetChildAndUnLock(objspr,"inc",jsvNewFromInteger(tipomovhorz*10));
					
				// nos quedamos con las decenas	
				sprinc=sprinc / 10;			// es int perdemos los decimales
				
				// si NO estabamos ya
				if ( sprinc != tipomovhorz )
					{
					
					// creamos el objeto de callback:
					JsVar *tmpobj=jsvNewObject();
					jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FSI));
					jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
					jsvObjectSetChild(tmpobj,"args",objspr);
					
					// lo añadimos a la lista
					tmpvar=jsvObjectGetChild(context,"cbk",0);
					jsvArrayPush(tmpvar,tmpobj);
					
					jsvUnLock(tmpobj);
					
					// DEBUG
					//jsiConsolePrintf("RIC debug : Sprite Inclination = %d\n",tipomovhorz);
					}
				
				}
			
		#endif
		
		}
	#ifndef PXT_VM	
		jsvUnLock(tmpvar);
	#endif
	
	
	/*	Miramos si hay el estudio del Z-order en funcion del cambio de posicion en Y
	*/

	// miramos si esta activada en la scene la Zona Ancha
	#ifdef PXT_VM
		int ysuelominimo=g_ysuelominimo;
	#else
		int ysuelominimo=(int)jsvGetIntegerAndUnLock(jsvObjectGetChild(context,"zaym",0));
	#endif
	if (ysuelominimo > 0)
		{
		
		// calculamos la Y final porque es la que interesa para el Z-order
		int yfant=(ysca - (sph >> 1)) + sph - 1;
		int yfact=(ysc - (sph >> 1)) + sph - 1;
		
		// si ha cambiado 
		if (yfant != yfact)
			{
			
			#ifdef PXT_VM
				
				// en PXT el z y el reorden de la lista de SPRITES los gestiona TS: aqui avisamos que ha cambiado
				cbkflags |= EVT_FZY;	update=true;
				
			#else
			
				// marcamos que la lista de SPRITES quedará desordenada y habra que reordenar antes del siguiente 'DrawAndProcessSceneMakecode'
				precalculatedscene.gsdirty=true;
				
				// lo recalculamos (PENDIENTE creo que es la Y del suelo que hay que calcular !!!!)
				jsvObjectSetChildAndUnLock(objspr,"z",jsvNewFromInteger(  ysc - ysuelominimo ));
				
				// miramos si este SPRITE tiene callback de esta casuistica
				tmpvar=jsvObjectGetChild(objp,"fzy",0);					// OJO es funcion solo del padre ...
				if (jsvIsFunction(tmpvar))
					{
					
					// creamos el objeto de callback:
					JsVar *tmpobj=jsvNewObject();
					jsvObjectSetChildAndUnLock(tmpobj,"k",jsvNewFromInteger((numsprite*1000) + CBK_KIND_FZY));
					jsvObjectSetChildAndUnLock(tmpobj,"acti",tmpvar);
					jsvObjectSetChild(tmpobj,"args",objspr);
					
					// lo añadimos a la lista
					tmpvar=jsvObjectGetChild(context,"cbk",0);
					jsvArrayPush(tmpvar,tmpobj);
					
					jsvUnLock(tmpobj);
					
					}
				jsvUnLock(tmpvar);
				
			#endif
			
			}
		
		}
	
	
	// DEBUG
	if (controljs) { define_INIT_DEBUG_ESTUDIOMOVIMIENTO_OBSTACULOS	}
	#ifdef PXT_VM
		if (contadordbg > 600)
			{
			//std::this_thread::sleep_for( std::chrono::milliseconds(10000) );
			}
	#endif
	
	
	/*	En VM debemos retornar los valores nuevos despues del estudio en un array
	*/
	#ifdef PXT_VM
	
		RefCollection *res = NEW_GC(RefCollection);
		if (update == true)
			{
			
			if (debugconsole) jsiConsolePrintf("Actualizamos coordenadas");
			
			registerGCObj(res);
				res->setLength(4);
				TValue *data = res->getData();
				data[0] = fromInt( memoria_para_fxsc - (spw >> 1) );
				data[1] = fromInt( memoria_para_fysc - (sph >> 1) );
				data[2] = fromInt( cbkflags );
				data[3] = fromInt( fsiinc );
			unregisterGCObj(res);
			
			}
		
		// si no cambian devolveremos un array vacio (length 0)
		return res;
		
	#endif
	
}


/*	OVERLAPS para Arcade Makecode estilo CRC Game Engine
*/
#ifdef PXT_VM

#define CRC_OV_MAX_SPR			MAXSPRITESXSCENE
#define CRC_OV_MAX_KINDS		10
#define CRC_OV_MAX_BUSY			10
#define CRC_OV_MAX_HITS			(CRC_OV_MAX_SPR * (CRC_OV_MAX_SPR - 1) / 2)
#define CRC_OV_NONE				0xFF

struct CRC_OVSPRITE
	{
	int16_t x0,y0,x1,y1;			// rectangulo [x0,x1) x [y0,y1) en pixels de scene
	int16_t w,h;					// tamaño nominal (el que nos pasa TS)
	int32_t id;						// sprite.id (estable aunque se compacte la lista de sprites)
	uint8_t kind;					// CRC_OV_NONE si no participa
	Image_  img;					// solo valido entre SetSprite y Compute
	};

struct CRC_OVBUSYENTRY
	{
	int32_t a,b;					// a < b
	uint8_t n;						// handlers de esta pareja que siguen ejecutandose
	};
	
static CRC_OVSPRITE		g_ovspr[CRC_OV_MAX_SPR];
static int				g_ovn=0;											// slots usados este frame (maximo slot + 1)
static uint32_t			g_ovk[CRC_OV_MAX_KINDS];							// g_ovk[i] bit i2 -> hay handler de overlap entre los kinds de indice i e i2
static int32_t			g_ovkid[CRC_OV_MAX_KINDS];							// valor REAL del kind (en Arcade: 1000, 1001, ... y Projectile=1) de cada indice denso
static int				g_ovnk=0;												// indices densos en uso
static int				g_ovminpixels=0;									// 0 -> solo rectangulo (como Arcade); >0 -> minimo de pixels no transparentes que deben coincidir
static uint16_t			g_ovhits[CRC_OV_MAX_HITS];
static int				g_ovnhits=0;
static CRC_OVBUSYENTRY	g_ovbusy[CRC_OV_MAX_BUSY];
static int				g_ovnbusy=0;

static int ov_kind_index(int32_t kind)
{
	/*	Los kinds de Arcade NO son 0,1,2...: SpriteKind.create() empieza en 1000 (y Projectile vale 1).
		Aqui los traducimos a indices densos 0..CRC_OV_MAX_KINDS-1 para poder usar mascaras de bits.
		Solo se registran los kinds que aparecen en algun onOverlap, asi que CRC_OV_MAX_KINDS limita
		"kinds distintos con overlap", no el valor maximo del kind.
	*/
	
	for (int i=0;i < g_ovnk;i++)
		{
		if (g_ovkid[i] == kind) return i;
		}
		
	// -1 si ese kind no tiene ningun handler de overlap
	return -1;
}

static int ov_kind_add(int32_t kind)
{
	// lo crea si no existe; -1 si la tabla esta llena
	
	int i=ov_kind_index(kind);
	if (i >= 0) return i;
	
	if (g_ovnk >= CRC_OV_MAX_KINDS) return -1;
	
	g_ovkid[g_ovnk]=kind;
	
	return g_ovnk++;
}

static int ov_busy_find(int32_t a,int32_t b)
{
	// debe a < b
	
	for (int i=0;i < g_ovnbusy;i++)
		{
		if (g_ovbusy[i].a == a && g_ovbusy[i].b == b) return i;
		}
		
	return -1;
}

static bool ov_pixels_overlap(const CRC_OVSPRITE &a,const CRC_OVSPRITE &b,int need)
{
	/*	Cuenta pixels NO transparentes que coinciden en el area de interseccion. Corta en cuanto llega a 'need'.
		Formato Image de MakeCode (4bpp): COLUMN-MAJOR, 2 filas por byte, fila par = nibble bajo, fila impar = nibble alto.
		El bytes-por-columna se deduce de pixLength()/width() por si la imagen tiene padding.
		Si algo no cuadra (sin imagen, no es 4bpp, sprite escalado) devolvemos true -> vale el rectangulo.
	*/
	
	if (!a.img || !b.img) return true;
	if (a.img->bpp() != 4 || b.img->bpp() != 4) return true;
	if (a.img->width() != a.w || a.img->height() != a.h) return true;
	if (b.img->width() != b.w || b.img->height() != b.h) return true;
	
	const int xs=(a.x0 > b.x0) ? a.x0 : b.x0;
	const int xe=(a.x1 < b.x1) ? a.x1 : b.x1;								// exclusivo
	const int ys=(a.y0 > b.y0) ? a.y0 : b.y0;
	const int ye=(a.y1 < b.y1) ? a.y1 : b.y1;								// exclusivo
	
	const uint8_t *da=(const uint8_t *)a.img->pix();
	const uint8_t *db=(const uint8_t *)b.img->pix();
	const int sa=(int)(a.img->pixLength() / a.img->width());
	const int sb=(int)(b.img->pixLength() / b.img->width());
	
	int hits=0;
	for (int x=xs;x < xe;x++)
		{
		const uint8_t *ca=da + ((x - a.x0) * sa);
		const uint8_t *cb=db + ((x - b.x0) * sb);
		
		for (int y=ys;y < ye;y++)
			{
			const int ya=y - a.y0;
			if (((ca[ya >> 1] >> ((ya & 1) << 2)) & 0x0F) == 0) continue;	// transparente en A
			
			const int yb=y - b.y0;
			if (((cb[yb >> 1] >> ((yb & 1) << 2)) & 0x0F) != 0)
				{
				
				if (++hits >= need)
					{
					return true;
					}
				
				}
			}
		}
		
	// RIC DEBUG
	jsiConsolePrintf("RIC debug : Sprite colision menor %d pixels\n",need);
	
	return false;
}

//% expose
void CRC_OvReset()
{
	g_ovn=0;
	g_ovnhits=0;
	g_ovnbusy=0;
	g_ovnk=0;
	memset(g_ovk,0,sizeof(g_ovk));
}

//% expose
void CRC_OvClearKinds()
{
	g_ovnk=0;
	memset(g_ovk,0,sizeof(g_ovk));
}

//% expose
void CRC_OvAddKinds(int kind,int otherkind)
{
	const int ia=ov_kind_add(kind);
	const int ib=ov_kind_add(otherkind);
	
	if (ia < 0 || ib < 0)
		{
		DMESG("CRC_OV : demasiados kinds distintos con overlap (maximo %d), se ignora (%d,%d)",CRC_OV_MAX_KINDS,kind,otherkind);
		return;
		}
	
	g_ovk[ia]|=(1u << ib);
	g_ovk[ib]|=(1u << ia);
}

//% expose
void CRC_OvSetMinPixels(int n)
{
	// 0 indica que solo comprobamos el AREA si colisiona, y valor mayor quiere decir como minimo esos poxels colisionan
	g_ovminpixels=(n > 0) ? n : 0;
}

//% expose
void CRC_OvSetSprite(int slot,int id,int kind,int x8,int y8,int w,int h,Image_ img)
{
	if (slot < 0 || slot >= CRC_OV_MAX_SPR)
		{
		static bool avisado=false;
		if (!avisado) { avisado=true; DMESG("CRC_OV : slot %d fuera de rango, maximo %d sprites",slot,CRC_OV_MAX_SPR); }
		return;
		}
	
	// los slots que TS se ha saltado (sprites sin overlap) no participan
	for (int k=g_ovn;k < slot;k++)
		{
		g_ovspr[k].kind=CRC_OV_NONE;
		}
	if (slot >= g_ovn) g_ovn=slot + 1;
	
	CRC_OVSPRITE &s=g_ovspr[slot];
		// el kind se calcula especial
		{
		const int ki=ov_kind_index(kind);									// -1 -> su kind no tiene ningun onOverlap: no participa
		s.kind=(ki >= 0) ? (uint8_t)ki : CRC_OV_NONE;
		}
	s.id=id;
	s.x0=(int16_t)(x8 >> 8);												// Fx8 -> pixel entero (floor), igual que el resto del motor
	s.y0=(int16_t)(y8 >> 8);
	s.w=(int16_t)w;
	s.h=(int16_t)h;
	s.x1=(int16_t)(s.x0 + w);
	s.y1=(int16_t)(s.y0 + h);
	s.img=img;
}

//% expose
int CRC_OvCompute()
{
	g_ovnhits=0;
	const int n=g_ovn;
	
	for (int i=0;i < n - 1;i++)
		{
		const CRC_OVSPRITE &a=g_ovspr[i];
		if (a.kind == CRC_OV_NONE) continue;
		const uint32_t mask=g_ovk[a.kind];
		if (!mask) continue;
		
		for (int j=i + 1;j < n;j++)
			{
			const CRC_OVSPRITE &b=g_ovspr[j];
			if (b.kind == CRC_OV_NONE) continue;
			if (!((mask >> b.kind) & 1)) continue;							// no hay handler entre estos dos kinds
			
			// AABB
			if (a.x0 >= b.x1 || b.x0 >= a.x1 || a.y0 >= b.y1 || b.y0 >= a.y1) continue;
			
			// pareja con handler en ejecucion -> no se vuelve a disparar
			if (g_ovnbusy)
				{
				const int32_t lo=(a.id < b.id) ? a.id : b.id;
				const int32_t hi=(a.id < b.id) ? b.id : a.id;
				if (ov_busy_find(lo,hi) >= 0) continue;
				}
			
			// colision de pixel
			if ( g_ovminpixels > 0 )			// numero pixels que deben colisionar minimo
				{
				if ( !ov_pixels_overlap(a,b,g_ovminpixels) ) continue;
				}
			
			g_ovhits[g_ovnhits++]=(uint16_t)(i | (j << 8));
			}
		}
	
		// siguiente frame: los slots solo se leen si estan por debajo de g_ovn y SetSprite los reescribe (o los marca NONE)

	// dejamos todo limpio para el siguiente frame (y no retenemos punteros a imagenes)
		// no hay que hacer nada , ya que los slots solo se leen si estan por debajo de g_ovn y SetSprite los reescribe (o los marca NONE)
	g_ovn=0;
	
	return g_ovnhits;
}

//% expose
int CRC_OvHit(int k)
{
	return (k >= 0 && k < g_ovnhits) ? (int)g_ovhits[k] : 0;
}

//% expose
void CRC_OvBusy(int ida,int idb,int count)
{
	if (count <= 0) return;
	if (ida > idb) { int t=ida; ida=idb; idb=t; }
	
	int f=ov_busy_find(ida,idb);
	if (f >= 0) { g_ovbusy[f].n=(uint8_t)(g_ovbusy[f].n + count); return; }
	
	if (g_ovnbusy >= CRC_OV_MAX_BUSY)
		{
		DMESG("CRC_OV : tabla de overlaps ocupados llena (%d)",CRC_OV_MAX_BUSY);
		return;
		}
	g_ovbusy[g_ovnbusy].a=ida;
	g_ovbusy[g_ovnbusy].b=idb;
	g_ovbusy[g_ovnbusy].n=(uint8_t)count;
	g_ovnbusy++;
}

//% expose
void CRC_OvDone(int ida,int idb)
{
	if (ida > idb) { int t=ida; ida=idb; idb=t; }
	
	int f=ov_busy_find(ida,idb);
	if (f < 0) return;
	
	if (--g_ovbusy[f].n == 0) g_ovbusy[f]=g_ovbusy[--g_ovnbusy];
}

#endif


#ifdef PXT_VM				// namespace pxt
} // namespace pxt
#endif

