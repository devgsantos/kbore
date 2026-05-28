APP_TITLE      := NSTV Native
APP_AUTHOR     := Gilson Santos / OpenAI
APP_VERSION    := 0.2.0
TARGET         := nstv-native
BUILD          := build
SOURCES        := source
INCLUDES       := include

HOST_CXX       ?= g++
HOST_CXXFLAGS  := -std=c++17 -Wall -Wextra -O2 -I$(INCLUDES)
HOST_LDFLAGS   := $(shell pkg-config --libs libcurl 2>/dev/null || curl-config --libs) -lz
HOST_SRCS      := $(wildcard $(SOURCES)/*.cpp)
HOST_BIN       := $(BUILD)/$(TARGET)-host

.PHONY: all host switch clean run status

all: host

host: $(HOST_BIN)

$(HOST_BIN): $(HOST_SRCS)
	@mkdir -p $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $^ -o $@ $(HOST_LDFLAGS)

run: host
	$(HOST_BIN)

status:
	@echo "NSTV Native C++ app"
	@echo "Host binary: $(HOST_BIN)"
	@echo "Switch build requires devkitPro/libnx + switch-curl. Run: make switch"

# Switch build: requires devkitPro, libnx and curl portlibs installed.
switch:
ifndef DEVKITPRO
	$(error DEVKITPRO is not set. Install devkitPro/devkitA64/libnx first.)
endif
	$(MAKE) -f Makefile.switch

clean:
	rm -rf $(BUILD)
