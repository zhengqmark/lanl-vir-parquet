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

#include "fmap.h"

#include "gen-cpp/parquet_types.h"
#include "thrift_serializer.h"

#include <algorithm>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

class ReadonlyOSFile : public RandomAccessFile {
 public:
  explicit ReadonlyOSFile(int fd) : fd_(fd) {}
  virtual int64_t Pread(void* buf, uint64_t size, uint64_t offset);
  virtual ~ReadonlyOSFile() { close(fd_); }

 private:
  int fd_;
};

int64_t ReadonlyOSFile::Pread(void* buf, uint64_t size, uint64_t offset) {
  return pread(fd_, buf, size, offset);
}

RandomAccessFile* NewOSFile(const char* fname, struct stat* statbuf) {
  int fd = open(fname, O_RDONLY);
  if (fd == -1) {
    throw std::runtime_error("Cannot open file for reading");
  }
  int r = fstat(fd, statbuf);
  if (r == -1) {
    throw std::runtime_error("Cannot stat file");
  }
  return new ReadonlyOSFile(fd);
}

RandomAccessFile::~RandomAccessFile() {}

FileMap::FileMap(std::vector<uint64_t>&& offsets,
                 std::vector<int64_t>&& underlying_offsets,
                 std::string&& direct_buf)
    : offsets_(std::move(offsets)),
      underlying_offsets_(std::move(underlying_offsets)),
      direct_buf_(std::move(direct_buf)),
      n_(offsets_.size() - 1) {}

int64_t FileMap::Pread(RandomAccessFile* base, char* buf, uint64_t size,
                       uint64_t offset) const {
  const uint64_t j =
      std::upper_bound(offsets_.begin(), offsets_.end(), offset) -
      offsets_.begin();
  const uint64_t ori = offset;

  uint64_t i = j - 1;
  while (size != 0 && offset < offsets_[n_]) {
    uint64_t lim = offsets_[i + 1] - offset;
    int64_t nr = Read(base, buf, i, offset - offsets_[i], std::min(size, lim));
    if (nr == -1) {
      return -1;
    }
    offset += nr;
    buf += nr;
    size -= nr;
    if (offset >= offsets_[i + 1]) {
      i++;
    }
  }

  return offset - ori;
}

int64_t FileMap::Read(RandomAccessFile* base, char* buf, uint64_t region_id,
                      uint64_t region_offset, uint64_t size) const {
  if (underlying_offsets_[region_id] > 0) {
    return base->Pread(buf, size,
                       underlying_offsets_[region_id] + region_offset);
  } else {
    memcpy(buf, &direct_buf_[-underlying_offsets_[region_id] + region_offset],
           size);
    return size;
  }
}

uint64_t FileMap::file_size() const { return offsets_[n_]; }

FileMap::~FileMap() {}

MapBuilder::MapBuilder() : bytes_written_(0) {}

MapBuilder::~MapBuilder() {}

void MapBuilder::AddDirect(void* buf, uint64_t size) {
  offsets_.push_back(bytes_written_);
  int64_t underlying_offset = direct_buf_.size();
  direct_buf_.append(reinterpret_cast<char*>(buf), size);
  underlying_offsets_.push_back(-underlying_offset);
  bytes_written_ += size;
}

void MapBuilder::AddMappedRegion(uint64_t offset, uint64_t size) {
  offsets_.push_back(bytes_written_);
  underlying_offsets_.push_back(int64_t(offset));
  bytes_written_ += size;
}

FileMap* MapBuilder::Finish() {
  offsets_.push_back(bytes_written_);  // EOF marking
  return new FileMap(std::move(offsets_), std::move(underlying_offsets_),
                     std::move(direct_buf_));
}

