AARCH64_CXX ?= aarch64-linux-gnu-g++-14
X86_64_CXX ?= x86_64-linux-gnu-g++-14
LD_LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
HOST_CXX ?= c++
HOST_IDENTITY_CPPFLAGS ?=
HOST_IDENTITY_LIBS ?= -lssl -lcrypto -lsqlite3
PYTHON ?= python3
AARCH64_CPPFLAGS ?=
X86_64_CPPFLAGS ?=

WARNINGS := -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wpedantic -Werror
KERNEL_FLAGS := -std=c++23 -O2 $(WARNINGS) -ffreestanding -fno-exceptions \
	-fno-rtti -fno-stack-protector -fno-pic -fno-pie -I kernel
ARM_FLAGS := -mgeneral-regs-only
X86_FLAGS := -mno-red-zone -mgeneral-regs-only
BOOT_HEADERS := kernel/boot.hpp kernel/boot_sources.hpp
IDENTITY_SOURCE := services/identity/identity.cpp
IDENTITY_HEADER := services/identity/identity.hpp
IDENTITY_ABI := services/identity/guest_abi.hpp
DOMAIN_HEADERS := kernel/domain.hpp kernel/platform.hpp $(BOOT_HEADERS)
IDENTITY_FLAGS := -I services/identity -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-jump-tables -fno-tree-loop-distribute-patterns -fno-tree-loop-vectorize -fno-tree-slp-vectorize

.PHONY: all kernels identity-targets test identity-host-test smoke
all: kernels
kernels: build/aarch64/kernel.elf build/x86_64/kernel.elf
identity-targets: build/aarch64/identity-domain.o build/x86_64/identity-domain.o

build/aarch64/kernel.elf: kernel/arch/aarch64/layout.ld build/aarch64/start.o build/aarch64/interrupts.o build/aarch64/platform.o build/aarch64/main.o build/aarch64/memory.o build/aarch64/domain.o build/aarch64/domain_entry.o build/aarch64/identity_smoke.o build/aarch64/identity-domain.o build/aarch64/identity-client-domain.o
	$(LD_LLD) --static --no-undefined -T $< $(filter %.o,$^) -o $@

build/x86_64/kernel.elf: kernel/arch/x86_64/layout.ld build/x86_64/start.o build/x86_64/interrupts.o build/x86_64/platform.o build/x86_64/main.o build/x86_64/memory.o build/x86_64/domain.o build/x86_64/domain_entry.o build/x86_64/identity_smoke.o build/x86_64/identity-domain.o build/x86_64/identity-client-domain.o
	$(LD_LLD) --static --no-undefined -T $< $(filter %.o,$^) -o $@

build/aarch64/start.o: kernel/arch/aarch64/start.S Makefile
	mkdir -p $(@D)
	$(AARCH64_CXX) $(ARM_FLAGS) -c $< -o $@

build/x86_64/start.o: kernel/arch/x86_64/start.S kernel/arch/x86_64/apic.h Makefile
	mkdir -p $(@D)
	$(X86_64_CXX) $(X86_FLAGS) -c $< -o $@

build/aarch64/interrupts.o: kernel/arch/aarch64/interrupts.S Makefile
	mkdir -p $(@D)
	$(AARCH64_CXX) $(ARM_FLAGS) -c $< -o $@

build/x86_64/interrupts.o: kernel/arch/x86_64/interrupts.S Makefile
	mkdir -p $(@D)
	$(X86_64_CXX) $(X86_FLAGS) -c $< -o $@

build/aarch64/platform.o: kernel/arch/aarch64/platform.cpp kernel/platform.hpp $(BOOT_HEADERS) Makefile
	mkdir -p $(@D)
	$(AARCH64_CXX) $(ARM_FLAGS) $(KERNEL_FLAGS) $(AARCH64_CPPFLAGS) -c $< -o $@

build/x86_64/platform.o: kernel/arch/x86_64/platform.cpp kernel/arch/x86_64/apic.h kernel/platform.hpp $(BOOT_HEADERS) Makefile
	mkdir -p $(@D)
	$(X86_64_CXX) $(X86_FLAGS) $(KERNEL_FLAGS) $(X86_64_CPPFLAGS) -c $< -o $@

