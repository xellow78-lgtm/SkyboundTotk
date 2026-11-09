# compiler-rt's profile runtime

The source files of LLVM compiler-rt's `lib/profile` (and `include/profile`) that an instrumented Windows or Linux
build needs, from llvm-project commit `510126255f89d693717c1ce7105b593f995f07c1` (2026-08-31): the commit of the LLVM
that emsdk 6.0.9 ships (`clang.exe`'s version string), which is the build's compiler. emsdk's LLVM has no
`clang_rt.profile` library, so `a64-kit/build.py` compiles these files into the game when `RX_PGO=gen`
(see the README's "Profile-guided build, instrumented").

The runtime and the compiler have to agree on the raw profile format (`INSTR_PROF_RAW_VERSION` in
`include/profile/InstrProfData.inc`, 11 here): when emsdk's version changes, take these files from the commit its
`clang --version` names.

Unchanged except for what is left out: `InstrProfilingVersionVar.c` (every instrumented object defines that variable
itself, with the IR-level flag; on Windows the runtime's weak copy could be the one the linker keeps), GCDA (gcov),
and the AIX, Darwin, Fuchsia, GPU, ROCm and "other" platform files.

License: Apache License v2.0 with LLVM Exceptions (`LICENSE.TXT`).
