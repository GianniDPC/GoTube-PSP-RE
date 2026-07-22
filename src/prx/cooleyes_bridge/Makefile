TARGET = cooleyesBridge
OBJS = main.o sceMeCore_driver.o
CFLAGS = -O2 -G0 -Wall
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
PSP_FW_VERSION = 500
LIBS = -lpspkernel
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
