<p align="center">
  <img src="logo.jpeg" alt="Virtual Parquet" width="100%">
</p>

Virtual Parquet is a FUSE filesystem that provides on-the-fly translation from standard VTK files to Parquet, without requiring the data to be converted in advance. Each FUSE file acts as a virtual address map: some regions map to dynamically generated in-memory Parquet metadata, while others map directly to regions of the underlying VTK file. Translations are one-way and read-only. We currently support image data (vti), structured grid (vts), and unstructured grid (vtu) mesh types.

# Parquet Tool Compatibility

Virtual Parquet works with a wide range of existing tools in the Parquet ecosystem. The following tools have been tested and confirmed to work with the virtual Parquet files translated from supported VTK files:

- [Apache Arrow Parquet Tools](https://github.com/apache/arrow/tree/main/cpp/tools/parquet)
- [DuckDB CLI](https://duckdb.org/docs/lts/clients/cli/overview)
- [Datafusion CLI](https://datafusion.apache.org/user-guide/cli/index.html)
- [Datanomy](https://github.com/raulcd/datanomy)
- [Data Wrangler](https://marketplace.visualstudio.com/items?itemName=ms-toolsai.datawrangler)
- [Hardwood](https://hardwood.dev/latest/)
- [Nail](https://github.com/Vitruves/nail-parquet)
- [Opteryx](https://github.com/mabel-dev/opteryx)
- [Parqeye](https://github.com/kaushiksrini/parqeye)
- [Parquetlens](https://github.com/cfahlgren1/parquetlens)
- [Pandas](https://pandas.pydata.org/)
- [PinkParquet](https://www.pinkparquet.com/)
- [Spark](https://pypi.org/project/pyspark/)
- [Tabview](https://github.com/shshemi/tabiew)

# Prerequisites

Compiling Virtual Parquet requires VTK 9.7+, libfuse3, libparquet, and libboost. On Ubuntu 26.04, VTK 9.7 (`wget https://vtk.org/files/release/9.7/VTK-9.7.0.tar.gz`) can be built from source with `cmake -DCMAKE_BUILD_TYPE=Release -DVTK_USE_X=OFF -DVTK_USE_Wayland=OFF`, while the other two dependencies can be installed via apt (`apt install libfuse3-dev libparquet-dev libboost-dev`). You will also need a few standard build tools, such as GCC, CMake, and Git: `apt install git wget gcc g++ make pkg-config cmake`.

# Use

Use the standard CMake workflow to build Virtual Parquet. Once built, run `fuse_main` to mount the FUSE filesystem backed by a Parquet file: `./fuse_main -odefault_permissions -ounderlying_file=/path/to/parquet/file /path/to/mount/point`. Use `fusermount -u /path/to/mount/point` to umount the FUSE filesystem. Mounting a FUSE filesystem requires write permission on the parent directory of the mount point.

A VTK file is translated into a tree of metadata and Parquet files. Metadata is exposed as pseudo-files representing the VTK file's root attributes, such as `byte_order`, `header_type`, and `compressor`. Each pseudo-file acts as a key-value pair: the filename is the key, and the file contents hold the value. Each Parquet file corresponds to a specific point-data or cell-data array in the VTK file.

As an example:

```
./fuse_main -odefault_permissions -ounderlying_file=/tmp/pv_insitu_300x300x300_24095.vti /tmp/pv_insitu_300x300x300_24095
tree /tmp/pv_insitu_300x300x300_24095/
/tmp/pv_insitu_300x300x300_24095/
├── celldata
│   └── vtkGhostType
├── METADATA
│   ├── byte_order
│   ├── compressor
│   ├── header_type
│   ├── Origin
│   ├── Spacing
│   ├── type
│   ├── version
│   └── WholeExtent
└── pointdata
    ├── prs
    ├── rho
    ├── tev
    ├── v02
    ├── v03
    ├── vtkGhostType
    └── vtkValidPointMask

4 directories, 16 files
fusermount -u /tmp/pv_insitu_300x300x300_24095
```

The VTK file used in this example can be downloaded from [oceans11.lanl.gov](https://oceans11.lanl.gov/deepwaterimpact/data/yA31/300x300x300-FourScalars_resolution/); any timestep will work. The files are part of LANL’s publicly available Deep Water Asteroid Impact dataset (LA-UR-17-21595). See this [video](https://www.youtube.com/watch?v=yeXcgnj8AG0) for more information about the dataset.

# Acknowledgement

[![License](https://licensebuttons.net/l/by/4.0/88x31.png)](https://creativecommons.org/licenses/by/4.0/)

```
██╗      █████╗ ███╗   ██╗██╗         ██╗  ██╗██████╗  ██████╗
██║     ██╔══██╗████╗  ██║██║         ██║  ██║██╔══██╗██╔════╝
██║     ███████║██╔██╗ ██║██║         ███████║██████╔╝██║
██║     ██╔══██║██║╚██╗██║██║         ██╔══██║██╔═══╝ ██║
███████╗██║  ██║██║ ╚████║███████╗    ██║  ██║██║     ╚██████╗
╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝    ╚═╝  ╚═╝╚═╝      ╚═════╝
```

This codebase is authored by an employee of Triad National Security, LLC which operates Los Alamos National Laboratory for the U.S. Department of Energy/National Nuclear Security Administration.

