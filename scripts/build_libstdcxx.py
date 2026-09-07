#!/usr/bin/env python3
"""
SzpontOS - GNU libstdc++-v3 Out-of-Tree Build Script
(C) Copyright by Szpont Industries. All rights reserved.

Compiles libsupc++ and libstdc++-v3 into libstdc++.so.6 and libstdc++.a
without touching any files inside the third_party/libstdc++ submodule.
"""

import os
import sys
import shutil
import subprocess
import multiprocessing

UNWIND_PE_H = """/*
 * SzpontOS - DWARF Exception Handling Pointer Encoding (unwind-pe.h)
 * Out-of-tree runtime support for GNU libsupc++ / libstdc++-v3
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _UNWIND_PE_H
#define _UNWIND_PE_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define DW_EH_PE_absptr   0x00
#define DW_EH_PE_omit     0xff

#define DW_EH_PE_uleb128  0x01
#define DW_EH_PE_udata2   0x02
#define DW_EH_PE_udata4   0x03
#define DW_EH_PE_udata8   0x04
#define DW_EH_PE_sleb128  0x09
#define DW_EH_PE_sdata2   0x0A
#define DW_EH_PE_sdata4   0x0B
#define DW_EH_PE_sdata8   0x0C
#define DW_EH_PE_signed   0x08

#define DW_EH_PE_pcrel    0x10
#define DW_EH_PE_textrel  0x20
#define DW_EH_PE_datarel  0x30
#define DW_EH_PE_funcrel  0x40
#define DW_EH_PE_aligned  0x50
#define DW_EH_PE_indirect 0x80

static inline unsigned int size_of_encoded_value(unsigned char encoding)
{
  if (encoding == DW_EH_PE_omit)
    return 0;

  switch (encoding & 0x07)
    {
    case 0:
      return sizeof(void *);
    case 2:
      return 2;
    case 3:
      return 4;
    case 4:
      return 8;
    default:
      abort();
    }
}

static inline const unsigned char *
read_uleb128(const unsigned char *p, _uleb128_t *val)
{
  unsigned int shift = 0;
  unsigned char byte;
  _uleb128_t result = 0;

  do
    {
      byte = *p++;
      result |= ((_uleb128_t)(byte & 0x7f)) << shift;
      shift += 7;
    }
  while (byte & 0x80);

  *val = result;
  return p;
}

static inline const unsigned char *
read_sleb128(const unsigned char *p, _sleb128_t *val)
{
  unsigned int shift = 0;
  unsigned char byte;
  _uleb128_t result = 0;

  do
    {
      byte = *p++;
      result |= ((_uleb128_t)(byte & 0x7f)) << shift;
      shift += 7;
    }
  while (byte & 0x80);

  if (shift < sizeof(_sleb128_t) * 8 && (byte & 0x40))
    result |= -(((_uleb128_t)1) << shift);

  *val = (_sleb128_t)result;
  return p;
}

static inline _Unwind_Ptr
base_of_encoded_value(unsigned char encoding __attribute__((unused)),
		      struct _Unwind_Context *context __attribute__((unused)))
{
  if (encoding == DW_EH_PE_omit)
    return 0;

  switch (encoding & 0x70)
    {
    case DW_EH_PE_absptr:
    case DW_EH_PE_pcrel:
    case DW_EH_PE_aligned:
      return 0;

    case DW_EH_PE_textrel:
      return _Unwind_GetTextRelBase(context);
    case DW_EH_PE_datarel:
      return _Unwind_GetDataRelBase(context);
    case DW_EH_PE_funcrel:
      return _Unwind_GetRegionStart(context);
    default:
      abort();
    }
}

static inline const unsigned char *
read_encoded_value_with_base(unsigned char encoding, _Unwind_Ptr base,
			     const unsigned char *p, _Unwind_Ptr *val)
{
  union unaligned
    {
      void *ptr;
      unsigned short u2 __attribute__((mode(HI)));
      unsigned int u4 __attribute__((mode(SI)));
      unsigned long u8 __attribute__((mode(DI)));
      signed short s2 __attribute__((mode(HI)));
      signed int s4 __attribute__((mode(SI)));
      signed long s8 __attribute__((mode(DI)));
    } __attribute__((packed));

  const union unaligned *u = (const union unaligned *)p;
  _Unwind_Internal_Ptr result;

  if (encoding == DW_EH_PE_aligned)
    {
      _Unwind_Internal_Ptr a = (_Unwind_Internal_Ptr)p;
      a = (a + sizeof(void *) - 1) & - sizeof(void *);
      result = *(_Unwind_Internal_Ptr *)a;
      p = (const unsigned char *)(a + sizeof(void *));
    }
  else
    {
      switch (encoding & 0x0f)
	{
	case DW_EH_PE_absptr:
	  result = (_Unwind_Internal_Ptr)u->ptr;
	  p += sizeof(void *);
	  break;

	case DW_EH_PE_uleb128:
	  {
	    _uleb128_t tmp;
	    p = read_uleb128(p, &tmp);
	    result = (_Unwind_Internal_Ptr)tmp;
	  }
	  break;

	case DW_EH_PE_sleb128:
	  {
	    _sleb128_t tmp;
	    p = read_sleb128(p, &tmp);
	    result = (_Unwind_Internal_Ptr)tmp;
	  }
	  break;

	case DW_EH_PE_udata2:
	  result = u->u2;
	  p += 2;
	  break;
	case DW_EH_PE_udata4:
	  result = u->u4;
	  p += 4;
	  break;
	case DW_EH_PE_udata8:
	  result = u->u8;
	  p += 8;
	  break;

	case DW_EH_PE_sdata2:
	  result = u->s2;
	  p += 2;
	  break;
	case DW_EH_PE_sdata4:
	  result = u->s4;
	  p += 4;
	  break;
	case DW_EH_PE_sdata8:
	  result = u->s8;
	  p += 8;
	  break;

	default:
	  abort();
	}

      if (result != 0)
	{
	  if ((encoding & 0x70) == DW_EH_PE_pcrel)
	    result += (_Unwind_Internal_Ptr)u;
	  else
	    result += base;

	  if (encoding & DW_EH_PE_indirect)
	    result = *(_Unwind_Internal_Ptr *)result;
	}
    }

  *val = result;
  return p;
}

static inline const unsigned char *
read_encoded_value(struct _Unwind_Context *context, unsigned char encoding,
		   const unsigned char *p, _Unwind_Ptr *val)
{
  return read_encoded_value_with_base(encoding,
		base_of_encoded_value(encoding, context),
		p, val);
}

#endif /* _UNWIND_PE_H */
"""

