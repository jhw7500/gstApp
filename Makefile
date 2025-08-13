
#TOP_DIR = $(shell pwd)
OUTPUT = bin

#SOURCE=main.cpp util.cpp util.h videoBin.cpp videoBin.h recordBin.cpp recordBin.h audioBin.cpp audioBin.h muxSinkBin.cpp muxSinkBin.h rtspServerBin.cpp rtspServerBin.h captureBin.cpp captureBin.h

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
LIBS += gstreamer-1.0 gstreamer-rtsp-server-1.0 glib-2.0 gstreamer-plugins-base-1.0 gstreamer-app-1.0 check json-c openssl gstreamer-video-1.0
#LIBS += gstreamer-audio-1.0
#LIBS += gstreamer-pbutils-1.0
LDFLAGS+="-Wl,--copy-dt-needed-entries"
LDFLAGS+=$(shell pkg-config --libs $(LIBS))
LDFLAGS+=-L/opt/desktop/gitlab/gst-jhw/gstapp/gstapp/app/rnnoise/lib -lrnnoise
ALL_LDFLAGS=$(LDFLAGS)

CFLAGS+=-Wall
CFLAGS+=$(shell pkg-config --cflags $(LIBS))
CFLAGS+=-I/opt/desktop/gitlab/gst-jhw/gstapp/gstapp/app/rnnoise/include
ALL_CFLAGS=-I$(IDIR) $(CFLAGS)

ALLFLAGS=$(ALL_CFLAGS) $(ALL_LDFLAGS) -lturbojpeg
#OBJS = videoBin.o recordBin.o muxSinkBin.o rtspServerBin.o audioBin.o muxBin.o
OBJS = videoBin.o recordBin.o muxSinkBin.o rtspServerBin.o testBin.o captureBin.o util.o parser.o aes.o tcpServer.o audioBin.o ipc.o encoderBin.o

gstApp : $(OBJS) main.cpp
	$(CXX) -o $@ $^ $(ALLFLAGS)
	mv $@ $(OUTPUT)
	mv *.o obj

videoBin.o : videoBin.cpp videoBin.h
	$(CXX) $(ALLFLAGS) -c videoBin.cpp

recordBin.o : recordBin.cpp recordBin.h
	$(CXX) $(ALLFLAGS) -c recordBin.cpp

muxSinkBin.o : muxSinkBin.cpp muxSinkBin.h
	$(CXX) $(ALLFLAGS) -c muxSinkBin.cpp

rtspServerBin.o : rtspServerBin.cpp rtspServerBin.h
	$(CXX) $(ALLFLAGS) -c rtspServerBin.cpp

#muxBin.o : muxBin.cpp muxBin.h
#	$(CXX) $(ALLFLAGS) -c muxBin.cpp

testBin.o : testBin.cpp testBin.h
	$(CXX) $(ALLFLAGS) -c testBin.cpp

captureBin.o : captureBin.cpp captureBin.h
	$(CXX) $(ALLFLAGS) -c captureBin.cpp

util.o : util.cpp util.h
	$(CXX) $(ALLFLAGS) -c util.cpp

parser.o : parser.cpp parser.h
	$(CXX) $(ALLFLAGS) -c parser.cpp

aes.o : aes.cpp aes.h
	$(CXX) $(ALLFLAGS) -c aes.cpp

tcpServer.o : tcpServer.cpp tcpServer.h
	$(CXX) $(ALLFLAGS) -c tcpServer.cpp

audioBin.o : audioBin.cpp audioBin.h
	$(CXX) $(ALLFLAGS) -c audioBin.cpp

ipc.o : ipc.cpp ipc.h
	$(CXX) $(ALLFLAGS) -c ipc.cpp

encoderBin.o : encoderBin.cpp encoderBin.h
	$(CXX) $(ALLFLAGS) -c encoderBin.cpp

#json_c.o : json_c.cpp json_c.h
#	$(CXX) $(ALLFLAGS) -c json_c.cpp

clean :
	rm -f $(OUTPUT)/*

