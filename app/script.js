
function StartTest1()
{
	
	var myself=this;
	
	/*	aclaraciones previas de objetos globales de javascript en nuestra APP y HTML DOM del navegador ...
	*/
	var objetorootglobal=null;
	if (typeof window !== 'undefined') { console.log('RIC debug : Estamos en un navegador y existe window'); objetorootglobal=window; }
	if (typeof global !== 'undefined') { console.log('RIC debug : Estamos en Node y existe global'); objetorootglobal=global; }
	if (self) console.log('RIC debug : Existe self');
	if (this) console.log('RIC debug : Existe this');
	if (self === this)
		{
		console.log('RIC debug : self === this, son iguales');
		
		if (self === objetorootglobal) console.log('RIC debug : self === objetorootglobal, son iguales');
		}
	if (typeof objetorootglobal.fetch !== 'undefined') console.log('RIC debug : Existe fetch');
	if (typeof objetorootglobal.WebAssembly !== 'undefined') console.log('RIC debug : Existe WebAssembly');
		// DETECCIÓN ESPECÍFICA DE NW.JS
		if (
			(typeof nw !== 'undefined' && typeof nw.App !== 'undefined')								// 1. Detección estándar y más fiable en NW.js moderno
							||
			(typeof process !== 'undefined' && process.versions && process.versions['nw'])				// 2. Detección a través de process.versions (funciona tanto en context-free como en Node-main)
		   )
			{
			console.log('RIC debug : Estamos en e NW.js (Versión NW:', process.versions['nw'], '| Node:', process.versions.node, ')');
			}
	
	
	function MontarWebASMvirtualFileSystem()
	{
		// Montamos un directorio local que puede acceder Node.js para que se pueda acceder desde C de Emscripten via fopen()
		// Montamos un directorio con ficheros de datos que puede acceder PWA para que se pueda acceder desde C de Emscripten via fopen()
		myself.MakecodeArcadeVMengine.FS.mkdir('/Storage');
		
		if (plataforma == 'winnwjs')
			{
			myself.MakecodeArcadeVMengine.FS.mount( myself.MakecodeArcadeVMengine.FS.filesystems.NODEFS , {root: './vfs/Storage'} , '/Storage' );
				// desde C vemos las lecturas al directorio /Storage como los archivos que hay en local del directorio /vfs/Storage
			}
		
	}
	
	// TFT ST7735 SPI simulada con el sistema WOKWI
	const WIDTH = 128;
	const HEIGHT = 160;
	
	
	/*	Valores de los botones en Arcade Makecode
	*/
	
	// valores del SOURCE de un EVENTO : componente soft o hard que genera el evento
	const PXT_INTERNAL_KEY_UP 		= 2050;
	const PXT_INTERNAL_KEY_DOWN		= 2051;
	const PXT_INTERNAL_USE			= 2049;			// ID asignado internamente para eventos de control de botones o dirección.
	const PXT_INTERNAL_DYNAMIC_A	= 3241;			// IDs dinámicos utilizados por el gestor de dispositivos
	const PXT_INTERNAL_DYNAMIC_B	= 3242;			// IDs dinámicos utilizados por el gestor de dispositivos
	
	// valores del VALUE de un EVENTO
	const CFG_BTN_A     = 5;						// configurado con setupButton(5,4) , 4 = CFG_PIN_BTN_A en dal.d.ts
	const CFG_BTN_B     = 6;						// configurado con setupButton(6,5) , 5 = CFG_PIN_BTN_B en dal.d.ts
	const CFG_BTN_LEFT  = 1;						// configurado con setupButton(1,47) , 47 = CFG_PIN_BTN_LEFT en dal.d.ts
	const CFG_BTN_RIGHT = 3;						// configurado con setupButton(3,48) , 48 = CFG_PIN_BTN_RIGHT en dal.d.ts
	const CFG_BTN_UP    = 2;						// configurado con setupButton(2,49) , 49 = CFG_PIN_BTN_UP en dal.d.ts
	const CFG_BTN_DOWN  = 4;						// configurado con setupButton(4,50) , 50 = CFG_PIN_BTN_DOWN en dal.d.ts
	const CFG_BTN_MENU  = 7;						// configurado con setupButton(7,51) , 51 = CFG_PIN_BTN_MENU en dal.d.ts
	
	const KEYMAP =	{
					'ArrowUp':		CFG_BTN_UP,
					'ArrowDown':	CFG_BTN_DOWN,
					'ArrowLeft':	CFG_BTN_LEFT,
					'ArrowRight':	CFG_BTN_RIGHT,
					'KeyA':			CFG_BTN_A,
					'KeyB':			CFG_BTN_B,
					'Enter':		CFG_BTN_B,
					'Space':		CFG_BTN_A,
					'Escape':		CFG_BTN_MENU,
					};
	
	function installKeyboardHandlers()
	{
	
		window.addEventListener('keydown', function(e)
			{
			
			//console.log('RIC debug : keydown');
				
			var cfgKey = KEYMAP[e.code];
			
			if (cfgKey === undefined) return;
			
			e.preventDefault();						// evita scroll de pagina con flechas/espacio
			
			if (e.repeat) return;					// solo nos interesa el flanco de bajada, no el auto-repeat del SO
			
			if (myself.MakecodeArcadeVMengine)
				{
				myself.MakecodeArcadeVMengine.setKeyState(PXT_INTERNAL_KEY_DOWN,cfgKey);
				}
			
			});
			
		window.addEventListener('keyup', function(e)
			{
				
			//console.log('RIC debug : keyup');
				
			var cfgKey = KEYMAP[e.code];
			
			if (cfgKey === undefined) return;
			
			e.preventDefault();
			
			if (myself.MakecodeArcadeVMengine)
				{
				myself.MakecodeArcadeVMengine.setKeyState(PXT_INTERNAL_KEY_UP,cfgKey);
				}
			
			});
	}	
	
	function setWindow(x0, y0, x1, y1)
	{
		// CASET
		myself.MakecodeArcadeVMengine.wokwi_write_command(0x2A);
		myself.MakecodeArcadeVMengine.wokwi_write_data( new Uint8Array([x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff]) );
		
		 // RASET
		myself.MakecodeArcadeVMengine.wokwi_write_command(0x2B);
		myself.MakecodeArcadeVMengine.wokwi_write_data( new Uint8Array([y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff]) );
		
		// RAMWR
		myself.MakecodeArcadeVMengine.wokwi_write_command(0x2C);
	}
	
	function FinalRun()
	{
		console.log('RIC debug : ');
		console.log('RIC debug : FinalRun()');
		
		initCanvas();
		
		installKeyboardHandlers();
		
		// Inicilizamos el simulador del TFT ST7735 SPI
		// MADCTL 0x00: sin rotación/espejo. Pruébalo también con otros valores si tu montaje físico necesita MX/MY/MV
		myself.MakecodeArcadeVMengine.wokwi_configure(WIDTH, HEIGHT);
		myself.MakecodeArcadeVMengine.wokwi_chip_init();
		myself.MakecodeArcadeVMengine.wokwi_reset();
			// MADCTL: la orientación queda indefinida hasta el primer 0x36
			/*	Valores tipicos:			
					0x00  -> orientación normal
					0x60  -> rotación 90°
					0xC0  -> rotación 180°
					0xA0  -> rotación 270°			
			*/
			myself.MakecodeArcadeVMengine.wokwi_write_command(0x36);
			myself.MakecodeArcadeVMengine.wokwi_write_data(new Uint8Array([0x60]));		// escribimos en MADCTL un 0x60
		// PENDINETE lo que propuso Claude -> run_init_sequence(cfg->madctl);
		
		// este intervalo es el que coje la copia de los pixeles que guarda en el framebuffer y lo vuelva en el navegador
		setInterval(function()
			{
			renderFrameFromWasm();
			},20);
			
		// leemos un fichero de texto con la config especifica para filtrar el DEBUG
		var listafuncionesIDignoradas=require('fs').readFileSync('debug-filter.txt','utf-8').split(',').map(n => Number(n.trim())).filter(Boolean);
		myself.MakecodeArcadeVMengine.SetFunctionIDsIgnore(listafuncionesIDignoradas);
		
		//myself.MakecodeArcadeVMengine.setGCSnapshotInterval(2);
		
		myself.MakecodeArcadeVMengine.LlamarMainOriginal();
		
		console.log('RIC debug :     hemos regresado a Javascript por 1a. vez !!');
	}
	
	// Variables para el renderizado
	var ctx = null;
	var canvasImageData = null;
	var ctxtft = null;
	var canvastftImageData = null;
	
	function initCanvas()
	{

		var canvas = document.getElementById('screenCanvas');
		
		if (canvas)
			{
			ctx = canvas.getContext('2d');
			// Creamos una estructura ImageData de 160x120 (cada píxel usa 4 bytes: RGBA)
			canvasImageData = ctx.createImageData(160, 120);
			}
			
		canvas = document.getElementById('TFTCanvas');

		if (canvas)
			{
			ctxtft = canvas.getContext('2d');
			canvastftImageData = ctxtft.createImageData(128, 160);
			}
		
	}
	
	function renderFrameFromWasm()
	{
		if (!ctx || !myself.MakecodeArcadeVMengine || typeof myself.MakecodeArcadeVMengine.get_screen_buffer_pointer !== 'function')
			{
			return;
			}
		
		var bufferArray = myself.MakecodeArcadeVMengine.get_screen_buffer_pointer();
		
		canvasImageData.data.set(bufferArray);
		
		ctx.putImageData(canvasImageData,0,0);

		if (!ctxtft || !myself.MakecodeArcadeVMengine || typeof myself.MakecodeArcadeVMengine.get_screen_buffer_pointer !== 'function')
			{
			return;
			}
		
		const w = myself.MakecodeArcadeVMengine.wokwi_get_width();
		const h = myself.MakecodeArcadeVMengine.wokwi_get_height();
		
		const rgba = myself.MakecodeArcadeVMengine.wokwi_get_framebuffer();
		
		// con este tipo de constructor no se copian los datos de nuevo se referencian ...
		const imgData = new ImageData( new Uint8ClampedArray(rgba.buffer,rgba.byteOffset,rgba.byteLength),w,h);
		
		ctxtft.putImageData(imgData, 0, 0);
	}
	
	// Cargamos el codigo WebASM (.WASM)
	if (plataforma == 'winnwjs')
		{
		
		// solo atendemos la primera llamada... las demas ignoramos de momento ...
		if (typeof myself.MakecodeArcadeVMengine !== 'undefined')
			{
			// solo atendemos la primera llamada... las demas ignoramos de momento ...
			}
		else
			{
				
			// Cargar el módulo generado por el compilador de Emscripten (.js y .wasm)
			myself.MakecodeArcadeVMengine=require('../MakecodeArcadeVMengine');
			
			myself.MakecodeArcadeVMengine.onAbort = function(reason)
				{
				console.log('RIC debug : WebAssembly ABORT reason -> ' + reason);
				
				process.exit(1);
				};			
			
			// Esperar a que el runtime esté listo antes de hacer llamadas a las funciones exportadas
			myself.MakecodeArcadeVMengine.onRuntimeInitialized = function()							
				{
				
				console.log('RIC debug : WebAssembly cargado y listo en Node.js');
				
				MontarWebASMvirtualFileSystem();
				
				// ejecutamos un poco mas tarde para que los mensajes de NW.js en la consola se estabilicen
				setTimeout(FinalRun,3000);
				
				};
			
			}
		
		}
	else if (plataforma == 'pwa')
		{
		// PENDIENTE
		}
	
}

