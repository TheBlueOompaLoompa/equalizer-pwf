CXX = clang++
EXE = equalizer-pwf

IMGUI_DIR = imgui
SOURCES = src/main.cpp src/audio_sink_info.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_sdl3.cpp $(IMGUI_DIR)/backends/imgui_impl_sdlgpu3.cpp
OBJS = $(patsubst %.cpp,build/%.o,$(SOURCES))

CXXFLAGS = -I inc -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat
CXXFLAGS += `pkg-config --cflags sdl3`
CXXFLAGS += `pkg-config --cflags libpipewire-0.3`
CFLAGS = $(CXXFLAGS) 

LIBS = -lm -ldl 
LIBS += `pkg-config --libs sdl3`
LIBS += `pkg-config --libs libpipewire-0.3`

build/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/$(EXE): $(OBJS)
	$(CXX) $^ -o $@ $(LIBS)

all: build/$(EXE)
	@echo Build complete

run: build/$(EXE)
	build/$(EXE)

clean:
	rm -rf build

