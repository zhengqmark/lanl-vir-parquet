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

void ParseCompressed64(RandomAccessFile* file, uint64_t offset,
                       CompressedArray* result) {
  uint64_t info[3];
  uint64_t nr = file->Pread(info, 24, offset);
  if (nr != 24) {
    throw std::runtime_error("Fail to read data array header");
  }

  uint64_t* compressed_block_size = new uint64_t[info[0]];
  nr = file->Pread(compressed_block_size, 8 * info[0], offset + 24);
  if (nr != 8 * info[0]) {
    delete[] compressed_block_size;
    throw std::runtime_error("Fail to read array block sizes");
  }

  result->num_blks = info[0];
  result->blk_sz = info[1];
  result->last_blk_sz = info[2];
  result->compressed_blk_sz = compressed_block_size;
  result->data_start = offset + 24 + 8 * info[0];
}

void ParseCompressed32(RandomAccessFile* file, uint64_t offset,
                       CompressedArray* result) {
  uint32_t info[3];
  uint64_t nr = file->Pread(info, 12, offset);
  if (nr != 12) {
    throw std::runtime_error("Fail to read data array header");
  }

  uint32_t* compressed_block_size = new uint32_t[info[0]];
  nr = file->Pread(compressed_block_size, 4 * info[0], offset + 12);
  if (nr != 4 * info[0]) {
    delete[] compressed_block_size;
    throw std::runtime_error("Fail to read array block sizes");
  }

  result->num_blks = info[0];
  result->blk_sz = info[1];
  result->last_blk_sz = info[2];
  uint64_t* compressed_block_size64 = new uint64_t[info[0]];
  for (int i = 0; i < info[0]; i++) {
    compressed_block_size64[i] = compressed_block_size[i];
  }
  delete[] compressed_block_size;
  result->compressed_blk_sz = compressed_block_size64;
  result->data_start = offset + 12 + 4 * info[0];
}

UncompressedArray::UncompressedArray() {}

void ParseUncompressed64(RandomAccessFile* file, uint64_t offset,
                         UncompressedArray* result) {
  uint64_t bytes;
  uint64_t nr = file->Pread(&bytes, 8, offset);
  if (nr != 8) {
    throw std::runtime_error("Fail to read data array header");
  }

  result->total_bytes = bytes;
  result->data_start = offset + 8;
}

void ParseUncompressed32(RandomAccessFile* file, uint64_t offset,
                         UncompressedArray* result) {
  uint32_t bytes;
  uint64_t nr = file->Pread(&bytes, 4, offset);
  if (nr != 4) {
    throw std::runtime_error("Fail to read data array header");
  }

  result->total_bytes = bytes;
  result->data_start = offset + 4;
}

int GetValueSize(DataType type) {
  switch (type) {
    case DataType::INT8:
    case DataType::UINT8:
      return 1;
    case DataType::UINT32:
    case DataType::FLOAT32:
      return 4;
    case DataType::UINT64:
    case DataType::FLOAT64:
      return 8;
    default:
      throw std::runtime_error("Unknown data type");
  }
}
