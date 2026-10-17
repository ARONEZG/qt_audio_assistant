// Compiles the miniaudio single-header library into exactly one translation
// unit.
//
// miniaudio is a third-party, header-only C library (Unlicense/MIT-0): the
// implementation is emitted only where MINIAUDIO_IMPLEMENTATION is defined
// before including <miniaudio.h>. Keeping that definition in a single
// project-owned translation unit means the ~95k-line amalgamation is compiled
// once and is not re-parsed by every consumer of the audio subsystem.
//
// The header is exposed as a SYSTEM include directory in CMakeLists.txt, so
// the strict project warning profile (-Weverything -Werror) does not flag
// diagnostics originating in third-party code. This file itself contains no
// capture logic; the audio subsystem is added on top of this target.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
