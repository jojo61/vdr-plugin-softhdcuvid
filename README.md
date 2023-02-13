@file README.txt		@brief A software HD output device for VDR

Copyright (c) 2011 - 2013 by Johns.  All Rights Reserved.
Copyright (c) 2018 by jojo61.  All Rights Reserved.

Contributor(s):

jojo61

License: AGPLv3

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

$Id: 5267da021a68b4a727b479417334bfbe67bbba14 $

A software and GPU emulated UHD output device plugin for VDR.

    o Video decoder CUVID or VAAPI
    o Video output opengl or DRM
    o Audio FFMpeg / ALSA / Analog
    o Audio FFMpeg / ALSA / Digital
    o HDMI/SPDIF pass-through
    o Software volume, compression, normalize and channel resample
    o VDR ScaleVideo API
    o CUDA deinterlacer
    o Suspend / Detach
    o Support for ambilight
    o Support for Screencopy
    o PIP (Picture-in-Picture) (only for CUVID)




This is a fork of johns original softhddevice work and I reworked it to support HEVC with CUDA and opengl output.
Currently I have tested it with a GTX 1050 from NVIDIA. SD, HD and UHD is working.

Current Status NVIDIA:
The CUDA driver supports HEVC with 8 Bit and 10 Bit up to UHD resolution. Opengl is able to output also 10 Bit, but NVIDIA does not support to output 10 Bit via HDMI.
Only via DisplayPort you can get 10 Bit output to a compatible screen. This is a restriction from NVIDIA.

Current Status with VAAPI:
I tested it with Intel VAAPI. If you have problmes with the shaders then copy the drirc file in your home directory as .drirc
AMD VAAPI is broken by AMD and will not work currently.

You have to adapt the Makefile to your needs. I use FFMPEG 4.0

This Version supports building with libplacebo. https://github.com/haasn/libplacebo
You have to enable it in the Makefile and install libplacebo yourself.

It also needs the NVIDIA driver 410.48 or newer as well as CUDA 10.

I recommend to use libplacebo. It has much better scaler and does colorconversion for HDR the correct way.

If your FFMEG supports it then you can enable YADIF in the Makefile and select between the buildin NVIDIA CUDA deinterlacer and the YADIF cuda deinterlacer.

Good luck
jojo61

Quickstart:
-----------

You have to adapt the Makefile. There are 3 possible Version that you can build:

    softhdcuvid  (CUVID=1)
    This is for NVIDA cards and uses cuvid as decoder. It uses xcb for output and needs a X Server to run.
    I recommend to use libplacebo and set LIBPLACEBO=1 in the Makefile

    softhdvaapi (VAAPI=1)
    This is for INTEL cards and uses Vaapi as decoder. It uses xcb for output and needs a X Server to run.
    I recommend to use libplacebo and set LIBPLACEBO=1 in the Makefile. Also LIBPLACEBO_GL is supportet here.

    softhddrm (DRM=1)
    This is for INTEL cards and also uses Vaapi as decoder. It uses the DRM API for output and
    runs without X Server. There are several commandline options to select the resolution and refresh rate.
    I recommend to use libplacebo and set LIBPLACEBO_GL=1 in the Makefile.

    You should use the following libplacebo Version:
	https://github.com/haasn/libplacebo/archive/db794a2fcc8214624c950752b04f6c23f8fc567d.zip
	Newer Versions may not work.


Install:
--------
	1a) git

	git clone git://github.com/jojo61/vdr-plugin-softhdcuvid.git
	cd vdr-plugin-softhdcuvid
	make
	make install

	You have to start vdr with -P 'softhdcuvid -d :0.0  ..<more option>.. '

