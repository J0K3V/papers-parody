# Parody, Please - native build
#
#   mingw32-make            build it
#   mingw32-make run        build and render a test frame
#   mingw32-make clean
#
# raylib and the json header live in ../vendor, so the only thing you need on
# the machine is a compiler.

CXX      := g++
# -MMD -MP writes a .d file per object listing the headers it used, so changing
# a header rebuilds what depends on it. Without this, editing a struct in a
# header left stale objects linked against the old layout - which crashes at
# startup in a way that looks nothing like the change that caused it.
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -MMD -MP
INCLUDES := -I../vendor/raylib -I../vendor/json -Isrc
# -mwindows builds it as a windowed program rather than a console one, so
# double-clicking the exe does not put a black console box behind the editor.
# The --render mode borrows the terminal it was launched from instead - see
# console.cpp - so it still prints its progress.
LDFLAGS  := -L../vendor/raylib -mwindows
# comdlg32 is the system's open and save panels, used by file_dialog.cpp.
LDLIBS   := -lraylib -lopengl32 -lgdi32 -lwinmm -lcomdlg32

SOURCES  := $(wildcard src/*.cpp)
OBJECTS  := $(patsubst src/%.cpp,build/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)
TARGET   := build/ParodyPlease.exe

# The icon and the file's version details, compiled into the executable so
# Explorer and the task bar have something to show. windres comes with the same
# toolchain as the compiler; if it is missing the build simply goes without.
WINDRES  := $(shell which windres 2>/dev/null)
ifneq ($(WINDRES),)
RESOURCE := build/app.res.o
endif

all: $(TARGET)

$(TARGET): $(OBJECTS) $(RESOURCE)
	@mkdir -p build
	$(CXX) $(OBJECTS) $(RESOURCE) -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "built $@"

build/app.res.o: app.rc app.ico
	@mkdir -p build
	$(WINDRES) app.rc -O coff -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET) ../assets ../build/native-frame.png

clean:
	rm -rf build

-include $(DEPS)

.PHONY: all run clean
