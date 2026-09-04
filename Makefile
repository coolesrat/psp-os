# ============================================================================
#  PSP-OS  —  Makefile  (pspdev / pspsdk)
#
#  Build:   make            -> produces EBOOT.PBP
#  Clean:   make clean
#
#  IR note: the IR Blaster links against the sceSircs stubs (-lpspsircs),
#  which ship with pspdev. If your toolchain ever errors on that lib,
#  build without IR:   make NO_IR=1
#  (everything else — UI, System Monitor, About — still builds and runs.)
# ============================================================================

TARGET = PSP-OS
OBJS   = main.o

# -- IR toggle ---------------------------------------------------------------
ifeq ($(NO_IR),1)
CFLAGS_IR =
LIBS_IR   =
else
CFLAGS_IR =
LIBS_IR   = -lpspsircs
endif

# -- compiler flags ----------------------------------------------------------
CFLAGS  = -O2 -G0 -Wall $(CFLAGS_IR)
ifeq ($(NO_IR),1)
CFLAGS += -DNO_IR
endif
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  = $(CFLAGS)

# -- libraries this shell actually calls -------------------------------------
LIBS = $(LIBS_IR) -lpsppower -lpspdebug -lpspge -lpspdisplay -lpspctrl -lpspwlan -lpspusb -lpspusbstor

# -- EBOOT metadata (what shows in the XMB) ----------------------------------
EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = PSP-OS
PSP_EBOOT_ICON  = icon0.png

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
