CC ?= cc
CFLAGS ?= -O2 -g
PNG_CFLAGS = $(shell pkg-config --cflags libpng)
PNG_LIBS = $(shell pkg-config --libs libpng)
NATIVE_SRC = src/bootstrap.c src/me_startup.c src/me_registration.c src/native/runtime.c src/native/pops_boot.c src/native/pops_image.c src/native/pops_disc.c src/native/pops_config.c src/native/pops_metadata.c src/native/pops_reset.c src/native/pops_graphics.c src/native/me_worker.c src/native/pops_me.c src/native/pops_spu.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/pops_memory_card.c src/native/main.c

UNICORN_PYTHON ?= $(if $(wildcard .tools/verify-env/bin/python),.tools/verify-env/bin/python,python3)
UNICORN_ROOT ?= $(shell $(UNICORN_PYTHON) -c 'import pathlib,unicorn;print(pathlib.Path(unicorn.__file__).parent)' 2>/dev/null)
UNICORN_CFLAGS = -I"$(UNICORN_ROOT)/include"
UNICORN_LIBS = "$(UNICORN_ROOT)/lib/libunicorn.a" -lpthread -lm
NATIVE_SRC += src/native/generated_unicorn.c src/native/pops_dispatch.c src/native/pops_events.c
NATIVE_SRC += src/native/pops_spu_registers.c
NATIVE_SRC += src/native/pops_compile_ram.c
NATIVE_SRC += src/native/pops_cdrom.c
NATIVE_SRC += src/native/pops_cd_block.c
NATIVE_SRC += src/native/pops_dma.c
NATIVE_SRC += src/native/pops_gpu.c
NATIVE_SRC += src/native/pops_gte.c
NATIVE_SRC += src/native/pops_display.c
NATIVE_SRC += src/native/pops_serial.c
NATIVE_SRC += src/native/pops_mdec.c
NATIVE_SRC += src/native/pops_spu_dma.c
CD_LIBS = -lz

.PHONY: all native analyze test test-native-disc test-native-config test-native-me test-native-spu test-native-emit test-native-memory-card test-generated-code test-unicorn-cache clean-help
all: native
native: build/repops-native
analyze: build/repops-analyze

build/repops-native build/test_native_events build/test_native_cdrom: src/native/pops_timer.h src/native/pops_gpu.h
build/repops-native: src/native/pops_display.h
build/repops-native: src/native/pops_mdec.h
build/repops-native: src/native/pops_spu_dma.h
build/test_native_events build/test_native_cdrom: src/native/pops_spu_dma.h src/native/pops_mdec.h
build/repops-native build/test_native_memory_card build/test_native_events build/test_native_cdrom: src/native/pops_memory_card.h
build/test_native_gpu: src/native/pops_dma.h
build/repops-native build/repops-analyze build/test_native_emit build/test_unicorn_cache: src/native/pops_gte.h

build/test_native_gte: src/native/runtime.c src/native/pops_gte.c src/native/pops_gte.h src/native/pops_state.h src/native/runtime.h tests/test_native_gte.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_gte.c tests/test_native_gte.c -lm -o $@

.PHONY: test-native-gte
test-native-gte: build/test_native_gte
	./build/test_native_gte

build/test_native_mdec: src/native/runtime.c src/native/pops_mdec.c src/native/pops_mdec.h src/native/pops_state.h src/native/runtime.h tests/test_native_mdec.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_mdec.c tests/test_native_mdec.c -o $@

.PHONY: test-native-mdec
test-native-mdec: build/test_native_mdec
	./build/test_native_mdec

build/test_native_spu_dma: src/native/runtime.c src/native/pops_spu_dma.c src/native/pops_spu_dma.h src/native/pops_state.h src/native/runtime.h tests/test_native_spu_dma.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_spu_dma.c tests/test_native_spu_dma.c -o $@

.PHONY: test-native-spu-dma
test-native-spu-dma: build/test_native_spu_dma
	./build/test_native_spu_dma

build/test_native_display: src/native/runtime.c src/native/pops_display.c src/native/pops_display.h src/native/pops_gpu.h src/native/pops_state.h src/native/runtime.h tests/test_native_display.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_display.c tests/test_native_display.c -o $@

.PHONY: test-native-display
test-native-display: build/test_native_display
	./build/test_native_display

build/repops-analyze: src/native/runtime.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/analyze_main.c src/native/runtime.h src/native/me_worker.h src/native/pops_emit.h src/native/pops_ir.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) src/native/runtime.c src/native/pops_analyze.c src/native/pops_compile.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/analyze_main.c -o $@

