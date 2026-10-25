idf_component_register(
						SRCS
							# BASE
							"base/pxt.cpp"
							"base/gc.cpp"
							"base/core.cpp"
							"base/advmath.cpp"
							"base/trig.cpp"
							"base/buffer.cpp"
							"base/loops.cpp"
							"base/control.cpp"
							"base/controlgc.cpp"
							
							# EMSCRIPTEN SNAPSHOT DE LA MEMORIA
							#"base/heapsnapshot.cpp"
							
							# CORE ESP32
							"core---esp32/scheduler.cpp"
							"core---esp32/config.cpp"
							"core---esp32/target.cpp"
							"core---esp32/dmesg.cpp"
							"core---esp32/usb.cpp"
							"core---esp32/platform.cpp"
							"core---esp32/pins.cpp"
							"core---esp32/pintarget.cpp"
							"core---esp32/pinsDigital.cpp"
							"core---esp32/control.cpp"
							"core---esp32/codalemu.cpp"
							"core---esp32/keys.cpp"
							"core---esp32/vm.cpp"
							"core---esp32/verify.cpp"
							"core---esp32/vmload.cpp"
							"core---esp32/worker.cpp"
							"core---esp32/crypto.cpp"
							"core---esp32/vmcache.cpp"
							
							# SETTINGS
							"settings---esp32/settings.cpp"
							
							# GAME
							"game/controllerbuttons.cpp"
							"game/keymap.cpp"
							"game/multiplayer.cpp"
							"game/systemKeyboard.cpp"
							
							# MIXER
							"mixer---ext/melody.cpp"
							"mixer---ext/sound.cpp"
							
							# SCREEN
							"screen---ext/image.cpp"
							"screen---ext/screen.cpp"
							"screen---ext/crcgameengine.cpp"
							#"screen---ext/fastlz.c"
							
							# ARCADE MAKECODE MAIN
							"main.cpp"
							"pointers.cpp"
							
						INCLUDE_DIRS							
							"."
						
						PRIV_REQUIRES
							# aqui no ponemos nada ya que nos funciona asi bien ...
							# hay un componente 'mdns' que se descarga de expressif a partir de un YML -> idf_component.yml
					  )

target_compile_options(${COMPONENT_TARGET} PUBLIC -fpermissive)

# si quiero debugear bien, que me encuentre con mas precision el numero de linea del Backtrace quitar optimizaciones -O0
#target_compile_options(${COMPONENT_TARGET} PUBLIC -O0)
#target_compile_options(${COMPONENT_TARGET} PUBLIC -Og)
target_compile_options(${COMPONENT_TARGET} PUBLIC -O2)

# flags del compilador que heredo de la version ESP-IDF 3.3.6
#target_compile_options(${COMPONENT_TARGET} PUBLIC -Og -fno-strict-aliasing -ffunction-sections -fdata-sections -fstrict-volatile-bitfields -fgnu89-inline -mlongcalls -nostdlib -MMD -MP -std=gnu99 -mfix-esp32-psram-cache-issue)

# esta configuracion en el ESP-IDF por defecto convierte estos warnings en errores, analizarlos y corregir el codigo mejor
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=misleading-indentation)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-volatile)

# estos defines los quite para que la salida en consola sea mas breve para identificar errores, pero luego comentarlos de nuevo
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-cast-function-type)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

