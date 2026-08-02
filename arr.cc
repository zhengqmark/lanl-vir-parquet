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

#include "arr.h"

#include "fmap.h"

#include <stdexcept>

CompressedArray::CompressedArray() {}

void ParseCompressed(RandomAccessFile* file, uint64_t offset,
                     CompressedArray* result) {
  uint64_t info[3];
  uint64_t nr = file->Pread(info, 24, offset);
  if (nr != 24) {
    throw std::runtime_error("Fail to read array header");
  }

  uint64_t* compressed_block_size = new uint64_t[info[0]];
  nr = file->Pread(compressed_block_size, 8 * info[0], offset + 24);
  if (nr != 8 * info[0]) {
    delete[] compressed_block_size;
    throw std::runtime_error("Fail to read block sizes");
  }

  result->num_blks = info[0];
  result->blk_sz = info[1];
  result->last_blk_sz = info[2];
  result->compressed_blk_sz = compressed_block_size;
  result->data_start = offset + 24 + 8 * info[0];
}

int GetValueSize(ArrayType type) {
  switch (type) {
    case ArrayType::INT8:
    case ArrayType::UINT8:
      return 1;
    case ArrayType::FLOAT32:
      return 4;
    default:
      throw std::runtime_error("Unknown array type");
  }
}
