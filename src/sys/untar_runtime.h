// compress.untar / compress.unar (spec/33_2 §3.7a).
#pragma once
#include <cstdint>

extern "C" {
// Any tar (plain, gzip, xz, zstd) below dst_dir: 0 ok, -1 args, -2 open,
// -3 corrupt/truncated, -4 unsafe entry, -5 write failure.
int32_t aiml_compress_untar(const char* src, const char* dst_dir);
// The members of an ar archive (a .deb) into dst_dir; same codes.
int32_t aiml_compress_unar(const char* src, const char* dst_dir);
}
