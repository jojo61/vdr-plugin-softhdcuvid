#
# Makefile for a Video Disk Recorder plugin
# 
# $Id: 2a41981a57e5e83036463c6a08c84b86ed9d2be3 $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.


### Configuration (edit this for your needs)
#  comment out if not needed

# what kind of decoder do we make - 
# if VAAPI is enabled the pluginname is softhdvaapi
# if CUVID is enabled the pluginname is softhdcuvid
# if DRM   is enabled the pluginname is softhddrm
VAAPI ?= 0
CUVID ?= 0

# if you enable DRM then the plugin will only run without X server
# only valid for VAAPI 
DRM ?= 0


# use libplacebo - 
# available for all decoders but for DRM you need LIBPLACEBO_GL  
LIBPLACEBO ?= 1
LIBPLACEBO_GL ?= 0

# use YADIF deint - only available with cuvid
#YADIF=1

# use gamma correction
#GAMMA ?= 0



CONFIG :=  -DDEBUG 		# remove # to enable debug output






#--------------------- no more config needed past this point--------------------------------

# sanitize selections --------
ifneq "$(MAKECMDGOALS)" "clean"
ifneq "$(MAKECMDGOALS)" "indent"

ifeq ($(VAAPI),0)
ifeq ($(CUVID),0)
ifeq ($(DRM),0)
$(error Please define a plugin in the Makefile)
exit 1;
endif
endif
endif


ifeq ($(CUVID),1)
ifeq ($(DRM),1)
$(error Missmatch in Plugin selection)
exit 1;
endif
endif


ifeq ($(CUVID),1)
ifeq ($(VAAPI),1)
$(error Missmatch in Plugin selection)
exit 1;
endif
endif

endif # MAKECMDGOALS!=indent
endif # MAKECMDGOALS!=clean
#--------------------------



PLUGIN = softhdcuvid

# support OPENGLOSD always needed
OPENGLOSD=1

# support alsa audio output module
ALSA ?= $(shell pkg-config --exists alsa && echo 1)
    # support OSS audio output module
OSS ?= 1

# use DMPS
SCREENSAVER=1

OPENGL=1

# use ffmpeg libswresample
SWRESAMPLE ?= $(shell pkg-config --exists libswresample && echo 1)
SWRESAMPLE = 1

# use libav libavresample
#ifneq ($(SWRESAMPLE),1)
#AVRESAMPLE ?= $(shell pkg-config --exists libavresample && echo 1#)
#AVRESAMPLE = 1
#endif

CONFIG += -DHAVE_GL			# needed for mpv libs
#CONFIG += -DSTILL_DEBUG=2		# still picture debug verbose level
CONFIG += -DAV_INFO -DAV_INFO_TIME=3000	# info/debug a/v sync
#CONFIG += -DHAVE_PTHREAD_NAME		# supports new pthread_setname_np
#CONFIG += -DNO_TS_AUDIO		# disable ts audio parser
#CONFIG += -DUSE_TS_VIDEO		# build new ts video parser
CONFIG += -DUSE_MPEG_COMPLETE		# support only complete mpeg packets
CONFIG += -DH264_EOS_TRICKSPEED		# insert seq end packets for trickspeed
#CONDIF += -DDUMP_TRICKSPEED		# dump trickspeed packets
#CONFIG += -DUSE_BITMAP			# VDPAU, use bitmap surface for OSD
CONFIG += -DUSE_VDR_SPU			# use VDR SPU decoder.
#CONFIG += -DUSE_SOFTLIMIT		# (tobe removed) limit the buffer fill

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*const VERSION *=' softhdcuvid.cpp | awk '{ print $$7 }' | sed -e 's/[";]//g')
GIT_REV = $(shell git describe --always 2>/dev/null)
### The name of the distribution archive:

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../.." pkg-config --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS	= $(call PKGCFG,cflags) 
export CXXFLAGS = $(call PKGCFG,cxxflags)

ifeq ($(CFLAGS),)
$(warning CFLAGS not set)
endif
ifeq ($(CXXFLAGS),)
$(warning CXXFLAGS not set)
endif

### The version number of VDR's plugin API:

APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)



### Parse softhddevice config

ifeq ($(ALSA),1)
CONFIG += -DUSE_ALSA
_CFLAGS += $(shell pkg-config --cflags alsa)
LIBS += $(shell pkg-config --libs alsa)
endif

ifeq ($(OSS),1)
CONFIG += -DUSE_OSS
endif

ifeq ($(OPENGL),1)
#_CFLAGS += $(shell pkg-config --cflags libva-glx)
#LIBS += $(shell pkg-config --libs libva-glx)
endif

ifeq ($(OPENGLOSD),1)
CONFIG += -DUSE_OPENGLOSD
endif

ifeq ($(OPENGL),1)
CONFIG += -DUSE_GLX
_CFLAGS += $(shell pkg-config --cflags gl glu glew)
#LIBS += $(shell pkg-config --libs glu  glew)
_CFLAGS += $(shell pkg-config --cflags freetype2)
LIBS   += $(shell pkg-config --libs freetype2)
endif

ifeq ($(VAAPI),1)
CONFIG += -DVAAPI 
#LIBPLACEBO=1
PLUGIN = softhdvaapi
endif

ifeq ($(LIBPLACEBO_GL),1)
CONFIG += -DPLACEBO_GL -DPLACEBO
LIBS += -lepoxy  
LIBS += -lplacebo 
else
LIBS += -lEGL  
endif

ifeq ($(LIBPLACEBO),1)
CONFIG += -DPLACEBO
LIBS += -lEGL  
LIBS += -lplacebo 
endif

