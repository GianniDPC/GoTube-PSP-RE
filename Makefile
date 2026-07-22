TARGET = GoTube
.DEFAULT_GOAL := all

OBJS = \
	vendor/intrafont-0.22/intraFont.o vendor/intrafont-0.31/libccc.o \
	src/boot/main.o src/boot/callbacks.o src/boot/input.o \
	src/bridge/native_registry.o \
	src/gui/gui.o src/gui/menu.o src/gui/thumbnail.o src/gui/osk.o src/gui/netconf.o src/gui/splash.o \
	src/natives/http.o \
	src/media/audio.o src/media/player.o src/media/comments.o src/media/save.o src/media/local.o src/media/modern.o src/net/net.o src/net/curl_http.o \
	src/video/video.o src/video/dvemgr.o

INCDIR = vendor/intrafont-0.22 vendor/intrafont-0.31 vendor/faad include third_party/ffmpeg \
	third_party/ffmpeg/libavcodec third_party/ffmpeg/libavformat \
	third_party/ffmpeg/libavutil
CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -DXP_UNIX
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR = third_party/ffmpeg/libavformat third_party/ffmpeg/libavcodec \
	third_party/ffmpeg/libavutil vendor/faad/lib
LIBS = -lavformat -lavcodec -lfaad -lavutil -ljpeg -lcurl \
	-lmbedtls -lmbedx509 -lmbedcrypto -lz -lm -lc -lcglue -lpthreadglue -lpthread -lpsprtc \
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
	rm -f release/GoTube/cfg.js release/GoTube/site.js \
		release/GoTube/update.js release/GoTube/site/ext.js \
		release/GoTube/site/YouTube.js
	pack-pbp release/GoTube/EBOOT.PBP PARAM.SFO assets/ICON0.PNG \
		NULL NULL NULL assets/SND0.AT3 GoTube.prx NULL
	cp runtime/cacert.pem release/GoTube/cacert.pem
	cp runtime/dvemgr.prx release/GoTube/dvemgr.prx
	cp runtime/mediaengine.prx release/GoTube/mediaengine.prx
