AARCH64_CXX ?= aarch64-linux-gnu-g++-14
X86_64_CXX ?= x86_64-linux-gnu-g++-14
LD_LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
HOST_CXX ?= c++
SAFETY_CXX ?= clang++
LLVM_PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
Z3 ?= z3
FUZZ_RUNS ?= 25000
SMOKE_RUNS ?= 1
HOST_IDENTITY_CPPFLAGS ?=
HOST_IDENTITY_LIBS ?= -lssl -lcrypto
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
DOMAIN_HEADERS := kernel/domain.hpp kernel/domain_abi.hpp kernel/platform.hpp $(BOOT_HEADERS)
IPC_HEADERS := kernel/ipc.hpp kernel/ipc_abi.hpp $(DOMAIN_HEADERS)
GUEST_FLAGS := -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-jump-tables -fno-tree-loop-distribute-patterns -fno-tree-loop-vectorize -fno-tree-slp-vectorize
IDENTITY_FLAGS := -I services/identity $(GUEST_FLAGS)
KERNEL_OBJECTS := start interrupts platform main memory domain domain_entry domain_runner \
	ipc user_smoke identity_smoke ipc_smoke user_edf_smoke identity-domain identity-client-domain \
	ipc-client-domain edf-client-domain

.PHONY: all kernels identity-targets test identity-host-test smoke safety safety-analysis safety-coverage safety-fuzz safety-invariants safety-identity-host
all: kernels
kernels: build/aarch64/kernel.elf build/x86_64/kernel.elf
identity-targets: build/aarch64/identity-domain.o build/x86_64/identity-domain.o

build/aarch64/kernel.elf: kernel/arch/aarch64/layout.ld $(addprefix build/aarch64/,$(addsuffix .o,$(KERNEL_OBJECTS)))
	$(LD_LLD) --static --no-undefined -T $< $(filter %.o,$^) -o $@

build/x86_64/kernel.elf: kernel/arch/x86_64/layout.ld $(addprefix build/x86_64/,$(addsuffix .o,$(KERNEL_OBJECTS)))
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

build/aarch64/main.o: kernel/main.cpp kernel/edf.hpp kernel/user_smoke.hpp kernel/platform.hpp $(BOOT_HEADERS) Makefile
	mkdir -p $(@D)
	$(AARCH64_CXX) $(ARM_FLAGS) $(KERNEL_FLAGS) $(AARCH64_CPPFLAGS) -c $< -o $@

build/x86_64/main.o: kernel/main.cpp kernel/edf.hpp kernel/user_smoke.hpp kernel/platform.hpp $(BOOT_HEADERS) Makefile
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

build/$(1)/domain_runner.o: kernel/domain.cpp $(IPC_HEADERS) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/ipc.o: kernel/ipc.cpp $(IPC_HEADERS) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/user_smoke.o: kernel/user_smoke.cpp kernel/user_smoke.hpp $(DOMAIN_HEADERS) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/ipc_smoke.o: kernel/ipc_smoke.cpp kernel/user_smoke.hpp $(IPC_HEADERS) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/user_edf_smoke.o: kernel/user_edf_smoke.cpp kernel/user_smoke.hpp kernel/user_edf_abi.hpp kernel/edf.hpp kernel/edf_admission.hpp $(DOMAIN_HEADERS) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) -c $$< -o $$@

build/$(1)/identity_smoke.o: kernel/identity_smoke.cpp kernel/user_smoke.hpp $(DOMAIN_HEADERS) $(IDENTITY_ABI) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity.o: $(IDENTITY_SOURCE) $(IDENTITY_HEADER) Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity_guest.o: services/identity/identity_guest.cpp $(IDENTITY_HEADER) $(IDENTITY_ABI) kernel/domain_abi.hpp Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity_client.o: tests/identity_client.cpp $(IDENTITY_ABI) kernel/domain_abi.hpp Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(IDENTITY_FLAGS) -c $$< -o $$@

