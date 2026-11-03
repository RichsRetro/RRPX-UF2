.DEFAULT_GOAL := all
XBE_TITLE = RRPXUF2-Retail
INCDIR = $(CURDIR)/Includes
RESOURCEDIR = $(CURDIR)/../branding
XBE_LOGO = $(RESOURCEDIR)/rrxd-xbe-logo.pgm
SRCDIR = $(CURDIR)/Sources

SRCS += \
	$(CURDIR)/Sources/startupProgress.cpp \
	$(CURDIR)/main.cpp \
	$(SRCDIR)/liteAssets.cpp \
	$(SRCDIR)/setupBackup.cpp \
	$(SRCDIR)/setupMenu.cpp \
	$(SRCDIR)/setupReport.cpp \
	$(SRCDIR)/setupTransaction.cpp \
	$(SRCDIR)/runtimePatches.cpp \
	$(SRCDIR)/bootComponent.cpp \
	$(SRCDIR)/recoveryBoot.cpp \
	$(SRCDIR)/recoveryFtp.c \
	$(SRCDIR)/diagnosticsMenu.cpp \
	$(SRCDIR)/audioMenu.cpp \
	$(SRCDIR)/config.cpp \
	$(SRCDIR)/dashboardPaths.cpp \
	$(SRCDIR)/font.cpp \
	$(SRCDIR)/fileExplorer.cpp \
	$(SRCDIR)/dvdCopy.cpp \
	$(SRCDIR)/gamePatches.cpp \
	$(SRCDIR)/ftpConnection.cpp \
	$(SRCDIR)/ftpServer.cpp \
	$(SRCDIR)/infoLog.cpp \
	$(SRCDIR)/langMenu.cpp \
	$(SRCDIR)/i18n.cpp \
	$(SRCDIR)/logViewer.cpp \
	$(SRCDIR)/logViewerMenu.cpp \
	$(SRCDIR)/menu.cpp \
	$(SRCDIR)/networkManager.cpp \
	$(SRCDIR)/networking.cpp \
	$(SRCDIR)/renderer.cpp \
	$(SRCDIR)/settingsMenu.cpp \
	$(SRCDIR)/sntpClient.cpp \
	$(SRCDIR)/subAppRouter.cpp \
	$(SRCDIR)/subsystems.cpp \
	$(SRCDIR)/timeMenu.cpp \
	$(SRCDIR)/timing.cpp \
	$(SRCDIR)/videoMenu.cpp \
	$(SRCDIR)/wipeCache.cpp \
	$(SRCDIR)/xbeLauncher.cpp \
	$(SRCDIR)/xbeScanner.cpp \
	$(CURDIR)/3rdparty/SDL_FontCache/SDL_FontCache.c

NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_SDL = y
NXDK_CXX = y
NXDK_NET = y
NXDK_DISABLE_AUTOMOUNT_D = y

GEN_XISO = ${XBE_TITLE}.iso

CXXFLAGS += -I$(CURDIR) \
            -I$(INCDIR) \
            -Wall \
            -Wextra \
            -std=gnu++11 \
            -DSDL_DISABLE_JOYSTICK_INIT_DELAY

CFLAGS   += -std=gnu11 \
            -DSDL_DISABLE_JOYSTICK_INIT_DELAY
CFLAGS += -I$(INCDIR)

ifneq ($(DEBUG),y)
CFLAGS += -O2
CXXFLAGS += -O2
endif

new_all: copy_resources all

include $(NXDK_DIR)/Makefile

copy_resources: $(OUTPUT_DIR)/config/config.json
	@mkdir -p $(OUTPUT_DIR)/assets $(OUTPUT_DIR)/licences $(OUTPUT_DIR)/game-patches
	@cp $(CURDIR)/../game-patches/*.json $(OUTPUT_DIR)/game-patches
	@cp $(RESOURCEDIR)/Share-Bold.ttf $(RESOURCEDIR)/rich-retro-xdash-lite-logo.jpg $(OUTPUT_DIR)/assets
	@cp $(RESOURCEDIR)/OFL.txt $(OUTPUT_DIR)/licences

$(OUTPUT_DIR)/config/config.json: $(CURDIR)/../config-hardmod.json
	@mkdir -p $(OUTPUT_DIR)/config
	cp $(CURDIR)/../config-hardmod.json $(OUTPUT_DIR)/config/config.json

# Size-focused optimization is confined to this UF2 edition.
CFLAGS += -Os
CXXFLAGS += -Os

# PicoX: copied .d files can name another checkout's absolute paths.
# Every application object explicitly depends on THIS checkout's public headers.
PICOX_LOCAL_HEADERS := $(wildcard $(CURDIR)/Includes/*.hpp $(CURDIR)/Includes/*.h)
PICOX_APP_OBJECTS := $(addsuffix .obj,$(basename $(filter $(CURDIR)/%,$(SRCS))))
$(PICOX_APP_OBJECTS): $(PICOX_LOCAL_HEADERS) $(CURDIR)/Makefile $(NXDK_DIR)/lib/net/nforceif/include/lwipopts.h

ifneq ($(filter %/permanentNative.cpp %/permanentPlan.cpp,$(SRCS)),)
$(error Retail Edition must not link permanent installation adapters)
endif