Beginners Guide for libplacebo:
-------------------------------
    When using libplacebo you will find several config options.

    First of all you need to set the right scaler for each resolution:
    Best you begin with setting all to "bilinear". If that works ok for you, you can try to change them
    for more advanced scaler. I use ewa_robidouxsharp on my GTX1050, but your mileage may vary.
    Unfortunatly on INTEL not all scalers may work or crash.
    The Intel GPU is much slower than NVIDIA and for UHD you most likly need to set the scaler to bilinear.

    You can enable a Scaler Test feature. When enabled then the screen is split.On the left half you will
    see the scaler defined by Scaler Test and on the right side you will see the scaler defined at the
    Resolution setting. There is as small black line between the halfs to remaind you that Scaler Test
    is activ.

    Then you should set the Monitor Type to "sRGB". This guarantees you the best colors on your screen.
    At the moment all calculations internaly are done in RGB space and all cards output also RGB.
    If you use the softhddrm Version then you should set the Monitor Type to HD TV or UHD-HDR TV if you have
    connected one of those.

    If you are colorblind you could try to remedy this with the Colorblind Settings. Realy only needed
    in rare cases.

    All other settings can be in their default state.

    Beginning with libplacebo API 58 user shaders from mpv are supported. Use -S parameter to set the shader.
    The plugins searches the shaders in $ConfigDir/plugins/shaders for the shaders. One example shader is
    provided in the shader subdirectory. Copy it to e.g.: /etc/vdr/plugins/shaders and then start
    vdr -P 'softhdcuvid -S filmgrain.glsl ...'
    I use KrigBilateral for UV scaling and then adaptive-sharpen for sharpening. This results in a perfect
    picture for me.

    You can also use a custon LUT File. It is located in $ConfigDir/shaders/lut/lut.cube. If you provide there
    a lut file it will be automaticly used. In the Mainmenue you can switch LUT on and off.

Konfig Guide for softhddrm Version
----------------------------------
    You should set the Monitor Type to HD TV or UHD-HDR TV depending on your TV Set
    With softhddrm and a HDR TV Set you can view HDR-HLG content. This is tested with Kernel 5.12 and a Intel NUC.



Setup:	environment
------
	Following is supported:

	DISPLAY=:0.0
		X11 display name

    ALSA configuration:
	ALSA_DEVICE=default
		ALSA PCM device name
	ALSA_PASSTHROUGH_DEVICE=
		ALSA pass-though (AC-3,E-AC-3,DTS,...) device name
	ALSA_MIXER=default
		ALSA control device name
	ALSA_MIXER_CHANNEL=PCM
		ALSA control channel name