build/aarch64/main.o: kernel/main.cpp kernel/edf.hpp $(DOMAIN_HEADERS) Makefile
	mkdir -p $(@D)
	$(AARCH64_CXX) $(ARM_FLAGS) $(KERNEL_FLAGS) $(AARCH64_CPPFLAGS) -c $< -o $@

build/x86_64/main.o: kernel/main.cpp kernel/edf.hpp $(DOMAIN_HEADERS) Makefile
	mkdir -p $(@D)
	$(X86_64_CXX) $(X86_FLAGS) $(KERNEL_FLAGS) $(X86_64_CPPFLAGS) -c $< -o $@

define domain_rules
build/$(1)/memory.o: kernel/memory.cpp Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -fno-tree-loop-distribute-patterns -c $$< -o $$@

build/$(1)/domain.o: kernel/arch/$(1)/domain.cpp $(DOMAIN_HEADERS) $$(wildcard kernel/arch/$(1)/apic.h) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/domain_entry.o: kernel/arch/$(1)/domain_entry.S Makefile
	mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@

build/$(1)/identity_smoke.o: kernel/identity_smoke.cpp $(DOMAIN_HEADERS) $(IDENTITY_ABI) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity.o: $(IDENTITY_SOURCE) $(IDENTITY_HEADER) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity_guest.o: services/identity/identity_guest.cpp $(IDENTITY_HEADER) $(IDENTITY_ABI) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity_client.o: tests/identity_client.cpp $(IDENTITY_ABI) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity-domain.o: build/$(1)/identity.o build/$(1)/identity_guest.o
	$$(LD_LLD) -r $$^ -o $$@.tmp
	$$(OBJCOPY) --rename-section .text=.identity_text --rename-section .rodata=.identity_rodata $$@.tmp $$@

build/$(1)/identity-client-domain.o: build/$(1)/identity_client.o
	$$(OBJCOPY) --rename-section .text=.identity_client_text --rename-section .rodata=.identity_client_rodata $$< $$@
endef

$(eval $(call domain_rules,aarch64,$(AARCH64_CXX),$(ARM_FLAGS),$(AARCH64_CPPFLAGS)))
$(eval $(call domain_rules,x86_64,$(X86_64_CXX),$(X86_FLAGS),$(X86_64_CPPFLAGS)))

build/edf-test: tests/edf.cpp kernel/edf.hpp Makefile
	mkdir -p $(@D)
	$(HOST_CXX) -std=c++23 -O2 $(WARNINGS) -I kernel $< -o $@

build/boot-test: tests/boot.cpp $(BOOT_HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CXX) -std=c++23 -O2 $(WARNINGS) -I kernel $< -o $@

build/identity-test: tests/identity.cpp $(IDENTITY_SOURCE) $(IDENTITY_HEADER) Makefile
	mkdir -p $(@D)
	$(HOST_CXX) -std=c++23 -O2 $(WARNINGS) -I services/identity $(filter %.cpp,$^) -o $@

build/identity-host-test: tests/identity_host.cpp $(IDENTITY_SOURCE) $(IDENTITY_HEADER) \
	services/identity/host_store.cpp services/identity/host_store.hpp \
	services/identity/host_verifier.cpp services/identity/host_verifier.hpp Makefile
	mkdir -p $(@D)
	$(HOST_CXX) -std=c++23 -O2 $(WARNINGS) -I services/identity $(HOST_IDENTITY_CPPFLAGS) \
		$(filter %.cpp,$^) $(HOST_IDENTITY_LIBS) -o $@

identity-host-test: build/identity-host-test
	$(PYTHON) tests/run_identity_host.py ./build/identity-host-test

test: build/edf-test build/boot-test build/identity-test
	./build/edf-test
	./build/boot-test
	./build/identity-test
	$(PYTHON) -m unittest discover -s examples/edf -v

smoke: kernels
	$(PYTHON) tests/boot.py