namespace {

uint32_t AppendPage(ThriftSerializer* serializer, MapBuilder* builder,
                    int value_size, uint64_t offset, uint32_t compressed_size,
                    uint32_t uncompressed_size, bool is_compressed) {
  parquet::format::PageHeader page_header;

  page_header.__set_type(parquet::format::PageType::DATA_PAGE_V2);
  page_header.__set_uncompressed_page_size(int32_t(uncompressed_size));
  page_header.__set_compressed_page_size(int32_t(compressed_size));

  {
    parquet::format::DataPageHeaderV2 v2;
    const int32_t num_values = int32_t(uncompressed_size) / value_size;
    v2.__set_num_values(num_values);
    v2.__set_num_nulls(0);
    v2.__set_num_rows(num_values);
    v2.__set_encoding(parquet::format::Encoding::PLAIN);
    v2.__set_definition_levels_byte_length(0);
    v2.__set_repetition_levels_byte_length(0);
    if (is_compressed) v2.__set_is_compressed(true);
    page_header.__set_data_page_header_v2(v2);
  }

  uint32_t len;
  uint8_t* buf;
  serializer->SerializeToBuffer(&page_header, &len, &buf);

  builder->AddDirect(buf, len);
  builder->AddMappedRegion(offset, compressed_size);
  return len;
}

bool ToParquetConvertedType(DataType type,
                            parquet::format::ConvertedType::type* converted) {
  switch (type) {
    case DataType::INT8:
      *converted = parquet::format::ConvertedType::INT_8;
      return true;
    case DataType::UINT8:
      *converted = parquet::format::ConvertedType::UINT_8;
      return true;
    case DataType::INT32:
      *converted = parquet::format::ConvertedType::INT_32;
      return true;
    case DataType::UINT32:
      *converted = parquet::format::ConvertedType::UINT_32;
      return true;
    case DataType::INT64:
      *converted = parquet::format::ConvertedType::INT_64;
      return true;
    case DataType::UINT64:
      *converted = parquet::format::ConvertedType::UINT_64;
      return true;
    case DataType::FLOAT32:
      return false;
    case DataType::FLOAT64:
      return false;
    default:
      throw std::runtime_error("Unsupported array data type");
  }
}

parquet::format::Type::type ToParquetType(DataType type) {
  switch (type) {
    case DataType::INT8:
    case DataType::UINT8:
    case DataType::INT32:
    case DataType::UINT32:
      return parquet::format::Type::INT32;
    case DataType::INT64:
    case DataType::UINT64:
      return parquet::format::Type::INT64;
    case DataType::FLOAT32:
      return parquet::format::Type::FLOAT;
    case DataType::FLOAT64:
      return parquet::format::Type::DOUBLE;
    default:
      throw std::runtime_error("Unsupported array data type");
  }
}

parquet::format::CompressionCodec::type ToParquetCodec(CompressionType type) {
  switch (type) {
    case CompressionType::NONE:
      return parquet::format::CompressionCodec::UNCOMPRESSED;
    case CompressionType::ZLIB:
      // Require parquet readers to accept zlib streams in addition to gzip
      // streams
      return parquet::format::CompressionCodec::GZIP;
    case CompressionType::LZ4:
      return parquet::format::CompressionCodec::LZ4_RAW;
    default:
      throw std::runtime_error("Unsupported data compression type");
  }
}

uint32_t AppendFooter(ThriftSerializer* serializer, MapBuilder* builder,
                      const std::string& name, DataType type,
                      CompressionType codec, int64_t total_compressed_size,
                      int64_t total_uncompressed_size, int64_t total_count) {
  parquet::format::FileMetaData file_meta_data;
  file_meta_data.__set_version(2);

  parquet::format::Type::type on_storage_type = ToParquetType(type);
  parquet::format::ConvertedType::type conv;
  bool needs_conversion = ToParquetConvertedType(type, &conv);

  {
    parquet::format::SchemaElement group;
    group.__set_name("vtkfile");
    group.__set_repetition_type(parquet::format::FieldRepetitionType::REQUIRED);
    group.__set_num_children(1);
    parquet::format::SchemaElement ele;
    ele.__set_name(name);
    ele.__set_repetition_type(parquet::format::FieldRepetitionType::REQUIRED);
    ele.__set_type(on_storage_type);
    if (needs_conversion) ele.__set_converted_type(conv);
    file_meta_data.__set_schema({group, ele});
  }

  file_meta_data.__set_num_rows(total_count);

  {
    parquet::format::RowGroup row_group;
    {
      parquet::format::ColumnChunk column;
      // This field is now deprecated and should not be used.
      // Writers should set this field to 0 if no ColumnMetaData has been
      // written outside the footer.
      column.__set_file_offset(0);
      {
        parquet::format::ColumnMetaData meta_data;
        meta_data.__set_type(on_storage_type);
        meta_data.__set_encodings({parquet::format::Encoding::PLAIN});
        meta_data.__set_path_in_schema({name});
        meta_data.__set_codec(ToParquetCodec(codec));
        meta_data.__set_num_values(total_count);
        meta_data.__set_total_uncompressed_size(total_uncompressed_size);
        meta_data.__set_total_compressed_size(total_compressed_size);
        meta_data.__set_data_page_offset(4);
        {
          parquet::format::Statistics stats;
#if 0
          float f = 1.0;
          std::string v;
          v.resize(4);
          memcpy(&v[0], &f, 4);
          stats.__set_max_value(v);
          stats.__set_is_max_value_exact(true);
          stats.__set_min_value(v);
          stats.__set_is_min_value_exact(true);
#endif
          stats.__set_null_count(0);
          meta_data.__set_statistics(stats);  // optional
        }
        column.__set_meta_data(meta_data);
      }
      row_group.__set_columns({column});
    }
    row_group.__set_num_rows(total_count);
    row_group.__set_total_byte_size(total_uncompressed_size);
    row_group.__set_file_offset(4);
    row_group.__set_total_compressed_size(total_compressed_size);
    file_meta_data.__set_row_groups({row_group});
  }

  uint32_t len;
  uint8_t* buf;
  serializer->SerializeToBuffer(&file_meta_data, &len, &buf);

  builder->AddDirect(buf, len);
  return len;
}

}  // namespace

