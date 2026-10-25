
/*	Como funciona:

	para compilar para app NW.js con Node.js
	
		--no-entry					asi no busca y ejecuta automaticamente el main()
		
		-O0							sin optimizaciones compilacion rapida
		
		-s WASM=1					opcion por defecto, genera .wasm y un .js
		
		-s ALLOW_MEMORY_GROWTH=1	permite ajustar la memoria en tiempo de ejecucion
		
		-s ALLOW_MEMORY_GROWTH=0	No dejamos que use mas memoria que la que definimos, asi si se pasa peta !!!
		-s INITIAL_MEMORY=<bytes>	Indicamos cuanta tendra, va en multiplos de 64Kb
										5 x 64Kb = 327.680 bytes
		-s MALLOC="emmalloc"		una gestion de memoria mas light
		
		-std=c++11
			o
		-std=c++20 					SI queremos usar la funcion .starts_with de std::string
		
		-lembind					activa el sistema de linkar funciones del C++ con javascript cliente
		
		-lnodefs.js					Para tener acceso a los archivos desde C++ y javascript
		-s FORCE_FILESYSTEM=1
		-s EXPORTED_RUNTIME_METHODS=['FS'] 
		
		-s ENVIRONMENT=node			es para usar en NODE.js . NW.js
			O
		-s ENVIRONMENT=web,worker	para Web
		-s MODULARIZE=1
		-s EXPORT_ES6=1
		
		
		Estas opciones son para tracear fallos de C++`dentro de EMSCRIPTEN ...
		
		'-fexceptions',
			con esta conseguimos que se hagan los bloques try ... catch ...
			y ademas si se llama a un puntero incorrecto, no lo hace y continua el flujo, asi todo segue
		
		'-g4',
		'-s ASSERTIONS=20',
		'-s DEMANGLE_SUPPORT=1'
		
*/

const { execSync } = require('child_process');

const fs = require('fs');

const path = require('path');

// 1. Leer el archivo listafiles.txt
const listFile = 'listafiles.txt';
if (!fs.existsSync(listFile))
	{
    console.error(`Error: No se encuentra el archivo ${listFile}`);
    process.exit(1);
	}

const sources = fs.readFileSync(listFile, 'utf8').split(/\r?\n/).map(line => line.trim()).filter(Boolean);


// 2. Definir banderas e includes
const includes = [
    '-I.',
    '-Idockerespidf/main',
    '-Idockerespidf/main/base',
    '-Idockerespidf/main/core---esp32',
    '-Idockerespidf/main/settings---esp32',
	'-Idockerespidf/main/screen',
	'-Idockerespidf/st7735sim'
];

const flags =	[
				'--no-entry',
				'-O0',
				'-std=c++11',
				'-s WASM=1',
				//'-s ALLOW_MEMORY_GROWTH=0','-s MALLOC=emmalloc','-s INITIAL_MEMORY=655360',
				'-s ALLOW_MEMORY_GROWTH=1','-s MALLOC=emmalloc',
				'-lembind',		
				'-lnodefs.js',
				'-s FORCE_FILESYSTEM=1',
				'-s EXPORTED_RUNTIME_METHODS=FS',
				'-s ENVIRONMENT=node',
				'-s EXIT_RUNTIME=0',
				
				'-fexceptions',
				
					// no se bien si lo necesito ya
				'-sASYNCIFY',
				
					// esto es para que funcionen los PThreads (estandard POSIX) en EMscripten, pero no los necesito en este caso ...
				//'-s USE_PTHREADS=1',
				
					// este flag lo tuve que poner porque si no al coger punteros y hacerles un cast de tipo, EMSCRIPTEN es muy extricto y daba 'Runtime Error?
				'-s EMULATE_FUNCTION_POINTER_CASTS=1'				
				];

// 3. Construir el comando completo
const command =	[
				//'emcc',					// para C clasico
				'em++',					// para C++
				...flags,
				...includes,
				...sources,
				'-o MakecodeArcadeVMengine.js'
				].join(' ');

console.log('Compilando con Emscripten...');
try {
    execSync(command, { stdio: 'inherit' });
    console.log('¡Compilación completada con éxito! (output.js / output.wasm)');
} catch (error) {
    console.error('Error durante la compilación.');
}

