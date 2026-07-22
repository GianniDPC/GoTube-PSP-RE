TARGET = GoTube
.DEFAULT_GOAL := all

OBJS = \
	vendor/intrafont-0.22/intraFont.o vendor/intrafont-0.31/libccc.o \
	src/boot/main.o src/boot/callbacks.o src/boot/input.o src/boot/trace.o \
	src/bridge/register.o src/bridge/callgate.o src/js/eval.o \
	src/gui/gui.o src/gui/menu.o src/gui/thumbnail.o src/gui/osk.o src/gui/netconf.o src/gui/splash.o \
	src/natives/http.o src/natives/psptube.o \
	src/media/audio.o src/media/player.o src/media/comments.o src/media/save.o src/media/local.o src/net/net.o \
	src/video/video.o src/video/dvemgr.o

INCDIR = vendor/intrafont-0.22 vendor/intrafont-0.31 vendor/faad include lib/include third_party/ffmpeg \
	third_party/ffmpeg/libavcodec third_party/ffmpeg/libavformat \
	third_party/ffmpeg/libavutil
CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -DXP_UNIX \
	$(GT_TEST_CFLAGS) $(if $(GT_TEST_CFLAGS),-DGT_ENABLE_TRACE,)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR = lib third_party/ffmpeg/libavformat third_party/ffmpeg/libavcodec \
	third_party/ffmpeg/libavutil vendor/faad/lib
LIBS = -lspidermonkey -lavformat -lavcodec -lfaad -lavutil -ljpeg -lz -lm -lc -lcglue \
	-lpspgum -lpspgu -lpspdisplay -lpspctrl -lpsppower \
	-lpsputility -lpsphttp -lpspssl -lpspnet -lpspnet_inet \
	-lpspnet_apctl -lpspnet_resolver -lpspwlan -lpsphprm -lpspkubridge -lpspsdk -lpspaudio

BUILD_PRX = 1
PSP_FW_VERSION = 500
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = GoTube 1.2 RE
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_SND0 = assets/SND0.AT3

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: historical-package
historical-package: EBOOT.PBP
	mkdir -p release/GoTube/site
	pack-pbp release/GoTube/EBOOT.PBP PARAM.SFO assets/ICON0.PNG \
		NULL NULL NULL assets/SND0.AT3 GoTube.prx NULL
	cp runtime/cfg.js release/GoTube/cfg.js
	cp runtime/site.js release/GoTube/site.js
	cp runtime/site/ext.js release/GoTube/site/ext.js
	cp runtime/site/YouTube.js release/GoTube/site/YouTube.js
	cp runtime/update.js release/GoTube/update.js
	cp runtime/dvemgr.prx release/GoTube/dvemgr.prx
	cp runtime/mediaengine.prx release/GoTube/mediaengine.prx
