CC      ?= clang
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(SDL_CFLAGS)
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

all: build/hercules app

# Keep a local Hercules.app (made by make_app.sh) running the binary just built. Skipped when there is no app.
app: build/hercules
	@if [ -d Hercules.app ]; then \
		cp build/hercules Hercules.app/Contents/MacOS/hercules && \
		codesign --force --deep --sign - Hercules.app 2>/dev/null; \
		echo "updated Hercules.app"; \
	fi

build/%.o: src/%.c src/*.h $(wildcard src/*.inc) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/hercules: $(OBJ)
	$(CC) $(OBJ) -o $@ $(SDL_LIBS) -lm

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all app clean
