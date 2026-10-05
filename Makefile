CC ?= cc
CFLAGS ?= -O2 -g
PNG_CFLAGS = $(shell pkg-config --cflags libpng)
PNG_LIBS = $(shell pkg-config --libs libpng)
NATIVE_SRC = src/bootstrap.c src/native/runtime.c src/native/pops_boot.c src/native/pops_image.c src/native/pops_disc.c src/native/pops_config.c src/native/pops_metadata.c src/native/main.c

.PHONY: all native test test-native-disc test-native-config test-native-me clean-help
all: native
native: build/repops-native

build/repops-native: $(NATIVE_SRC) src/native/runtime.h src/bootstrap.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) $(PNG_CFLAGS) $(NATIVE_SRC) $(PNG_LIBS) -o $@

test:
	python3 -m unittest discover -s tests -v

build/test_native_disc: src/native/runtime.c src/native/pops_disc.c src/native/pops_config.c src/native/runtime.h tests/test_native_disc.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_disc.c src/native/pops_config.c tests/test_native_disc.c -o $@

test-native-disc: build/test_native_disc
	./build/test_native_disc

build/test_native_config: src/native/runtime.c src/native/pops_disc.c src/native/pops_config.c src/native/runtime.h tests/test_native_config.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_disc.c src/native/pops_config.c tests/test_native_config.c -o $@

test-native-config: build/test_native_config
	./build/test_native_config

build/test_me_worker: src/native/me_worker.c src/native/me_worker.h tests/test_me_worker.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/me_worker.c tests/test_me_worker.c -o $@

test-native-me: build/test_me_worker
	./build/test_me_worker

clean-help:
	@echo 'Analysis artifacts are retained. Remove only explicitly selected local outputs.'
