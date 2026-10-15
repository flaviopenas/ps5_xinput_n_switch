"""Prepare a checked local GPU-read helper from one pinned upstream revision.

Does not contact the console or modify the reference checkout.
"""
from pathlib import Path
import argparse
import subprocess

REVISION = "699870d16ca4cd5cd70fc88f8cf61b3953d4a292"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=Path("testing_tools/.tools/ps5-hen-reference"))
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/bt_gpu_reference.cpp"))
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "-C", str(args.reference), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        parser.error("Unreviewed reference revision; refusing to prepare GPU helper")
    source = subprocess.check_output(["git", "-C", str(args.reference), "show", "HEAD:gpu_dma.cpp"], text=True)
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            parser.error("Reference source contract changed")
        source = source.replace(old, new)
    # Failed submission must restore the payload's own remapped GPU entry.
    replace('print("[gpu] ioctl submit failed: %d\\n", ret);\n        return -1;',
            'print("[gpu] ioctl submit failed: %d\\n", ret);\n'
            '        kernel_setlong(s_gpu.victim_ptbe_va, s_gpu.cleared_ptbe | s_gpu.victim_real_pa);\n'
            '        return -1;')
    replace('kernel_setlong(s_gpu.victim_ptbe_va, new_ptbe);',
            'if (kernel_setlong(s_gpu.victim_ptbe_va, new_ptbe)) return -1;')
    replace('kernel_setlong(s_gpu.victim_ptbe_va, orig_ptbe);',
            'if (kernel_setlong(s_gpu.victim_ptbe_va, orig_ptbe)) return -1;')
    replace('print("[gpu] WARNING: page size 0x%lx != dmem_size 0x%lx\\n",\n'
            '                 s_gpu.page_size, s_gpu.dmem_size);',
            'print("[gpu] incompatible GPU page size\\n"); return -4;')
    # Original direct-memory allocations lack release tracking. Track all
    # three and release them, including an allocation whose mapping failed.
    replace('static struct gpu_ctx s_gpu = {0};',
            'static struct gpu_ctx s_gpu = {0};\n'
            'static uint64_t allocated_phys[3], allocated_va[3];\n'
            'static unsigned allocated_count;\n'
            'extern "C" int sceKernelReleaseDirectMemory(uint64_t, size_t);')
    replace('ret = sceKernelMapNamedDirectMemory(&va_out, size, prot,',
            'if (allocated_count >= 3) { sceKernelReleaseDirectMemory(phys_out, size); return 0; }\n'
            '    unsigned slot = allocated_count++; allocated_phys[slot] = phys_out;\n'
            '    ret = sceKernelMapNamedDirectMemory(&va_out, size, prot,')
    replace('return (uint64_t)va_out;',
            'allocated_va[slot] = (uint64_t)va_out;\n    return (uint64_t)va_out;')
    replace('s_gpu.initialized = 0;\n    print("[gpu] Cleaned up\\n");',
            'for (unsigned i = 0; i < allocated_count; ++i) {\n'
            '        if (allocated_va[i]) munmap((void*)allocated_va[i], s_gpu.dmem_size);\n'
            '        sceKernelReleaseDirectMemory(allocated_phys[i], s_gpu.dmem_size);\n'
            '    }\n    allocated_count = 0;\n'
            '    s_gpu.initialized = 0;\n    print("[gpu] Cleaned up\\n");')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source)
    print(f"Prepared pinned GPU helper: {args.output}")

if __name__ == "__main__":
    main()
