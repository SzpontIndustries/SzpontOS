#!/usr/bin/env python3
"""Run actual kernel code with simulated RAM/ports; no privileged instructions."""
import pathlib
import platform
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
gc_flag = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
with tempfile.TemporaryDirectory(prefix="szpontos-regression-") as tmp:
    for name in ("vmm", "ps2"):
        binary = pathlib.Path(tmp) / name
        subprocess.run([
            "cc", "-std=c17", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
            "-fno-builtin", gc_flag, "-I", str(ROOT / "kernel/include"),
            "-I", str(ROOT), str(ROOT / f"scripts/tests/{name}_regression.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)

