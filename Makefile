APP_TITLE      := NSTV Native
APP_AUTHOR     := Gilson Santos / OpenAI
APP_VERSION    := 0.3.0
TARGET         := nstv-native
BUILD          := build
SOURCES        := source
INCLUDES       := include

HOST_CXX       ?= g++
HOST_CXXFLAGS  = -std=c++17 -Wall -Wextra -O2 -DNSTV_USE_SDL -DNSTV_USE_SDL_TTF -DNSTV_USE_SDL_IMAGE $(HOST_FFMPEG_CFLAGS) -I$(INCLUDES)
HOST_SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null)
HOST_SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null)
HOST_CURL_LIBS  := $(shell pkg-config --libs libcurl 2>/dev/null || curl-config --libs)
HOST_FFMPEG_CFLAGS := $(shell pkg-config --exists libavformat libavcodec libavutil libswscale 2>/dev/null && echo -DNSTV_USE_FFMPEG $$(pkg-config --cflags libavformat libavcodec libavutil libswscale))
HOST_FFMPEG_LIBS   := $(shell pkg-config --exists libavformat libavcodec libavutil libswscale 2>/dev/null && pkg-config --libs libavformat libavcodec libavutil libswscale)
HOST_LDFLAGS   := $(HOST_SDL_LIBS) $(HOST_FFMPEG_LIBS) $(HOST_CURL_LIBS) -lz
HOST_SRCS      := $(wildcard $(SOURCES)/*.cpp)
HOST_BIN       := $(BUILD)/$(TARGET)-host

.PHONY: all host switch clean run status legacy-host

all: host

host: $(HOST_BIN)

$(HOST_BIN): $(HOST_SRCS)
	@mkdir -p $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_SDL_CFLAGS) $^ -o $@ $(HOST_LDFLAGS)

run: host
	$(HOST_BIN)

status:
	@echo "NSTV Native C++ SDL app"
	@echo "Host binary: $(HOST_BIN)"
	@echo "Host requires: libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev"
	@echo "Switch requires: devkitPro/libnx + switch-sdl2 + switch-sdl2_ttf + switch-sdl2_image + switch-curl"

# Switch build: requires devkitPro, libnx and SDL2/curl portlibs installed.
switch:
ifndef DEVKITPRO
	$(error DEVKITPRO is not set. Install devkitPro/devkitA64/libnx first.)
endif
	$(MAKE) -f Makefile.switch

clean:
	rm -rf $(BUILD) build-switch $(TARGET).elf $(TARGET).nro $(TARGET).nacp nstv-frame.bmp nstv-frame.ppm
