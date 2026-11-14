#---------------------------------------------------------------------------------------------------------------------
# TARGET is the name of the output.
# BUILD is the directory where object files & intermediate files will be placed.
# LIBBUTANO is the main directory of butano library (https://github.com/GValiente/butano).
# PYTHON is the path to the python interpreter.
# SOURCES is a list of directories containing source code.
# INCLUDES is a list of directories containing extra header files.
# DATA is a list of directories containing binary data files with *.bin extension.
# GRAPHICS is a list of files and directories containing files to be processed by grit.
# AUDIO is a list of files and directories containing files to be processed by the audio backend.
# AUDIOBACKEND specifies the backend used for audio playback. Supported backends: maxmod, aas, null.
# AUDIOTOOL is the path to the tool used process the audio files.
# DMGAUDIO is a list of files and directories containing files to be processed by the DMG audio backend.
# DMGAUDIOBACKEND specifies the backend used for DMG audio playback. Supported backends: default, null.
# ROMTITLE is a uppercase ASCII, max 12 characters text string containing the output ROM title.
# ROMCODE is a uppercase ASCII, max 4 characters text string containing the output ROM code.
#---------------------------------------------------------------------------------------------------------------------
TARGET      	:=  $(notdir $(CURDIR))
BUILD       	:=  build
LIBBUTANO   	:=  ../butano/butano
PYTHON      	:=  python
SOURCES     	:=  src
INCLUDES    	:=  include ../butano/common/include
DATA        	:=
GRAPHICS    	:=  graphics ../butano/common/graphics
AUDIO       	:=  audio
AUDIOBACKEND	:=  maxmod
AUDIOTOOL		:=
DMGAUDIO    	:=  dmg_audio
DMGAUDIOBACKEND	:=  default
ROMTITLE    	:=  RHYTHM CAVES
ROMCODE     	:=  SRPE
USERFLAGS   	:=  -DBN_CFG_BGS_MAX_ITEMS=8
USERCXXFLAGS	:=
USERASFLAGS 	:=
USERLDFLAGS 	:=
USERLIBDIRS 	:=
USERLIBS    	:=
DEFAULTLIBS 	:=
STACKTRACE		:=
USERBUILD   	:=
EXTTOOL     	:=

#---------------------------------------------------------------------------------------------------------------------
# Export absolute butano path:
#---------------------------------------------------------------------------------------------------------------------
ifndef LIBBUTANOABS
	export LIBBUTANOABS	:=	$(realpath $(LIBBUTANO))
endif

#---------------------------------------------------------------------------------------------------------------------
# Include main makefile:
#---------------------------------------------------------------------------------------------------------------------
include $(LIBBUTANOABS)/butano.mak