build/$(1)/identity-domain.o: build/$(1)/identity.o build/$(1)/identity_guest.o
	$$(LD_LLD) -r $$^ -o $$@.tmp
	$$(OBJCOPY) --rename-section .text=.identity_text --rename-section .rodata=.identity_rodata $$@.tmp $$@

build/$(1)/identity-client-domain.o: build/$(1)/identity_client.o
	$$(OBJCOPY) --rename-section .text=.identity_client_text --rename-section .rodata=.identity_client_rodata $$< $$@

build/$(1)/ipc_client.o: tests/ipc_client.cpp kernel/ipc_abi.hpp kernel/domain_abi.hpp Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(GUEST_FLAGS) -c $$< -o $$@

build/$(1)/ipc-client-domain.o: build/$(1)/ipc_client.o
	$$(OBJCOPY) --rename-section .text=.ipc_client_text --rename-section .rodata=.ipc_client_rodata $$< $$@

build/$(1)/edf_client.o: tests/user_edf_client.cpp kernel/domain_abi.hpp kernel/user_edf_abi.hpp Makefile
	mkdir -p $$(@D)
	$(2) $(3) $$(KERNEL_FLAGS) $(4) $$(GUEST_FLAGS) -c $$< -o $$@

build/$(1)/edf-client-domain.o: build/$(1)/edf_client.o
	$$(OBJCOPY) --rename-section .text=.edf_client_text --rename-section .rodata=.edf_client_rodata $$< $$@
endef

$(eval $(call domain_rules,aarch64,$(AARCH64_CXX),$(ARM_FLAGS),$(AARCH64_CPPFLAGS)))
$(eval $(call domain_rules,x86_64,$(X86_64_CXX),$(X86_FLAGS),$(X86_64_CPPFLAGS)))

HOST_TESTS := edf-test boot-test identity-test ipc-test domain-test ipc-sequences-test
SAFETY_DIR := build/safety
SAFETY_REPORTS := $(addsuffix .coverage,$(addprefix $(SAFETY_DIR)/,$(HOST_TESTS)))
HOST_IDENTITY_REPORT := $(SAFETY_DIR)/identity-host-test.coverage
.PHONY: $(SAFETY_REPORTS) $(HOST_IDENTITY_REPORT)
HOST_FLAGS := -std=c++23 $(WARNINGS) -UNDEBUG -I kernel -I services/identity
SAFETY_FLAGS := $(HOST_FLAGS) -stdlib=libc++ -O1 -g \
	-fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
COVERAGE_FLAGS := -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc
IDENTITY_ADAPTERS := services/identity/host_store.cpp services/identity/host_verifier.cpp

define host_test_rules
build/$(1): $(2) Makefile
	mkdir -p $$(@D)
	$$(HOST_CXX) $$(HOST_FLAGS) -O2 $(3) $$(filter %.cpp,$$^) $(4) -o $$@

$$(SAFETY_DIR)/$(1): $(2) Makefile
	mkdir -p $$(@D)
	$$(SAFETY_CXX) $$(SAFETY_FLAGS) $$(COVERAGE_FLAGS) $(3) $$(filter %.cpp,$$^) $(4) -o $$@

$$(SAFETY_DIR)/$(1).coverage: $$(SAFETY_DIR)/$(1)
	rm -f $$<.profraw
	LLVM_PROFILE_FILE=$$<.profraw $(if $(filter identity-host-test,$(1)),$$(PYTHON) tests/run_identity_host.py) ./$$<
	$$(LLVM_PROFDATA) merge -sparse $$<.profraw -o $$<.profdata
	{ printf '\n%s\n' $$<; $$(LLVM_COV) report $$< -instr-profile=$$<.profdata \
		-show-branch-summary -show-mcdc-summary $(5); } > $$@
endef

