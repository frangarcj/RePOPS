CC ?= cc
CFLAGS ?= -O2 -g
NATIVE_SRC = src/bootstrap.c src/native/runtime.c src/native/pops_boot.c src/native/main.c

.PHONY: all native test clean-help
all: native
native: build/repops-native

build/repops-native: $(NATIVE_SRC) src/native/runtime.h src/bootstrap.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) $(NATIVE_SRC) -o $@

test:
	python3 -m unittest discover -s tests -v

clean-help:
	@echo 'Analysis artifacts are retained. Remove only explicitly selected local outputs.'