build/repops-native: $(NATIVE_SRC) src/native/runtime.h src/native/pops_state.h src/native/pops_cdrom.h src/native/pops_dma.h src/native/pops_gpu.h src/native/pops_serial.h src/bootstrap.h src/me_startup.h src/me_registration.h src/native/me_worker.h src/native/pops_emit.h src/native/pops_ir.h
	mkdir -p build
	@test -f "$(UNICORN_ROOT)/include/unicorn/unicorn.h" || { echo 'Install unicorn==2.1.4 in the local Python environment, or set UNICORN_ROOT'; exit 1; }
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) $(PNG_CFLAGS) $(UNICORN_CFLAGS) $(NATIVE_SRC) $(PNG_LIBS) $(UNICORN_LIBS) $(CD_LIBS) -o $@

test:
	python3 -m unittest discover -s tests -v

build/test_native_gpu: src/native/runtime.c src/native/pops_gpu.c src/native/pops_gpu.h src/native/pops_state.h src/native/pops_cdrom.h src/native/runtime.h tests/test_native_gpu.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_gpu.c tests/test_native_gpu.c -o $@

.PHONY: test-native-gpu
test-native-gpu: build/test_native_gpu
	./build/test_native_gpu

build/test_native_events: src/native/runtime.c src/native/pops_events.c src/native/pops_dma.c src/native/pops_serial.c src/native/pops_memory_card.c src/native/pops_dma.h src/native/pops_serial.h src/native/runtime.h src/native/pops_state.h src/native/pops_cdrom.h tests/test_native_events.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_events.c src/native/pops_dma.c src/native/pops_serial.c src/native/pops_memory_card.c tests/test_native_events.c -o $@

.PHONY: test-native-events
test-native-events: build/test_native_events
	./build/test_native_events

build/test_native_cdrom: src/native/runtime.c src/native/pops_cdrom.c src/native/pops_cd_block.c src/native/pops_events.c src/native/pops_dma.c src/native/pops_serial.c src/native/pops_memory_card.c src/native/pops_dma.h src/native/pops_serial.h src/native/pops_config.c src/native/pops_disc.c src/native/pops_cdrom.h src/native/pops_state.h tests/test_native_cdrom.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_cdrom.c src/native/pops_cd_block.c src/native/pops_events.c src/native/pops_dma.c src/native/pops_serial.c src/native/pops_memory_card.c src/native/pops_config.c src/native/pops_disc.c tests/test_native_cdrom.c -lm -lz -o $@

.PHONY: test-native-cdrom
test-native-cdrom: build/test_native_cdrom
	./build/test_native_cdrom

build/test_spu_registers: src/native/runtime.c src/native/pops_spu_registers.c src/native/runtime.h tests/test_spu_registers.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_spu_registers.c tests/test_spu_registers.c -o $@

.PHONY: test-spu-registers
test-spu-registers: build/test_spu_registers
	./build/test_spu_registers

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

build/test_native_spu: src/native/runtime.c src/native/runtime.h src/native/pops_state.h src/native/pops_spu.c tests/test_native_spu.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_spu.c tests/test_native_spu.c -o $@

test-native-spu: build/test_native_spu
	./build/test_native_spu

build/test_native_emit: src/native/runtime.c src/native/pops_emit.c src/native/pops_emit_memory.c src/native/pops_emit.h src/native/pops_ir.h src/native/runtime.h tests/test_native_emit.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_emit.c src/native/pops_emit_memory.c tests/test_native_emit.c -o $@

test-native-emit: build/test_native_emit
	./build/test_native_emit

build/test_native_memory_card: src/native/runtime.c src/native/pops_memory_card.c src/native/runtime.h tests/test_native_memory_card.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/pops_memory_card.c tests/test_native_memory_card.c -o $@

test-native-memory-card: build/test_native_memory_card
	./build/test_native_memory_card

build/test_generated_code: src/native/runtime.c src/native/generated_code.c src/native/runtime.h tests/test_generated_code.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined src/native/runtime.c src/native/generated_code.c tests/test_generated_code.c -o $@

test-generated-code: build/test_generated_code
	./build/test_generated_code

build/test_unicorn_cache: src/native/runtime.c src/native/generated_unicorn.c src/native/runtime.h tests/test_unicorn_cache.c
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g $(UNICORN_CFLAGS) src/native/runtime.c src/native/generated_unicorn.c tests/test_unicorn_cache.c $(UNICORN_LIBS) -o $@

test-unicorn-cache: build/test_unicorn_cache
	./build/test_unicorn_cache

clean-help:
	@echo 'Analysis artifacts are retained. Remove only explicitly selected local outputs.'
