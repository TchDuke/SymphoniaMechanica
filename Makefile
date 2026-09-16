# SymphoniaMechanica — музыкальный синтезатор: движок синтеза + GUI на HID/SDL.
#
# Структура:
#   src/Synth/  — движок: осцилляторы, огибающая, пул голосов, живой звуковой поток.
#   src/app/    — GUI-приложение на фреймворке Libs/HID (SDL2), C++20.
#   ../Libs/    — общая библиотека (Core, HID, Audio, Graph) — та же, что у CFD.
#
# Цели:
#   make     собрать GUI (бинарник smp — в КОРЕНЬ проекта)
#   make run собрать и запустить
#   make clean

CXX      ?= g++
LIBS_DIR := ../Libs

SYNTH_DIR := src/Synth
APP_DIR   := src/app
BUILD     := build
OBJ       := $(BUILD)/obj
GUI       := smp

# Audio в наборе: живой звук — через Libs/Audio::Engine (микшер + SDL-устройство).
# Parsing здесь не нужен: чужих форматов v1 не читает.
LIB_SRC_DIRS := $(LIBS_DIR)/Core $(LIBS_DIR)/AppConfig $(LIBS_DIR)/AppFramework \
    $(LIBS_DIR)/Graph/Fonts $(LIBS_DIR)/HID $(LIBS_DIR)/Graph/Image $(LIBS_DIR)/Audio

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

APP_FLAGS := -std=c++20 -O2 -Wall -Wextra -fno-strict-aliasing $(SDL_CFLAGS) \
    -Isrc -Isrc/Synth -Isrc/app -I$(LIBS_DIR) -I$(LIBS_DIR)/Core -I$(LIBS_DIR)/AppConfig \
    -I$(LIBS_DIR)/AppFramework -I$(LIBS_DIR)/HID -I$(LIBS_DIR)/Graph \
    -I$(LIBS_DIR)/Graph/Fonts

DEPFLAGS := -MMD -MP

# Библиотека графических портов — ЯВНЫМ списком: прочие тач-файлы Libs/Graph
# зависят от заголовков железа и на ПК не собираются (то же, что в CFD).
GRAPH_SRCS := $(LIBS_DIR)/Graph/Gport.cpp $(LIBS_DIR)/Graph/GraphicsLL.cpp \
              $(LIBS_DIR)/Graph/Clipping.cpp $(LIBS_DIR)/Graph/Transforms.cpp \
              $(LIBS_DIR)/Graph/VectorMath.cpp $(LIBS_DIR)/Graph/strings.cpp \
              $(LIBS_DIR)/Graph/Gport/GportSdl.cpp $(LIBS_DIR)/Graph/Gport/GportSoftDriver.cpp
# Корневые подпорои Core не безопасны целиком: в них DSP (чужие кодеки),
# код под МК (DynData/IO/Timing) — вырезаем поддеревьями как в CFD.
CORE_SKIP      := DSP DynData IO Timing
CORE_SKIP_PATS := $(foreach d,$(CORE_SKIP),$(LIBS_DIR)/Core/$(d)/%)
LIB_SRCS := $(filter-out $(CORE_SKIP_PATS),$(shell find $(LIB_SRC_DIRS) -name '*.cpp' | sort)) $(GRAPH_SRCS)
LIB_OBJS := $(patsubst $(LIBS_DIR)/%.cpp,$(OBJ)/libs/%.o,$(LIB_SRCS))

SYNTH_SRCS := $(shell find $(SYNTH_DIR) -name '*.cpp' | sort)
SYNTH_OBJS := $(patsubst %.cpp,$(OBJ)/%.o,$(SYNTH_SRCS))
APP_SRCS   := $(shell find $(APP_DIR) -name '*.cpp' | sort)
APP_OBJS   := $(patsubst %.cpp,$(OBJ)/%.o,$(APP_SRCS))

DEPS := $(LIB_OBJS:.o=.d) $(SYNTH_OBJS:.o=.d) $(APP_OBJS:.o=.d)

.PHONY: all run clean
.SECONDARY: $(LIB_OBJS) $(SYNTH_OBJS) $(APP_OBJS)

all: $(GUI)

# ОБЩИЙ ШРИФТ КОПИРУЕТСя КАЖДОЙ СБОРКОЙ (правило мастерской: своя копия должна
# жить в одной версии с общей — расхождение глифов ловилось в CFD).
$(GUI): assets/default.vfont $(SYNTH_OBJS) $(APP_OBJS) $(LIB_OBJS)
	$(CXX) $(SYNTH_OBJS) $(APP_OBJS) $(LIB_OBJS) -o $@ $(SDL_LIBS) -lm

assets/default.vfont: $(LIBS_DIR)/Graph/Fonts/default.vfont
	@mkdir -p assets
	cp -f $< $@

$(OBJ)/src/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(APP_FLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ)/libs/%.o: $(LIBS_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(APP_FLAGS) $(DEPFLAGS) -c $< -o $@

run: $(GUI)
	./$(GUI)

clean:
	rm -rf $(BUILD)
	rm -f $(GUI)

-include $(DEPS)
