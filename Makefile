CC ?= cc
CFLAGS ?= -O2 -g
PNG_CFLAGS = $(shell pkg-config --cflags libpng)
PNG_LIBS = $(shell pkg-config --libs libpng)
NATIVE_SRC = src/bootstrap.c src/me_startup.c src/me_registration.c src/native/runtime.c src/native/pops_boot.c src/native/pops_image.c src/native/pops_disc.c src/native/pops_config.c src/native/pops_metadata.c src/native/pops_reset.c src/native/pops_graphics.c src/native/me_worker.c src/native/pops_me.c src/native/pops_spu.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/main.c

.PHONY: all native analyze test test-native-disc test-native-config test-native-me test-native-spu test-native-emit clean-help
all: native
native: build/repops-native
analyze: build/repops-analyze

build/repops-analyze: src/native/runtime.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/analyze_main.c src/native/runtime.h src/native/me_worker.h src/native/pops_emit.h src/native/pops_ir.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) src/native/runtime.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/analyze_main.c -o $@

build/repops-native: $(NATIVE_SRC) src/native/runtime.h src/bootstrap.h src/me_startup.h src/me_registration.h src/native/me_worker.h src/native/pops_emit.h src/native/pops_ir.h
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

build/test_native_spu: src/native/runtime.c src/native/runtime.h src/native/pops_spu.c tests/test_native_spu.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_spu.c tests/test_native_spu.c -o $@

test-native-spu: build/test_native_spu
	./build/test_native_spu

build/test_native_emit: src/native/runtime.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/pops_emit.h src/native/pops_ir.h src/native/runtime.h tests/test_native_emit.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_emit.c src/native/pops_emit_memory.c tests/test_native_emit.c -o $@

test-native-emit: build/test_native_emit
	./build/test_native_emit

clean-help:
	@echo 'Analysis artifacts are retained. Remove only explicitly selected local outputs.'
