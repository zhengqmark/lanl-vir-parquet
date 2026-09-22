/*
 * Copyright (c) 2026 Triad National Security, LLC, as operator of Los Alamos
 * National Laboratory with the U.S. Department of Energy/National Nuclear
 * Security Administration. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of TRIAD, Los Alamos National Laboratory, LANL, the
 *    U.S. Government, nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "base64.h"

#include <vtkBase64Utilities.h>

Base64Reader::Base64Reader(RandomAccessFile* base, uint64_t start)
    : base_(base), start_(start) {}

Base64Reader::~Base64Reader() {}

int Base64Reader::Decode4(unsigned char& c0, unsigned char& c1,
                          unsigned char& c2, uint64_t offset) {
  unsigned char in[4];
  int64_t nr = base_->Pread(in, 4, start_ + offset);
  if (nr != 4) {
    return -1;
  }

  return vtkBase64Utilities::DecodeTriplet(in[0], in[1], in[2], in[3], &c0, &c1,
                                           &c2);
}

int64_t Base64Reader::Pread(void* buf, uint64_t size, uint64_t offset) {
  return PreadTyped(static_cast<unsigned char*>(buf), size, offset);
}

int64_t Base64Reader::PreadTyped(unsigned char* buf, uint64_t size,
                                 uint64_t offset) {
  unsigned char* p = buf;
  unsigned char* end = p + size;

  uint64_t base64_offset = offset / 3 * 4;
  int skip = offset % 3;

  while (p != end) {
    unsigned char c[3];
    int r = Decode4(c[0], c[1], c[2], base64_offset);
    if (r == -1) {
      return -1;
    } else if (r <= skip) {
      break;
    }

    uint64_t available = r - skip;
    uint64_t needed = end - p;
    uint64_t n = std::min(available, needed);
    memcpy(p, c + skip, n);

    p += n;
    base64_offset += 4;
    skip = 0;
  }

  return p - buf;
}