CXX_STUBS_CC = """/*
 * SzpontOS - Out-of-tree C++ ABI Support Stubs (cxx_stubs.cc)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <cstdlib>
#include <exception>
#include <string>

extern "C" {

char* __cxa_demangle(const char* mangled_name, char* output_buffer, size_t* length, int* status) {
    (void)mangled_name;
    (void)output_buffer;
    (void)length;
    if (status) *status = -2;
    return nullptr;
}

extern const char __libstdcxx_eh_frame_begin[];
void __register_frame(const void *);

}

namespace {
    struct LibstdcxxFrameRegistrar {
        LibstdcxxFrameRegistrar() {
            if (__libstdcxx_eh_frame_begin) {
                __register_frame(__libstdcxx_eh_frame_begin);
            }
        }
    } g_libstdcxx_frame_registrar;
}

namespace std {
_GLIBCXX_PURE bool
__verify_grouping_impl(const char* __grouping, size_t __grouping_size,
                       const char* __grouping_tmp, size_t __n);

bool
__verify_grouping(const char* __grouping, size_t __grouping_size,
                  const string& __grouping_tmp) throw()
{
    return __verify_grouping_impl(__grouping, __grouping_size,
                                  __grouping_tmp.c_str(),
                                  __grouping_tmp.size());
}
}
"""

def compile_worker(args):
    cmd, rel = args
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        return (False, rel, res.stderr)
    return (True, rel, "")

