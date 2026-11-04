CC      ?= clang
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(SDL_CFLAGS)
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

all: build/hercules

build/%.o: src/%.c src/*.h $(wildcard src/*.inc) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/hercules: $(OBJ)
	$(CC) $(OBJ) -o $@ $(SDL_LIBS) -lm

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
