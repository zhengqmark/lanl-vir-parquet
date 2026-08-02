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

#pragma once

#include "fmap.h"

#include <memory>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

struct FileStat {
  FileStat();
  uint64_t file_size;
  bool is_dir;
};

class Iter {
 public:
  virtual bool Valid() const = 0;
  virtual const char* Value() const = 0;
  virtual void Next() = 0;
  virtual void SeekToFirst() = 0;
  virtual ~Iter();
};

class Dir {
 public:
  virtual RandomAccessFile* Open(const std::string& name) = 0;
  virtual Dir* GetDir(const std::string& name) = 0;
  virtual bool Stat(const std::string& name, FileStat* stat) = 0;
  virtual Iter* NewIter() = 0;
  virtual ~Dir();
};

class MetadataDir : public Dir {
 public:
  MetadataDir(std::unordered_map<std::string, std::string>&& map);
  virtual RandomAccessFile* Open(const std::string& name);
  virtual Dir* GetDir(const std::string& name);
  virtual bool Stat(const std::string& name, FileStat* stat);
  virtual Iter* NewIter();
  virtual ~MetadataDir();

 private:
  const std::unordered_map<std::string, std::string> map_;
};

class ArrayDir : public Dir {
 public:
  ArrayDir(std::unordered_map<std::string, std::unique_ptr<FileMap>>&& maps,
           RandomAccessFile* base, bool own_base);
  virtual RandomAccessFile* Open(const std::string& name);
  virtual Dir* GetDir(const std::string& name);
  virtual bool Stat(const std::string& name, FileStat* stat);
  virtual Iter* NewIter();
  virtual ~ArrayDir();

 private:
  const std::unordered_map<std::string, std::unique_ptr<FileMap>> arr_maps_;
  RandomAccessFile* base_;
  bool own_base_;
};

class VtkTree : public Dir {
 public:
  VtkTree(std::unordered_map<std::string, std::unique_ptr<Dir>>&& subdir,
          const struct stat* statbuf, RandomAccessFile* base, bool own_base);
  const struct stat* base_statbuf() const { return &base_statbuf_; }
  virtual RandomAccessFile* Open(const std::string& name);
  virtual Dir* GetDir(const std::string& name);
  virtual bool Stat(const std::string& name, FileStat* stat);
  virtual Iter* NewIter();
  virtual ~VtkTree();

 private:
  std::unordered_map<std::string, std::unique_ptr<Dir>> subdirs_;
  const struct stat base_statbuf_;
  RandomAccessFile* base_;
  bool own_base_;
};