Setup: /etc/vdr/setup.conf
------
	Following is supported:

	softhddevice.MakePrimary = 0
	0 = no change, 1 make softhddevice primary at start

	softhddevice.HideMainMenuEntry = 0
	0 = show softhddevice main menu entry, 1 = hide entry

	softhddevice.Osd.Width = 0
	0 = auto (=display, unscaled) n = fixed osd size scaled for display
	softhddevice.Osd.Height = 0
	0 = auto (=display, unscaled) n = fixed osd size scaled for display

	<res> of the next parameters is 576i, 720p, 1080i_fake or 1080i.
	1080i_fake is 1280x1080 or 1440x1080
	1080i is "real" 1920x1080

	softhddevice.<res>.Scaling = 0
	0 = normal, 1 = fast, 2 = HQ, 3 = anamorphic

	softhddevice.<res>.Deinterlace = 0
	0 = bob, 1 = weave, 2 = temporal, 3 = temporal_spatial, 4 = software
	(only 0, 1, 4 supported with VA-API)

	softhddevice.<res>.SkipChromaDeinterlace = 0
	0 = disabled, 1 = enabled (for slower cards, poor qualit�t)

	softhddevice.<res>.InverseTelecine = 0
	0 = disabled, 1 = enabled

	softhddevice.<res>.Denoise = 0
	0 .. 1000 noise reduction level (0 off, 1000 max)

	softhddevice.<res>.Sharpness = 0
	-1000 .. 1000 noise reduction level (0 off, -1000 max blur,
	    1000 max sharp)

	softhddevice.<res>.CutTopBottom = 0
	Cut 'n' pixels at at top and bottom of the video picture.

	softhddevice.<res>.CutLeftRight = 0
	Cut 'n' pixels at at left and right of the video picture.

	softhddevice.AudioDelay = 0
	+n or -n ms
	delay audio or delay video

	softhddevice.AudioPassthrough = 0
	0 = none, 1 = PCM, 2 = MPA, 4 = AC-3, 8 = EAC-3, -X disable

	for PCM/AC-3/EAC-3 the pass-through device is used and the audio
	stream is passed undecoded to the output device.
	z.b. 12 = AC-3+EAC-3, 13 = PCM+AC-3+EAC-3
	note: MPA/DTS/TrueHD/... aren't supported yet
	negative values disable passthrough

	softhddevice.AudioDownmix = 0
	0 = none, 1 = downmix
	Use ffmpeg/libav downmix of AC-3/EAC-3 audio to stereo.

	softhddevice.AudioSoftvol = 0
	0 = off, use hardware volume control
	1 = on, use software volume control

	softhddevice.AudioNormalize = 0
	0 = off, 1 = enable audio normalize

	softhddevice.AudioMaxNormalize = 0
	maximal volume factor/1000 of the normalize filter

	softhddevice.AudioCompression = 0
	0 = off, 1 = enable audio compression

	softhddevice.AudioMaxCompression = 0
	maximal volume factor/1000 of the compression filter

	softhddevice.AudioStereoDescent = 0
	reduce volume level (/1000) for stereo sources

	softhddevice.AudioBufferTime = 0
	0 = default (336 ms)
	1 - 1000 = size of the buffer in ms

	softhddevice.Background = 0
	32bit RGBA background color
	(Red * 16777216 +  Green * 65536 + Blue * 256 + Alpha)
	or hex RRGGBBAA
	grey 127 * 16777216 + 127 * 65536 + 127 * 256 => 2139062016
	in the setup menu this is entered as (24bit RGB and 8bit Alpha)
	(Red * 65536 +  Green * 256 + Blue)

	softhddevice.StudioLevels = 0
		0 use PC levels (0-255) with vdpau.
		1 use studio levels (16-235) with vdpau.

	softhddevice.Suspend.Close = 0
	1 suspend closes x11 window, connection and audio device.
	(use svdrpsend plug softhddevice RESU to resume, if you have no lirc)

	softhddevice.Suspend.X11 = 0
	1 suspend stops X11 server (not working yet)

	softhddevice.60HzMode = 0
	0 disable 60Hz display mode
	1 enable 60Hz display mode

	softhddevice.SoftStartSync = 0
	0 disable soft start of audio/video sync
	1 enable soft start of audio/video sync

	softhddevice.BlackPicture = 0
	0 disable black picture during channel switch
	1 enable black picture during channel switch

	softhddevice.ClearOnSwitch = 0
	0 keep video und audio buffers during channel switch
	1 clear video and audio buffers on channel switch

	softhddevice.Video4to3DisplayFormat = 1
	0 pan and scan
	1 letter box
	2 center cut-out
	3 original

	softhddevice.VideoOtherDisplayFormat = 1
	0 pan and scan
	1 pillar box
	2 center cut-out
	3 original

	softhddevice.pip.X = 79
	softhddevice.pip.Y = 78
	softhddevice.pip.Width = 18
	softhddevice.pip.Height = 18
	PIP pip window position and size in percent.

	softhddevice.pip.VideoX = 0
	softhddevice.pip.VideoY = 0
	softhddevice.pip.VideoWidth = 0
	softhddevice.pip.VideoHeight = 0
	PIP video window position and size in percent.

	softhddevice.pip.Alt.X = 0
	softhddevice.pip.Alt.Y = 50
	softhddevice.pip.Alt.Width = 0
	softhddevice.pip.Alt.Height = 50
	PIP alternative pip window position and size in percent.

	softhddevice.pip.Alt.VideoX = 0
	softhddevice.pip.Alt.VideoY = 0
	softhddevice.pip.Alt.VideoWidth = 0
	softhddevice.pip.Alt.VideoHeight = 50
	PIP alternative video window position and size in percent.


Commandline:
------------

	Use vdr -h to see the command line arguments supported by the plugin.

    -a audio_device

	Selects audio output module and device.
	""		to disable audio output
	other		to use ALSA audio module

SVDRP:
------

	Use 'svdrpsend.pl plug softhddevice HELP'
	or 'svdrpsend plug softhddevice HELP' to see the SVDRP commands help
	and which are supported by the plugin.

Keymacros:
----------

	See keymacros.conf how to setup the macros.

	This are the supported key sequences:

	@softhdcuvid Blue 1 0		disable pass-through
	@softhdcuvid Blue 1 1		enable pass-through
	@softhdcuvid Blue 1 2		toggle pass-through
	@softhdcuvid Blue 1 3		decrease audio delay by 10ms
	@softhdcuvid Blue 1 4		increase audio delay by 10ms
	@softhdcuvid Blue 1 5		toggle ac3 mixdown
	@softhdcuvid Blue 2 0		disable fullscreen
	@softhdcuvid Blue 2 1		enable fullscreen
	@softhdcuvid Blue 2 2		toggle fullscreen

Running:
--------

	Click into video window to toggle fullscreen/window mode, only if you
	have a window manager running.


Known Bugs:
-----------
	SD Streams not working very well on vaapi
