"""Boot the two development images and verify their serial self-test."""

from pathlib import Path
import subprocess
import sys
import tempfile
import time


MACHINES = {
    "aarch64": ("virt-8.2,gic-version=2", "cortex-a57", "DTB"),
    "x86_64": ("pc-i440fx-8.2", "qemu64", "PVH, E820"),
}


def boot(architecture, machine, cpu, handoff):
    kernel = Path("build") / architecture / "kernel.elf"
    command = [
        f"qemu-system-{architecture}", "-machine", machine, "-cpu", cpu,
        "-accel", "tcg", "-smp", "1", "-m", "128M", "-display", "none",
        "-monitor", "none", "-serial", "stdio", "-no-reboot", "-kernel", str(kernel),
    ]
    expected = (f"Alacre Core booted: {architecture}\n"
                f"Boot handoff v1: PASS ({handoff})\n"
                "Context EDF smoke: PASS\n"
                "Isolated identity IPC: PASS\n").encode()
    # SeaBIOS may omit the optional RSDP from the PVH record.
    accepted = (expected, expected.replace(b"E820)", b"E820, ACPI RSDP)"))
    with tempfile.TemporaryDirectory() as directory:
        console = Path(directory) / "console"
        with console.open("wb") as output, subprocess.Popen(
            command, stdout=output, stderr=subprocess.STDOUT
        ) as guest:
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    log = console.read_bytes()
                    if any(transcript in log for transcript in accepted):
                        print(log.decode(errors="replace"), end="")
                        return
                    if b": FAIL" in log or guest.poll() is not None:
                        break
                    time.sleep(0.05)
                raise RuntimeError(f"{architecture} boot check failed:\n{console.read_text(errors='replace')}")
            finally:
                if guest.poll() is None:
                    guest.kill()


if __name__ == "__main__":
    try:
        for target, (machine_type, cpu_model, handoff) in MACHINES.items():
            boot(target, machine_type, cpu_model, handoff)
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