def main():
    if len(sys.argv) < 5:
        print("Usage: build_libstdcxx.py <src_dir> <build_dir> <sysroot_dir> <rootfs_dir>")
        sys.exit(1)

    src_dir = os.path.abspath(sys.argv[1])
    build_dir = os.path.abspath(sys.argv[2])
    sysroot_dir = os.path.abspath(sys.argv[3])
    rootfs_dir = os.path.abspath(sys.argv[4])

    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(os.path.join(sysroot_dir, "usr", "lib"), exist_ok=True)
    os.makedirs(os.path.join(rootfs_dir, "lib"), exist_ok=True)

    # 1. Install headers
    script_dir = os.path.dirname(os.path.abspath(__file__))
    install_script = os.path.join(script_dir, "install_libstdcxx_headers.py")
    include_cxx = os.path.join(sysroot_dir, "usr", "include", "c++")
    
    print("  [CXX-HEADERS] Installing libstdc++ headers into sysroot...")
    subprocess.check_call([sys.executable, install_script, src_dir, include_cxx])

    # 2. Out-of-tree generated helpers
    unwind_pe = os.path.join(build_dir, "unwind-pe.h")
    with open(unwind_pe, "w") as f:
        f.write(UNWIND_PE_H)

    cxx_stubs = os.path.join(build_dir, "cxx_stubs.cc")
    with open(cxx_stubs, "w") as f:
        f.write(CXX_STUBS_CC)

    # 3. Source lists
    sources_gnu17 = [
        # libsupc++
        "libsupc++/del_op.cc",
        "libsupc++/del_ops.cc",
        "libsupc++/del_opnt.cc",
        "libsupc++/del_opv.cc",
        "libsupc++/del_opvs.cc",
        "libsupc++/del_opvnt.cc",
        "libsupc++/new_op.cc",
        "libsupc++/new_opnt.cc",
        "libsupc++/new_opv.cc",
        "libsupc++/new_opvnt.cc",
        "libsupc++/bad_alloc.cc",
        "libsupc++/bad_array_length.cc",
        "libsupc++/bad_array_new.cc",
        "libsupc++/bad_cast.cc",
        "libsupc++/bad_typeid.cc",
        "libsupc++/pure.cc",
        "libsupc++/guard.cc",
        "libsupc++/tinfo.cc",
        "libsupc++/class_type_info.cc",
        "libsupc++/si_class_type_info.cc",
        "libsupc++/vmi_class_type_info.cc",
        "libsupc++/dyncast.cc",
        "libsupc++/eh_exception.cc",
        "libsupc++/eh_terminate.cc",
        "libsupc++/eh_catch.cc",
        "libsupc++/eh_throw.cc",
        "libsupc++/eh_alloc.cc",
        "libsupc++/eh_globals.cc",
        "libsupc++/hash_bytes.cc",
        "libsupc++/nested_exception.cc",
        "libsupc++/atexit_thread.cc",
        "libsupc++/array_type_info.cc",
        "libsupc++/del_opa.cc",
        "libsupc++/del_opant.cc",
        "libsupc++/del_opsa.cc",
        "libsupc++/del_opva.cc",
        "libsupc++/del_opvant.cc",
        "libsupc++/del_opvsa.cc",
        "libsupc++/eh_aux_runtime.cc",
        "libsupc++/eh_call.cc",
        "libsupc++/eh_personality.cc",
        "libsupc++/eh_ptr.cc",
        "libsupc++/eh_term_handler.cc",
        "libsupc++/eh_tm.cc",
        "libsupc++/eh_type.cc",
        "libsupc++/eh_unex_handler.cc",
        "libsupc++/enum_type_info.cc",
        "libsupc++/function_type_info.cc",
        "libsupc++/fundamental_type_info.cc",
        "libsupc++/guard_error.cc",
        "libsupc++/new_handler.cc",
        "libsupc++/new_opa.cc",
        "libsupc++/new_opant.cc",
        "libsupc++/new_opva.cc",
        "libsupc++/new_opvant.cc",
        "libsupc++/pbase_type_info.cc",
        "libsupc++/pmem_type_info.cc",
        "libsupc++/pointer_type_info.cc",
        "libsupc++/tinfo2.cc",
        "libsupc++/vec.cc",
        "libsupc++/vterminate.cc",

        # config
        "config/locale/generic/c_locale.cc",
        "config/locale/generic/codecvt_members.cc",
        "config/locale/generic/ctype_members.cc",
        "config/locale/generic/time_members.cc",
        "config/os/generic/ctype_configure_char.cc",
        "config/io/basic_file_stdio.cc",

        # src/c++98
        "src/c++98/ios_init.cc",
        "src/c++98/globals_io.cc",
        "src/c++98/locale.cc",
        "src/c++98/locale_facets.cc",
        "src/c++98/codecvt.cc",
        "src/c++98/complex_io.cc",
        "src/c++98/valarray.cc",
        "src/c++98/tree.cc",
        "src/c++98/list.cc",
        "src/c++98/list-aux.cc",
        "src/c++98/list-aux-2.cc",
        "src/c++98/pool_allocator.cc",
        "src/c++98/mt_allocator.cc",
        "src/c++98/stdexcept.cc",
        "src/c++98/hash_tr1.cc",
        "src/c++98/hashtable_tr1.cc",
        "src/c++98/strstream.cc",
        "src/c++98/istream.cc",
        "src/c++98/streambuf.cc",
        "src/c++98/allocator-inst.cc",
        "src/c++98/concept-inst.cc",
        "src/c++98/ext-inst.cc",
        "src/c++98/compatibility.cc",

        # src/c++11
        "src/c++11/ios.cc",
        "src/c++11/ios_errcat.cc",
        "src/c++11/ios-inst.cc",
        "src/c++11/iostream-inst.cc",
        "src/c++11/ostream-inst.cc",
        "src/c++11/istream-inst.cc",
        "src/c++11/streambuf-inst.cc",
        "src/c++11/string-inst.cc",
        "src/c++11/string-io-inst.cc",
        "src/c++11/wstring-inst.cc",
        "src/c++11/wstring-io-inst.cc",
        "src/c++11/ctype.cc",
        "src/c++11/functexcept.cc",
        "src/c++11/snprintf_lite.cc",
        "src/c++11/chrono.cc",
        "src/c++11/condition_variable.cc",
        "src/c++11/mutex.cc",
        "src/c++11/thread.cc",
        "src/c++11/future.cc",
        "src/c++11/functional.cc",
        "src/c++11/shared_ptr.cc",
        "src/c++11/system_error.cc",
        "src/c++11/random.cc",
        "src/c++11/regex.cc",
        "src/c++11/hash_c++0x.cc",
        "src/c++11/hashtable_c++0x.cc",
        "src/c++11/assert_fail.cc",
        "src/c++11/fstream-inst.cc",
        "src/c++11/sstream-inst.cc",
        "src/c++11/codecvt.cc",
        "src/c++11/cow-stdexcept.cc",
        "src/c++11/ext11-inst.cc",
        "src/c++11/cow-string-inst.cc",
        "src/c++11/cow-string-io-inst.cc",
        "src/c++11/cow-wstring-inst.cc",
        "src/c++11/cow-wstring-io-inst.cc",
        "src/c++11/cow-shim_facets.cc",
        "src/c++11/cow-fstream-inst.cc",
        "src/c++11/cow-sstream-inst.cc",
    ]

    sources_gnu17_abi0 = [
        "src/c++11/locale-inst.cc",
        "src/c++11/wlocale-inst.cc",
        "config/locale/generic/collate_members.cc",
        "config/locale/generic/messages_members.cc",
        "config/locale/generic/monetary_members.cc",
        "config/locale/generic/numeric_members.cc",
        "src/c++98/istream-string.cc",
    ]

    sources_gnu17_abi1 = [
        "src/c++11/cxx11-locale-inst.cc",
        "src/c++11/cxx11-wlocale-inst.cc",
        "src/c++11/cxx11-ios_failure.cc",
        "src/c++11/cxx11-stdexcept.cc",
        "src/c++11/cxx11-shim_facets.cc",
        "src/c++11/cxx11-hash_tr1.cc",
        "src/c++11/sso_string.cc",
        "config/locale/generic/collate_members.cc",
        "config/locale/generic/messages_members.cc",
        "config/locale/generic/monetary_members.cc",
        "config/locale/generic/numeric_members.cc",
        "src/c++98/istream-string.cc",
    ]

    sources_gnu11 = [
        "src/c++11/locale_init.cc",
        "src/c++11/localename.cc",
        "src/c++11/limits.cc",
        "src/c++11/cow-locale_init.cc",
    ]

    sources_gnu98 = [
        "src/c++98/bitmap_allocator.cc",
        "src/c++98/ios_locale.cc",
        "src/c++98/ios_failure.cc",
    ]

    base_cxx_flags = [
        "-c", "-fPIC", "-O2", "-ffreestanding", "-nostdinc++",
        f"-isystem{sysroot_dir}/usr/include/c++",
        f"-isystem{sysroot_dir}/usr/include/c++/bits",
        f"-isystem{sysroot_dir}/usr/include/c++/backward",
        f"-isystem{sysroot_dir}/usr/include/c++/ext",
        f"-isystem{sysroot_dir}/usr/include",
        f"-I{src_dir}/include",
        f"-I{src_dir}/libsupc++",
        f"-I{build_dir}",
        "-D_GLIBCXX_SHARED",
        "-D_GLIBCXX_HOSTED=1",
    ]

    # Dso handle for shared library
    dso_src = os.path.join(build_dir, "dso_handle.c")
    with open(dso_src, "w") as f:
        f.write('__attribute__((visibility("hidden"))) void *__dso_handle = &__dso_handle;\n')

    dso_obj = os.path.join(build_dir, "dso_handle.o")
    subprocess.check_call([
        "x86_64-elf-gcc", "-c", "-fPIC", "-O2", "-ffreestanding",
        f"-isystem{sysroot_dir}/usr/include", dso_src, "-o", dso_obj
    ])

    tasks = []
    all_objs = [dso_obj]

    # Out-of-tree cxx_stubs
    stubs_obj = os.path.join(build_dir, "cxx_stubs.o")
    all_objs.append(stubs_obj)
    tasks.append((["x86_64-elf-g++", "-std=gnu++17"] + base_cxx_flags + [cxx_stubs, "-o", stubs_obj], "cxx_stubs.cc"))

    for rel in sources_gnu17:
        full = os.path.join(src_dir, rel)
        obj_name = rel.replace("/", "_").replace(".cc", ".o")
        out_obj = os.path.join(build_dir, obj_name)
        all_objs.append(out_obj)
        cmd = ["x86_64-elf-g++", "-std=gnu++17"] + base_cxx_flags + [full, "-o", out_obj]
        tasks.append((cmd, rel))

    for rel in sources_gnu17_abi0:
        full = os.path.join(src_dir, rel)
        obj_name = rel.replace("/", "_").replace(".cc", "_abi0.o")
        out_obj = os.path.join(build_dir, obj_name)
        all_objs.append(out_obj)
        cmd = ["x86_64-elf-g++", "-std=gnu++17", "-D_GLIBCXX_USE_CXX11_ABI=0"] + base_cxx_flags + [full, "-o", out_obj]
        tasks.append((cmd, rel))

    for rel in sources_gnu17_abi1:
        full = os.path.join(src_dir, rel)
        obj_name = rel.replace("/", "_").replace(".cc", "_abi1.o")
        out_obj = os.path.join(build_dir, obj_name)
        all_objs.append(out_obj)
        cmd = ["x86_64-elf-g++", "-std=gnu++17", "-D_GLIBCXX_USE_CXX11_ABI=1"] + base_cxx_flags + [full, "-o", out_obj]
        tasks.append((cmd, rel))

    for rel in sources_gnu11:
        full = os.path.join(src_dir, rel)
        obj_name = rel.replace("/", "_").replace(".cc", ".o")
        out_obj = os.path.join(build_dir, obj_name)
        all_objs.append(out_obj)
        cmd = ["x86_64-elf-g++", "-std=gnu++11"] + base_cxx_flags + [full, "-o", out_obj]
        tasks.append((cmd, rel))

    for rel in sources_gnu98:
        full = os.path.join(src_dir, rel)
        obj_name = rel.replace("/", "_").replace(".cc", ".o")
        out_obj = os.path.join(build_dir, obj_name)
        all_objs.append(out_obj)
        cmd = ["x86_64-elf-g++", "-std=gnu++98"] + base_cxx_flags + [full, "-o", out_obj]
        tasks.append((cmd, rel))

    print(f"  [MAKE-LIBSTDCXX] Compiling {len(tasks)} libstdc++ translation units...")
    num_cpus = os.cpu_count() or 4
    with multiprocessing.Pool(processes=num_cpus) as pool:
        results = pool.map(compile_worker, tasks)

    failed = [r for r in results if not r[0]]
    if failed:
        for _, rel, err in failed[:5]:
            print(f"ERROR in {rel}:\n{err}")
        print(f"Compilation failed for {len(failed)} files.")
        sys.exit(1)

    # 4. Extract and unhide symbols from libgcc.a unwind routines
    libgcc = subprocess.check_output(["x86_64-elf-gcc", "-print-file-name=libgcc.a"]).decode().strip()
    unhide_dir = os.path.join(build_dir, "libgcc_unhide")
    os.makedirs(unhide_dir, exist_ok=True)
    unhide_objs = []
    for member in ["unwind-dw2.o", "unwind-dw2-fde.o", "unwind-c.o", "emutls.o"]:
        out_obj = os.path.join(unhide_dir, member)
        subprocess.check_call(["x86_64-elf-ar", "p", libgcc, member], stdout=open(out_obj, "wb"))
        with open(out_obj, "rb") as f:
            data = bytearray(f.read())
        import struct
        shoff = struct.unpack("<Q", data[40:48])[0]
        shentsize = struct.unpack("<H", data[58:60])[0]
        shnum = struct.unpack("<H", data[60:62])[0]
        for i in range(shnum):
            sec = shoff + i * shentsize
            sh_type = struct.unpack("<I", data[sec+4:sec+8])[0]
            if sh_type == 2:  # SHT_SYMTAB
                sh_offset = struct.unpack("<Q", data[sec+24:sec+32])[0]
                sh_size = struct.unpack("<Q", data[sec+32:sec+40])[0]
                sh_entsize = struct.unpack("<Q", data[sec+56:sec+64])[0]
                for s in range(sh_size // sh_entsize):
                    sym_off = sh_offset + s * sh_entsize
                    st_other = data[sym_off + 5]
                    if (st_other & 3) == 2:  # STV_HIDDEN
                        data[sym_off + 5] = st_other & ~3
        with open(out_obj, "wb") as f:
            f.write(data)
        unhide_objs.append(out_obj)

    # Generate .eh_frame begin and end boundary markers
    eh_begin_asm = os.path.join(build_dir, "eh_begin.asm")
    with open(eh_begin_asm, "w") as f:
        f.write("[bits 64]\nsection .eh_frame\nglobal __libstdcxx_eh_frame_begin:data hidden\n__libstdcxx_eh_frame_begin:\n")
    eh_begin_obj = os.path.join(build_dir, "eh_begin.o")
    subprocess.check_call(["nasm", "-f", "elf64", eh_begin_asm, "-o", eh_begin_obj])

    eh_end_asm = os.path.join(build_dir, "eh_end.asm")
    with open(eh_end_asm, "w") as f:
        f.write("[bits 64]\nsection .eh_frame\nglobal __libstdcxx_eh_frame_end:data hidden\n__libstdcxx_eh_frame_end:\n    dd 0\n")
    eh_end_obj = os.path.join(build_dir, "eh_end.o")
    subprocess.check_call(["nasm", "-f", "elf64", eh_end_asm, "-o", eh_end_obj])

    link_objs = [eh_begin_obj] + all_objs + unhide_objs + [eh_end_obj]

    # 5. Link libstdc++.so.6
    so_target = os.path.join(sysroot_dir, "usr", "lib", "libstdc++.so.6")
    so_link = os.path.join(sysroot_dir, "usr", "lib", "libstdc++.so")
    a_target = os.path.join(sysroot_dir, "usr", "lib", "libstdc++.a")

    print("  [LD-LIBSTDCXX] Linking libstdc++.so.6...")
    ld_cmd = [
        "x86_64-elf-ld",
        "-shared",
        "-soname", "libstdc++.so.6",
        "-o", so_target,
    ] + link_objs + [
        f"-L{sysroot_dir}/usr/lib",
        "-lc", "-lm", libgcc,
        "--allow-shlib-undefined"
    ]
    subprocess.check_call(ld_cmd)

    if os.path.islink(so_link) or os.path.exists(so_link):
        os.remove(so_link)
    os.symlink("libstdc++.so.6", so_link)

    # 6. Create static archive libstdc++.a
    print("  [AR-LIBSTDCXX] Creating libstdc++.a...")
    if os.path.exists(a_target):
        os.remove(a_target)
    subprocess.check_call(["x86_64-elf-ar", "rcs", a_target] + link_objs)

    # 7. Copy to rootfs
    shutil.copy2(so_target, os.path.join(rootfs_dir, "lib", "libstdc++.so.6"))
    rootfs_so_link = os.path.join(rootfs_dir, "lib", "libstdc++.so")
    if os.path.islink(rootfs_so_link) or os.path.exists(rootfs_so_link):
        os.remove(rootfs_so_link)
    os.symlink("libstdc++.so.6", rootfs_so_link)
    shutil.copy2(a_target, os.path.join(rootfs_dir, "lib", "libstdc++.a"))

    print("  [DONE] GNU libstdc++-v3 successfully built and installed.")

if __name__ == "__main__":
    main()
