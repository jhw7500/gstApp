
#TOP_DIR = $(shell pwd)
OUTPUT = bin

#SOURCE += basic-tutorial-15
#SOURCE=rtspApp
SOURCE+= timeApp
#SOURCE+= srcApp
#SOURCE+= appApp
#SOURCE+= ghostApp
#SOURCE+= binApp
#SOURCE+= fakeApp

#SOURCE+= customApp
#SOURCE+= jhwApp
#SOURCE += testApp
#SOURCE=*

#ROOTFS += /opt/desktop/build-desktop/tmp/work/imx8mpevk-fsl-linux/imx-image-desktop/20.04.2-r0/rootfs
#SYSROOT += /shared/fsl-imx-xwayland/5.10-hardknott/sysroots/cortexa53-crypto-poky-linux
#PKG_CONFIG_PATH = /shared/fsl-imx-xwayland/5.10-hardknott/sysroots/cortexa53-crypto-poky-linux/usr/lib/pkgconfig/
#CROSS_COMPILE += /shared/fsl-imx-xwayland/5.10-hardknott/sysroots/x86_64-pokysdk-linux/usr/bin/aarch64-poky-linux/aarch64-poky-linux-
#CFLAGS += -W -Wall
#CFLAGS += -Wno-unused-parameter
#CFLAGS += -Wno-unused-variable
#CFLAGS += `pkg-config --cflags --libs gstreamer-1.0`
#CFLAGS += --sysroot=$(SYSROOT)
#CFLAGS += -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -lglib-2.0
#CFLAGS += -I/opt/desktop/build-desktop/tmp/sysroots-components/cortexa53-crypto/glib-2.0/usr/include/glib-2.0/
#CFLAGS += -I/opt/desktop/build-desktop/tmp/sysroots-components/cortexa53-crypto/glib-2.0/usr/lib/glib-2.0/include/
#CFLAGS += -I/shared/fsl-imx-xwayland/5.10-hardknott/sysroots/cortexa53-crypto-poky-linux/usr/include/gstreamer-1.0
#CFLAGS += -I/shared/fsl-imx-xwayland/5.10-hardknott/sysroots/cortexa53-crypto-poky-linux/usr/include/gstreamer-1.0/gst/audio/
#CFLAGS += -I/opt/desktop/build-desktop/tmp/sysroots-components/cortexa53-crypto-mx8mp/gstreamer1.0-plugins-base/usr/include/gstreamer-1.0
#IFLAGS += -I$(ROOTFS)/usr/include
#IDIR += $(SYSROOT)/usr/include #-I$(ROOTFS)/usr/include
LIBS += gstreamer-1.0 gstreamer-rtsp-server-1.0 glib-2.0 gstreamer-plugins-base-1.0 gstreamer-app-1.0 check
#LIBS += gstreamer-audio-1.0
#LIBS += gstreamer-pbutils-1.0
LDFLAGS+="-Wl,--copy-dt-needed-entries"
LDFLAGS+=$(shell pkg-config --libs $(LIBS))
ALL_LDFLAGS=$(LDFLAGS)

CFLAGS+=-Wall
CFLAGS+=$(shell pkg-config --cflags $(LIBS))
ALL_CFLAGS=-I$(IDIR) $(CFLAGS)

ALLFLAGS=$(ALL_CFLAGS) $(ALL_LDFLAGS)


$(SOURCE) : $(SOURCE).c
	$(CC) -o $@ $^ $(ALLFLAGS) -lpthread
	mv $@ $(OUTPUT)

clean :
	rm -f $(OUTPUT)/*