$(eval $(call host_test_rules,edf-test,tests/edf.cpp kernel/edf.hpp kernel/edf_admission.hpp,,,kernel/edf.hpp))
$(eval $(call host_test_rules,boot-test,tests/boot.cpp $(BOOT_HEADERS),,,$(BOOT_HEADERS)))
$(eval $(call host_test_rules,identity-test,tests/identity.cpp $(IDENTITY_SOURCE) $(IDENTITY_HEADER),,,$(IDENTITY_SOURCE)))
$(eval $(call host_test_rules,ipc-test,tests/ipc.cpp kernel/ipc.cpp $(IPC_HEADERS),,,kernel/ipc.cpp))
$(eval $(call host_test_rules,domain-test,tests/domain.cpp $(DOMAIN_HEADERS),,,kernel/domain.hpp))
$(eval $(call host_test_rules,ipc-sequences-test,tests/ipc_sequences.cpp kernel/ipc.cpp $(IPC_HEADERS),,,kernel/ipc.cpp))
$(eval $(call host_test_rules,identity-host-test,tests/identity_host.cpp $(IDENTITY_SOURCE) $(IDENTITY_HEADER) \
	$(IDENTITY_ADAPTERS) services/identity/host_store.hpp services/identity/host_verifier.hpp, \
	$(HOST_IDENTITY_CPPFLAGS),$(HOST_IDENTITY_LIBS),$(IDENTITY_SOURCE) $(IDENTITY_ADAPTERS)))

identity-host-test: build/identity-host-test
	$(PYTHON) tests/run_identity_host.py ./build/identity-host-test

test: $(addprefix build/,$(HOST_TESTS))
	set -e; for test in $^; do ./$$test; done
	$(PYTHON) -m unittest discover -s examples/edf -v

smoke: kernels
	$(PYTHON) tests/boot.py --runs $(SMOKE_RUNS)

safety: safety-analysis safety-coverage safety-fuzz safety-invariants

safety-analysis:
	set -e; for src in kernel/domain.cpp kernel/ipc.cpp $(IDENTITY_SOURCE) tests/boot.cpp tests/edf.cpp tests/domain.cpp; do \
		$(SAFETY_CXX) --analyze -Xanalyzer -analyzer-werror -std=c++23 -stdlib=libc++ $(WARNINGS) \
			-I kernel -I services/identity "$$src" -o /dev/null; \
	done

safety-identity-host: $(HOST_IDENTITY_REPORT)
	set -e; for src in $(IDENTITY_ADAPTERS); do \
		$(SAFETY_CXX) --analyze -Xanalyzer -analyzer-werror -std=c++23 -stdlib=libc++ $(WARNINGS) \
			-I services/identity $(HOST_IDENTITY_CPPFLAGS) "$$src" -o /dev/null; \
	done
	cat $<

safety-coverage: $(SAFETY_REPORTS)
	cat $^ > $(SAFETY_DIR)/coverage.txt
	cat $(SAFETY_DIR)/coverage.txt

$(SAFETY_DIR)/ipc-fuzz: tests/ipc_sequences.cpp kernel/ipc.cpp $(IPC_HEADERS) Makefile
	mkdir -p $(@D)
	$(SAFETY_CXX) $(SAFETY_FLAGS) -fsanitize=fuzzer -DALACRE_FUZZ $(filter %.cpp,$^) -o $@

safety-fuzz: $(SAFETY_DIR)/ipc-fuzz
	./$< -runs=$(FUZZ_RUNS) -seed=1 -max_len=128 -timeout=5 -artifact_prefix=$(SAFETY_DIR)/

safety-invariants: tests/safety_invariants.smt2
	mkdir -p $(SAFETY_DIR)
	$(Z3) -T:10 $< > $(SAFETY_DIR)/invariants.txt
	cat $(SAFETY_DIR)/invariants.txt
	printf '%s\n' unsat unsat unsat unsat | cmp - $(SAFETY_DIR)/invariants.txt