FileMap* BuildMap(const std::string& name, DataType type, CompressionType codec,
                  const CompressedArray& arr) {
  char par1[] = "PAR1";
  int64_t total_compressed_size = 0;
  int64_t total_uncompressed_size = 0;
  int64_t total_count = 0;
  const int value_size = GetValueSize(type);
  ThriftSerializer serializer;
  MapBuilder builder;
  builder.AddDirect(par1, 4);
  uint64_t underlying_offset = arr.data_start;
  for (int i = 0; i < arr.num_blks - 1; i++) {
    uint32_t compressed_size = arr.compressed_blk_sz[i];
    uint32_t uncompressed_size = arr.blk_sz;
    uint32_t header_size =
        AppendPage(&serializer, &builder, value_size, underlying_offset,
                   compressed_size, uncompressed_size, true);
    underlying_offset += compressed_size;
    total_compressed_size += header_size + compressed_size;
    total_uncompressed_size += header_size + uncompressed_size;
    total_count += uncompressed_size / value_size;
  }
  {
    uint32_t compressed_size = arr.compressed_blk_sz[arr.num_blks - 1];
    uint32_t uncompressed_size = arr.last_blk_sz;
    uint32_t header_size =
        AppendPage(&serializer, &builder, value_size, underlying_offset,
                   compressed_size, uncompressed_size, true);
    total_compressed_size += header_size + compressed_size;
    total_uncompressed_size += header_size + uncompressed_size;
    total_count += uncompressed_size / value_size;
  }
  uint32_t foot_size =
      AppendFooter(&serializer, &builder, name, type, codec,
                   total_compressed_size, total_uncompressed_size, total_count);
  builder.AddDirect(&foot_size, 4);
  builder.AddDirect(par1, 4);

  return builder.Finish();
}

FileMap* BuildMap(const std::string& name, DataType type,
                  const UncompressedArray& arr) {
  char par1[] = "PAR1";
  int64_t total_compressed_size = 0;
  int64_t total_uncompressed_size = 0;
  int64_t total_count = 0;
  const int value_size = GetValueSize(type);
  ThriftSerializer serializer;
  MapBuilder builder;
  builder.AddDirect(par1, 4);
  uint64_t underlying_offset = arr.data_start;
  uint64_t blk_sz = 32768;
  uint64_t num_blks = (arr.total_bytes + blk_sz - 1) / blk_sz;  // TODO
  uint64_t last_blk_sz = (arr.total_bytes - 1) % 32768 + 1;
  for (int i = 0; i < num_blks - 1; i++) {
    uint32_t header_size = AppendPage(&serializer, &builder, value_size,
                                      underlying_offset, blk_sz, blk_sz, false);
    underlying_offset += blk_sz;
    total_compressed_size += header_size + blk_sz;
    total_uncompressed_size += header_size + blk_sz;
    total_count += blk_sz / value_size;
  }
  {
    uint32_t header_size =
        AppendPage(&serializer, &builder, value_size, underlying_offset,
                   last_blk_sz, last_blk_sz, false);
    total_compressed_size += header_size + last_blk_sz;
    total_uncompressed_size += header_size + last_blk_sz;
    total_count += last_blk_sz / value_size;
  }
  uint32_t foot_size =
      AppendFooter(&serializer, &builder, name, type, CompressionType::NONE,
                   total_compressed_size, total_uncompressed_size, total_count);
  builder.AddDirect(&foot_size, 4);
  builder.AddDirect(par1, 4);

  return builder.Finish();
}
