PXT_C = 
PXT_CPP = main/base/pxt.cpp main/base/gc.cpp main/base/core.cpp main/base/advmath.cpp main/base/trig.cpp main/base/buffer.cpp main/base/loops.cpp main/base/control.cpp main/base/controlgc.cpp main/core---esp32/scheduler.cpp main/core---esp32/config.cpp main/core---esp32/target.cpp main/core---esp32/dmesg.cpp main/core---esp32/usb.cpp main/core---esp32/platform.cpp main/core---esp32/pins.cpp main/core---esp32/pintarget.cpp main/core---esp32/pinsDigital.cpp main/core---esp32/control.cpp main/core---esp32/codalemu.cpp main/core---esp32/keys.cpp main/core---esp32/vm.cpp main/core---esp32/verify.cpp main/core---esp32/vmload.cpp main/core---esp32/worker.cpp main/core---esp32/crypto.cpp main/settings---esp32/settings.cpp main/pointers.cpp main/main.cpp
PXT_S = 
PXT_HEADERS = main/configkeys.h main/pxtbase.h main/dmesg.h main/pxt.h main/platform.h main/pxtcore.h main/uf2hid.h main/pins.h main/pintarget.h main/platform_includes.h main/vm.h main/pxtconfig.h
PXT_SOURCES := $(PXT_C) $(PXT_S) $(PXT_CPP)
PXT_OBJS := $(addprefix bld/, $(PXT_C:.c=.o) $(PXT_S:.s=.o) $(PXT_CPP:.cpp=.o))
