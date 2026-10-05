CC ?= cc
CFLAGS ?= -O2 -g
PNG_CFLAGS = $(shell pkg-config --cflags libpng)
PNG_LIBS = $(shell pkg-config --libs libpng)
NATIVE_SRC = src/bootstrap.c src/native/runtime.c src/native/pops_boot.c src/native/pops_image.c src/native/main.c

.PHONY: all native test clean-help
all: native
native: build/repops-native

build/repops-native: $(NATIVE_SRC) src/native/runtime.h src/bootstrap.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) $(PNG_CFLAGS) $(NATIVE_SRC) $(PNG_LIBS) -o $@

test:
	python3 -m unittest discover -s tests -v

clean-help:
	@echo 'Analysis artifacts are retained. Remove only explicitly selected local outputs.'
