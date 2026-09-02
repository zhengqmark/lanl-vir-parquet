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

#include "c.h"

#include "parser.h"
#include "vfs.h"

#include <memory>
#include <stdint.h>

namespace {

enum LookupRes {
  DIR,
  FILE,
  NOTFOUND,
};

int Lookup(Dir* dir, const std::string& name) {
  FileStat stat;
  if (dir->Stat(name, &stat)) {
    return stat.is_dir ? LookupRes::DIR : LookupRes::FILE;
  } else {
    return LookupRes::NOTFOUND;
  }
}

// Resolve the input pathname up to the very last parent directory. Repeated '/'
// characters in the middle or at the end of the pathname are allowed and
// silently skipped. A trailing '/' does not require the final component to
// resolve to a directory.

// This operation is expected to be followed by a final Lookup call to resolve
// the last pathname component, which may refer to either a file or a directory.
int ResolvePath(Dir* root, const char* path, Dir** parent, std::string* name) {
  if (!path || path[0] != '/')  // Only absolute paths are supported
    return -EINVAL;

  const char* p = path;
  while (*p == '/') ++p;  // Skip leading separators

  if (!*p)  // "/" has no final component. Path is root.
    return 0;
  *parent = root;

  for (;;) {
    // Locate the next pathname component.
    const char* q = p;
    while (*p && *p != '/') ++p;
    name->assign(q, p - q);

    while (*p == '/')  // Skip separators following this component
      ++p;

    // If there are no more components, return the parent directory
    // together with the final component.
    if (!*p) {
      return 0;
    }

    // Resolve the next intermediate directory
    int r = Lookup(*parent, *name);
    if (r == LookupRes::DIR) {
      *parent = (*parent)->GetDir(*name);
    } else if (r == LookupRes::FILE) {
      return -ENOTDIR;
    } else {
      return -ENOENT;
    }
  }
}

// Convert owner/group/other read permissions to directory level
// read + execute permissions.
mode_t MakeDirMode(mode_t mode) {
  mode_t new_mode = S_IFDIR;

  if (mode & S_IRUSR) new_mode |= S_IRUSR | S_IXUSR;
  if (mode & S_IRGRP) new_mode |= S_IRGRP | S_IXGRP;
  if (mode & S_IROTH) new_mode |= S_IROTH | S_IXOTH;

  return new_mode;
}

int Getattr(Dir* parent, const std::string& name, struct stat* statbuf) {
  if (name.empty()) {  // Target is root
    statbuf->st_mode = MakeDirMode(statbuf->st_mode);
    return 0;
  }
  FileStat stat;
  if (!parent->Stat(name, &stat)) {
    return -ENOENT;
  }
  if (stat.is_dir) {
    statbuf->st_mode = MakeDirMode(statbuf->st_mode);
  } else {
    statbuf->st_size = stat.file_size;
    statbuf->st_mode |= S_IFREG;
  }
  return 0;
}

int Getattr(VtkTree* tree, const char* path, struct stat* statbuf) {
  Dir* parent = tree;
  std::string name;
  int r = ResolvePath(tree, path, &parent, &name);
  if (r == 0) {
    *statbuf = *tree->base_statbuf();
    return Getattr(parent, name, statbuf);
  } else {  // Error
    return r;
  }
}

int Readdir(Dir* dir, void* buf, fuse_fill_dir_t filler) {
  std::unique_ptr<Iter> it(dir->NewIter());
  it->SeekToFirst();
  while (it->Valid()) {
    filler(buf, it->Value(), NULL, 0, static_cast<enum fuse_fill_dir_flags>(0));
    it->Next();
  }
  return 0;
}

int Readdir(Dir* parent, const std::string& name, void* buf,
            fuse_fill_dir_t filler) {
  if (name.empty()) {  // Target is root
    return Readdir(parent, buf, filler);
  }
  int r = Lookup(parent, name);
  if (r == LookupRes::DIR) {
    return Readdir(parent->GetDir(name), buf, filler);
  } else if (r == LookupRes::FILE) {
    return -ENOTDIR;
  } else {
    return -ENOENT;
  }
}

int Readdir(VtkTree* tree, const char* path, void* buf,
            fuse_fill_dir_t filler) {
  Dir* parent = tree;
  std::string name;
  int r = ResolvePath(tree, path, &parent, &name);
  if (r == 0) {
    return Readdir(parent, name, buf, filler);
  } else {  // Error
    return r;
  }
}

int Open(Dir* parent, const std::string& name, struct fuse_file_info* fi) {
  if (name.empty()) {  // Target is root
    return -EISDIR;
  }
  int r = Lookup(parent, name);
  if (r == LookupRes::DIR) {
    return -EISDIR;
  } else if (r == LookupRes::FILE) {
    fi->fh = reinterpret_cast<uintptr_t>(parent->Open(name));
    return 0;
  } else {
    return -ENOENT;
  }
}

int Open(VtkTree* tree, const char* path, struct fuse_file_info* fi) {
  Dir* parent = tree;
  std::string name;
  int r = ResolvePath(tree, path, &parent, &name);
  if (r == 0) {
    return Open(parent, name, fi);
  } else {  // Error
    return r;
  }
}

int Read(char* buf, size_t size, off_t off, struct fuse_file_info* fi) {
  return reinterpret_cast<RandomAccessFile*>(fi->fh)->Pread(buf, size, off);
}

int Release(struct fuse_file_info* fi) {
  delete reinterpret_cast<RandomAccessFile*>(fi->fh);
  return 0;
}

}  // namespace

