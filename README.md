# Virtual Parquet

[![License](https://licensebuttons.net/l/by/4.0/88x31.png)](https://creativecommons.org/licenses/by/4.0/)

```
██╗      █████╗ ███╗   ██╗██╗         ██╗  ██╗██████╗  ██████╗
██║     ██╔══██╗████╗  ██║██║         ██║  ██║██╔══██╗██╔════╝
██║     ███████║██╔██╗ ██║██║         ███████║██████╔╝██║
██║     ██╔══██║██║╚██╗██║██║         ██╔══██║██╔═══╝ ██║
███████╗██║  ██║██║ ╚████║███████╗    ██║  ██║██║     ╚██████╗
╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝    ╚═╝  ╚═╝╚═╝      ╚═════╝
```

Virtual Parquet is a FUSE filesystem that provides on-the-fly translation from standard VTK files to Parquet, without requiring the data to be converted in advance.

# Prerequisites

Compiling Virtual Parquet requires VTK 9.7+, libfuse3, libparquet, and libboost. On Ubuntu 26.04, VTK 9.7 (`wget https://github.com/Kitware/VTK/archive/refs/tags/v9.7.0.rc4.tar.gz`) can be built from source with `cmake -DCMAKE_BUILD_TYPE=Release -DVTK_USE_X=OFF -DVTK_USE_Wayland=OFF`, while the other two dependencies can be installed via apt (`apt install libfuse3-dev libparquet-dev libboost-dev`). You will also need a few standard build tools, such as GCC, CMake, and Git: `apt install git wget gcc g++ make pkg-config cmake`.

# Acknowledgement

This codebase is authored by an employee of Triad National Security, LLC which operates Los Alamos National Laboratory for the U.S. Department of Energy/National Nuclear Security Administration.

