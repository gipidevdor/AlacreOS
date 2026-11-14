CLANGXX ?= clang++
LD_LLD ?= ld.lld
HOST_CXX ?= c++
PYTHON ?= python3

WARNINGS := -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wpedantic -Werror
KERNEL_FLAGS := -std=c++23 -O2 $(WARNINGS) -ffreestanding -fno-exceptions \
	-fno-rtti -fno-stack-protector -fno-pic -fno-pie -I kernel
ARM_TARGET := --target=aarch64-none-elf -mgeneral-regs-only
X86_TARGET := --target=x86_64-none-elf -mno-red-zone -mgeneral-regs-only

.PHONY: all kernels test smoke
all: kernels
kernels: build/aarch64/kernel.elf build/x86_64/kernel.elf

build/aarch64/kernel.elf: kernel/arch/aarch64/layout.ld build/aarch64/start.o build/aarch64/platform.o build/aarch64/main.o
	$(LD_LLD) --no-undefined -T $< $(filter %.o,$^) -o $@

build/x86_64/kernel.elf: kernel/arch/x86_64/layout.ld build/x86_64/start.o build/x86_64/platform.o build/x86_64/main.o
	$(LD_LLD) --no-undefined -T $< $(filter %.o,$^) -o $@

build/aarch64/start.o: kernel/arch/aarch64/start.S
	mkdir -p $(@D)
	$(CLANGXX) $(ARM_TARGET) -c $< -o $@

build/x86_64/start.o: kernel/arch/x86_64/start.S
	mkdir -p $(@D)
	$(CLANGXX) $(X86_TARGET) -c $< -o $@

build/aarch64/platform.o: kernel/arch/aarch64/platform.cpp kernel/platform.hpp
	mkdir -p $(@D)
	$(CLANGXX) $(ARM_TARGET) $(KERNEL_FLAGS) -c $< -o $@

build/x86_64/platform.o: kernel/arch/x86_64/platform.cpp kernel/platform.hpp
	mkdir -p $(@D)
	$(CLANGXX) $(X86_TARGET) $(KERNEL_FLAGS) -c $< -o $@

build/aarch64/main.o: kernel/main.cpp kernel/platform.hpp kernel/edf.hpp
	mkdir -p $(@D)
	$(CLANGXX) $(ARM_TARGET) $(KERNEL_FLAGS) -c $< -o $@

build/x86_64/main.o: kernel/main.cpp kernel/platform.hpp kernel/edf.hpp
	mkdir -p $(@D)
	$(CLANGXX) $(X86_TARGET) $(KERNEL_FLAGS) -c $< -o $@

build/edf-test: tests/edf.cpp kernel/edf.hpp
	mkdir -p $(@D)
	$(HOST_CXX) -std=c++23 -O2 $(WARNINGS) -I kernel $< -o $@

test: build/edf-test
	./build/edf-test
	$(PYTHON) -m unittest discover -s examples/edf -v

smoke: kernels
	$(PYTHON) tests/boot.py
