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

#include "vfs.h"

#include <stdexcept>
#include <string.h>

FileStat::FileStat() {}

class StringBufFile : public RandomAccessFile {
 public:
  explicit StringBufFile(const std::string* strbuf) : strbuf_(strbuf) {}
  virtual int64_t Pread(void* buf, uint64_t size, uint64_t offset);
  virtual ~StringBufFile() {}

 private:
  const std::string* strbuf_;
};

int64_t StringBufFile::Pread(void* buf, uint64_t size, uint64_t offset) {
  uint64_t n = strbuf_->size();
  if (offset < n) {
    uint64_t nbytes = std::min(n - offset, size);
    memcpy(buf, &(*strbuf_)[offset], nbytes);
    return nbytes;
  } else {
    return 0;
  }
}

class VirtualFile : public RandomAccessFile {
 public:
  VirtualFile(const FileMap& map, RandomAccessFile* base, bool own_base)
      : map_(map), base_(base), own_base_(own_base) {}
  virtual int64_t Pread(void* buf, uint64_t size, uint64_t offset);
  virtual ~VirtualFile() {
    if (own_base_) delete base_;
  }

 private:
  const FileMap& map_;
  RandomAccessFile* base_;
  bool own_base_;
};

int64_t VirtualFile::Pread(void* buf, uint64_t size, uint64_t offset) {
  return map_.Pread(base_, static_cast<char*>(buf), size, offset);
}

Iter::~Iter() {}

template <class T>
class KeyIter : public Iter {
 public:
  explicit KeyIter(const std::unordered_map<std::string, T>* map) : map_(map) {
    it_ = map_->end();
  }
  virtual bool Valid() const { return it_ != map_->end(); }
  virtual const char* Value() const { return it_->first.c_str(); }
  virtual void Next() { ++it_; }
  virtual void SeekToFirst() { it_ = map_->begin(); }
  virtual ~KeyIter() {}

 private:
  const std::unordered_map<std::string, T>* map_;
  typename std::unordered_map<std::string, T>::const_iterator it_;
};

Dir::~Dir() {}

MetadataDir::MetadataDir(std::unordered_map<std::string, std::string>&& map)
    : map_(std::move(map)) {}

RandomAccessFile* MetadataDir::Open(const std::string& name) {
  return new StringBufFile(&map_.at(name));
}

Dir* MetadataDir::GetDir(const std::string& name) { return NULL; }

bool MetadataDir::Stat(const std::string& name, FileStat* stat) {
  auto it = map_.find(name);
  if (it != map_.end()) {
    stat->file_size = it->second.size();
    stat->is_dir = false;
    return true;
  } else {
    return false;
  }
}

Iter* MetadataDir::NewIter() { return new KeyIter(&map_); }

MetadataDir::~MetadataDir() {}

ArrayDir::ArrayDir(
    std::unordered_map<std::string, std::unique_ptr<FileMap>>&& maps,
    RandomAccessFile* base, bool own_base)
    : arr_maps_(std::move(maps)), base_(base), own_base_(own_base) {}

RandomAccessFile* ArrayDir::Open(const std::string& name) {
  return new VirtualFile(*arr_maps_.at(name), base_, false);
}

Dir* ArrayDir::GetDir(const std::string& name) { return NULL; }

bool ArrayDir::Stat(const std::string& name, FileStat* stat) {
  auto it = arr_maps_.find(name);
  if (it != arr_maps_.end()) {
    stat->file_size = it->second->file_size();
    stat->is_dir = false;
    return true;
  } else {
    return false;
  }
}

Iter* ArrayDir::NewIter() { return new KeyIter(&arr_maps_); }

ArrayDir::~ArrayDir() {
  if (own_base_) delete base_;
}

namespace {

struct stat SanitizedStat(const struct stat* input) {
  struct stat statbuf;
  memset(&statbuf, 0, sizeof(statbuf));
  statbuf.st_mode = input->st_mode & (S_IRUSR | S_IRGRP | S_IROTH);
  statbuf.st_atim = input->st_atim;
  statbuf.st_mtim = input->st_mtim;
  statbuf.st_ctim = input->st_ctim;
  statbuf.st_rdev = input->st_rdev;
  statbuf.st_dev = input->st_dev;
  statbuf.st_uid = input->st_uid;
  statbuf.st_gid = input->st_gid;
  return statbuf;
}

}  // namespace

VtkTree::VtkTree(std::unordered_map<std::string, std::unique_ptr<Dir>>&& subdir,
                 const struct stat* statbuf, RandomAccessFile* base,
                 bool own_base)
    : subdirs_(std::move(subdir)),
      base_statbuf_(SanitizedStat(statbuf)),
      base_(base),
      own_base_(own_base) {}

RandomAccessFile* VtkTree::Open(const std::string& name) {
  return NULL;  // TODO
}

Dir* VtkTree::GetDir(const std::string& name) {
  return subdirs_.at(name).get();
}

bool VtkTree::Stat(const std::string& name, FileStat* stat) {
  auto it = subdirs_.find(name);
  if (it != subdirs_.end()) {
    stat->file_size = 0;
    stat->is_dir = true;
    return true;
  } else {
    return false;
  }
}

Iter* VtkTree::NewIter() { return new KeyIter(&subdirs_); }

VtkTree::~VtkTree() {
  if (own_base_) delete base_;
}
