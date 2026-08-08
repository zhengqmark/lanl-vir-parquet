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

#define FUSE_USE_VERSION 31

#include "c.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static struct fs_conf {
  const char* underlying_file;
} g_conf;

static void* fs_init_tree(  //
    struct fuse_conn_info* conn, struct fuse_config* cfg) {
  return vtk_init_tree_int(g_conf.underlying_file);
}

static const struct fuse_operations fs_oper = {
    .getattr = vtk_getattr,
    .open = vtk_open,
    .read = vtk_read,
    .flush = vtk_flush,
    .release = vtk_release,
    .readdir = vtk_readdir,
    .init = fs_init_tree,
    .destroy = vtk_destroy_tree,
};

static const struct fuse_opt fs_opts[] = {
    {"underlying_file=%s", offsetof(struct fs_conf, underlying_file), 0},
    FUSE_OPT_END,
};

int main(int argc, char* argv[]) {
  umask(0);  // Reset umask
  memset(&g_conf, 0, sizeof(struct fs_conf));
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  fuse_opt_parse(&args, &g_conf, fs_opts, NULL);
  if (!g_conf.underlying_file) {
    fprintf(stderr,
            "No vtk file specified. Use \"-ounderlying_file=%s\" to set the "
            "underlying vtk file.\n");
    return 1;
  }
  const int ret = fuse_main(args.argc, args.argv, &fs_oper, NULL);
  fuse_opt_free_args(&args);
  return ret;
}
