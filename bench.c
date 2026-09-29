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

#include "fuse_bypass.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

int os_drop_caches() {
  int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
  if (fd == -1) {
    perror("open drop_caches");
    exit(EXIT_FAILURE);
  }

  if (write(fd, "3\n", 2) != 2) {
    perror("write drop_caches");
    close(fd);
    exit(EXIT_FAILURE);
  }

  return close(fd);
}

uint64_t current_micros() {
  uint64_t result;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  result = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
  return result;
}

void run_bypass(void* vtk_tree, const char* fname, char* buf, size_t bufsz) {
  uint64_t start = current_micros();
  void* const file = TEST_open(vtk_tree, fname);
  if (!file) {
    return;
  }
  off_t off = 0;
  for (;;) {
    int nr = TEST_read(file, buf, bufsz, off);
    if (nr < 0) {
      return;  // Error
    }
    off += nr;
    if (nr < bufsz) {
      break;  // End-of-file
    }
  }
  uint64_t dura = current_micros() - start;
  fprintf(stdout, "f=%s (%ld bytes)\n", fname, off);
  fprintf(stdout, "bufsz=%lu bytes\n", bufsz);
  fprintf(stdout, "Read speed=%.3f MiB/s\n",
          1000.0 * 1000.0 * (double)off / (double)dura / 1024.0 / 1024.0);
  TEST_close(file);
}

void run_posix(const char* fname, char* buf, size_t bufsz) {
  uint64_t start = current_micros();
  int fd = open(fname, O_RDONLY);
  if (fd == -1) {
    return;
  }
  off_t off = 0;
  for (;;) {
    int nr = pread(fd, buf, bufsz, off);
    if (nr < 0) {
      return;  // Error
    }
    off += nr;
    if (nr < bufsz) {
      break;  // End-of-file
    }
  }
  uint64_t dura = current_micros() - start;
  fprintf(stdout, "f=%s (%ld bytes)\n", fname, off);
  fprintf(stdout, "bufsz=%lu bytes\n", bufsz);
  fprintf(stdout, "Read speed=%.3f MiB/s\n",
          1000.0 * 1000.0 * (double)off / (double)dura / 1024.0 / 1024.0);
  close(fd);
}

int main(int argc, char* argv[]) {
  int mode = 1; /* 0=posix/fuse path, 1=fuse-bypass */
  size_t bufsz = 131072;
  int c;
  while ((c = getopt(argc, argv, "b:m:")) != -1) {
    switch (c) {
      case 'b':
        bufsz = atoi(optarg);
        break;
      case 'm':
        mode = atoi(optarg);
        break;
      default:
        break;
    }
  }

  argc -= optind;
  argv += optind;

  if (mode == 0) {
    if (argc < 1) {
      exit(1);
    }

    os_drop_caches();
    char* const buf = malloc(bufsz);
    run_posix(argv[0], buf, bufsz);
    free(buf);

  } else {
    if (argc < 2) {
      exit(1);
    }

    const char* vtk_fname = argv[0];
    const char* vir_fname = argv[1];

    void* const vtk_tree = TEST_init_tree(vtk_fname);
    os_drop_caches();
    char* const buf = malloc(bufsz);
    run_bypass(vtk_tree, vir_fname, buf, bufsz);
    free(buf);
    TEST_destroy_tree(vtk_tree);
  }

  return 0;
}