extern "C" {

// We guard against potential exceptions during tree initialization. Any
// exception at this stage results in early program termination, since there is
// no clear way to recover from an unparseable VTK file.

// Once the tree has been successfully initialized, the program state is
// effectively read-only and no further exceptions are expected. An exception
// after this point would therefore indicate a programming error that should be
// debugged rather than recovered from. We intentionally leave such exceptions
// unhandled so that the program fails fast.
void* vtk_init_tree_int(const char* fname) {
  try {
    return ParseVtkFile(fname);
  } catch (const std::exception& e) {
    fprintf(stderr, "Unable to init vtk tree. %s\n", e.what());
    struct fuse_context* ctx = fuse_get_context();
    if (ctx && ctx->fuse) {
      fuse_exit(ctx->fuse);
    }
    return nullptr;
  }
}

int vtk_getattr(const char* path, struct stat* statbuf,
                struct fuse_file_info* fi) {
  VtkTree* const tree =
      reinterpret_cast<VtkTree*>(fuse_get_context()->private_data);
  return Getattr(tree, path, statbuf);
}

int vtk_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t off,
                struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
  VtkTree* const tree =
      reinterpret_cast<VtkTree*>(fuse_get_context()->private_data);
  return Readdir(tree, path, buf, filler);
}

int vtk_open(const char* path, struct fuse_file_info* fi) {
  // Files may only be opened read-only. This check is likely redundant when
  // FUSE is mounted with -o default_permissions, which instructs the kernel to
  // enforce file permissions rather than deferring permission checks to the
  // filesystem.
  if ((fi->flags & O_ACCMODE) != O_RDONLY) {
    return -EPERM;
  }
  VtkTree* const tree =
      reinterpret_cast<VtkTree*>(fuse_get_context()->private_data);
  return Open(tree, path, fi);
}

int vtk_read(const char* path, char* buf, size_t size, off_t off,
             struct fuse_file_info* fi) {
  return Read(buf, size, off, fi);
}

int vtk_flush(const char* path, struct fuse_file_info* fi) { return 0; }

int vtk_release(const char* path, struct fuse_file_info* fi) {
  return Release(fi);
}

void vtk_destroy_tree(void* private_data) {
  delete reinterpret_cast<VtkTree*>(private_data);
}
}