ifeq ($(DRM),1)
PLUGIN = softhddrm
CONFIG += -DUSE_DRM -DVAAPI
_CFLAGS += $(shell pkg-config --cflags libdrm)
LIBS += -lgbm -ldrm -lEGL
endif


ifeq ($(CUVID),1)
#CONFIG += -DUSE_PIP			# PIP support
CONFIG += -DCUVID			# enable CUVID decoder
LIBS += -lEGL -lGL 
ifeq ($(YADIF),1)
CONFIG += -DYADIF			# Yadif only with CUVID
endif
endif

ifeq ($(GAMMA),1)
CONFIG  += -DGAMMA
endif


ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so


#
# Test that libswresample is available 
#
#ifneq (exists, $(shell pkg-config libswresample && echo exists))
#  $(warning ******************************************************************)
#  $(warning 'libswresample' not found!)
#  $(error ******************************************************************)
#endif

#
# Test and set config for libavutil 
#
ifneq (exists, $(shell pkg-config libavutil && echo exists))
  $(warning ******************************************************************)
  $(warning 'libavutil' not found!)
  $(error ******************************************************************)
endif
_CFLAGS += $(shell pkg-config --cflags libavutil)
LIBS += $(shell pkg-config --libs libavutil)

#
# Test and set config for libswscale 
#
ifneq (exists, $(shell pkg-config libswscale && echo exists))
  $(warning ******************************************************************)
  $(warning 'libswscale' not found!)
  $(error ******************************************************************)
endif
_CFLAGS += $(shell pkg-config --cflags libswscale)
LIBS += $(shell pkg-config --libs libswscale)

#
# Test and set config for libavcodec
#
ifneq (exists, $(shell pkg-config libavcodec && echo exists))
  $(warning ******************************************************************)
  $(warning 'libavcodec' not found!)
  $(error ******************************************************************)
endif
_CFLAGS += $(shell pkg-config --cflags libavcodec)
LIBS += $(shell pkg-config --libs libavcodec libavfilter)


ifeq ($(SCREENSAVER),1)
CONFIG += -DUSE_SCREENSAVER
_CFLAGS += $(shell pkg-config --cflags xcb-screensaver xcb-dpms)
LIBS += $(shell pkg-config --libs xcb-screensaver xcb-dpms)
endif
ifeq ($(SWRESAMPLE),1)
CONFIG += -DUSE_SWRESAMPLE
_CFLAGS += $(shell pkg-config --cflags libswresample)
LIBS += $(shell pkg-config --libs libswresample)
endif
ifeq ($(AVRESAMPLE),1)
CONFIG += -DUSE_AVRESAMPLE
_CFLAGS += $(shell pkg-config --cflags libavresample)
LIBS += $(shell pkg-config --libs libavresample)
endif

#_CFLAGS += $(shell pkg-config --cflags libavcodec x11 x11-xcb xcb xcb-icccm)
#LIBS += -lrt $(shell pkg-config --libs libavcodec x11 x11-xcb xcb xcb-icccm)
_CFLAGS += $(shell pkg-config --cflags  x11 x11-xcb xcb xcb-icccm)
LIBS += -lrt $(shell pkg-config --libs  x11 x11-xcb xcb xcb-icccm)

_CFLAGS += -I./opengl -I./

LIBS += -L/usr/lib64


ifeq ($(CUVID),1)
LIBS +=  -lcuda  -lnvcuvid  
endif

LIBS += -lGLEW -lGLU  -ldl -lglut 
### Includes and Defines (add further entries here):

INCLUDES +=

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -D_GNU_SOURCE $(CONFIG) \
	$(if $(GIT_REV), -DGIT_REV='"$(GIT_REV)"')

### Make it standard

override CXXFLAGS += $(_CFLAGS) $(DEFINES) $(INCLUDES) \
    -g  -W -Wextra -Winit-self -Werror=overloaded-virtual  -Wno-unused-parameter 
override CFLAGS	  += $(_CFLAGS) $(DEFINES) $(INCLUDES) \
    -g -W  -Wextra -Winit-self  -std=gnu99


### The object files (add further files here):

OBJS = softhdcuvid.o softhddev.o video.o audio.o codec.o ringbuffer.o openglosd.o 
ifeq ($(GAMMA),1)
OBJS += colorramp.o 
ifeq ($(DRM),1)
OBJS += gamma-drm.o
else
OBJS += gamma-vidmode.o
endif
endif

SRCS = $(wildcard $(OBJS:.o=.c)) *.cpp

### The main target:

all: $(SOFILE) i18n

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(CXXFLAGS) $(SRCS) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR	  = po
I18Npo	  = $(wildcard $(PODIR)/*.po)
I18Nmo	  = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot	  = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(SRCS)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP \
	-k_ -k_N --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) \
	--msgid-bugs-address='<see README>' -o $@ `ls $^`

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

### Targets:

$(OBJS): Makefile


$(SOFILE): $(OBJS) shaders.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared  $(OBJS) $(LIBS)  -o $@

install-lib: $(SOFILE)
	install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~

## Private Targets:

HDRS=	$(wildcard *.h)

indent:
	for i in $(SRCS) $(HDRS); do \
		indent $$i; \
		unexpand -a $$i | sed -e s/constconst/const/ > $$i.up; \
		mv $$i.up $$i; \
	done

video_test: video.c Makefile
	$(CC) -DVIDEO_TEST -DVERSION='"$(VERSION)"' $(CFLAGS) $(LDFLAGS) $< \
	$(LIBS) -o $@
