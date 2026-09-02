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

#include "parser.h"

#include <vtkDataCompressor.h>
#include <vtkSmartPointer.h>
#include <vtkXMLDataElement.h>
#include <vtkXMLDataParser.h>
#include <vtkXMLImageDataReader.h>
#include <vtkXMLReader.h>
#include <vtkXMLStructuredGridReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <string_view>

namespace {

void CopyAttrs(std::unordered_map<std::string, std::string>* des,
               vtkXMLDataElement* src) {
  for (int i = 0; i < src->GetNumberOfAttributes(); i++) {
    (*des)[src->GetAttributeName(i)] = src->GetAttributeValue(i);
  }
}

FileMap* ParseAppendedArray(RandomAccessFile* file, const std::string& name,
                            DataType type, CompressionType codec,
                            DataType header_type, uint64_t offset) {
  if (codec == CompressionType::NONE) {
    UncompressedArray arr;
    if (header_type == DataType::UINT32)
      ParseUncompressed32(file, offset, &arr);
    else
      ParseUncompressed64(file, offset, &arr);
    FileMap* map = BuildMap(name, type, arr);
    return map;
  } else {
    CompressedArray arr;
    if (header_type == DataType::UINT32)
      ParseCompressed32(file, offset, &arr);
    else
      ParseCompressed64(file, offset, &arr);
    FileMap* map = BuildMap(name, type, codec, arr);
    delete[] arr.compressed_blk_sz;
    return map;
  }
}

FileMap* ParseVtkDataArray(
    RandomAccessFile* file, const std::string& name, CompressionType codec,
    DataType header_type,
    const std::unordered_map<std::string, std::string>& map,
    uint64_t appended_data_pos) {
  DataType type = DataType::UNKNOWN;
  const std::string& t = map.at("type");
  if (t == "Int8") {
    type = DataType::INT8;
  } else if (t == "UInt8") {
    type = DataType::UINT8;
  } else if (t == "Int32") {
    type = DataType::INT32;
  } else if (t == "UInt32") {
    type = DataType::UINT32;
  } else if (t == "Int64") {
    type = DataType::INT64;
  } else if (t == "UInt64") {
    type = DataType::UINT64;
  } else if (t == "Float32") {
    type = DataType::FLOAT32;
  } else if (t == "Float64") {
    type = DataType::FLOAT64;
  } else {
    throw std::runtime_error("Unsupported data array type");
  }
  const std::string& format = map.at("format");
  if (format == "appended") {
    return ParseAppendedArray(
        file, name, type, codec, header_type,
        atoll(map.at("offset").c_str()) + appended_data_pos);
  } else {
    throw std::runtime_error("Unsupported data array format");
  }
}

Dir* ParseArrayGroup(
    RandomAccessFile* file, CompressionType codec, DataType header_type,
    const std::unordered_map<
        std::string, std::unordered_map<std::string, std::string>>& arr_info,
    uint64_t appended_data_pos) {
  std::unordered_map<std::string, std::unique_ptr<FileMap>> maps;
  for (auto& [name, info] : arr_info) {
#if 0
    if (name == "vtkValidPointMask") continue;
    if (name == "vtkGhostType") continue;
#endif
    maps.insert(
        {name, std::unique_ptr<FileMap>(ParseVtkDataArray(
                   file, name, codec, header_type, info, appended_data_pos))});
  }
  return new ArrayDir(std::move(maps), file, false);
}

CompressionType IdentifyCompressionType(vtkDataCompressor* compr) {
  if (!compr) {
    return CompressionType::NONE;
  } else if (compr->IsA("vtkZLibDataCompressor")) {
    return CompressionType::ZLIB;
  } else if (compr->IsA("vtkLZ4DataCompressor")) {
    return CompressionType::LZ4;
  } else {
    throw std::runtime_error("Unsupported data compression type");
  }
}

void ExtractCellsInfo(
    vtkXMLDataElement* root,
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>*
        cells_info) {
  vtkXMLDataElement* parent = root->LookupElementWithName("Cells");
  for (int i = 0; i < parent->GetNumberOfNestedElements(); i++) {
    vtkXMLDataElement* const arr = parent->GetNestedElement(i);
    const char* name = arr->GetAttribute("Name");
    CopyAttrs(&(*cells_info)[name], arr);
  }
}

void ExtractPointsInfo(
    vtkXMLDataElement* root,
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>*
        points_info) {
  vtkXMLDataElement* parent = root->LookupElementWithName("Points");
  for (int i = 0; i < parent->GetNumberOfNestedElements(); i++) {
    vtkXMLDataElement* const arr = parent->GetNestedElement(i);
    const char* name = arr->GetAttribute("Name");
    CopyAttrs(&(*points_info)[name], arr);
  }
}

void ExtractFieldArrayInfo(
    vtkXMLReader* reader, vtkXMLDataElement* root,
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>*
        point_arr_info,
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>*
        cell_arr_info) {
  vtkXMLDataElement* pd = root->LookupElementWithName("PointData");
  for (int i = 0; i < reader->GetNumberOfPointArrays(); i++) {
    CopyAttrs(&(*point_arr_info)[reader->GetPointArrayName(i)],
              pd->GetNestedElement(i));
  }
  vtkXMLDataElement* cd = root->LookupElementWithName("CellData");
  for (int i = 0; i < reader->GetNumberOfCellArrays(); i++) {
    CopyAttrs(&(*cell_arr_info)[reader->GetCellArrayName(i)],
              cd->GetNestedElement(i));
  }
}

DataType ParseHeaderType(
    const std::unordered_map<std::string, std::string>& map) {
  const std::string& t = map.at("header_type");
  if (t == "UInt32") {
    return DataType::UINT32;
  } else if (t == "UInt64") {
    return DataType::UINT64;
  }
  throw std::runtime_error("Unknown header type");
}

vtkXMLReader* CreateReader(const std::string& mesh_type) {
  if (mesh_type == "UnstructuredGrid")
    return vtkXMLUnstructuredGridReader::New();
  if (mesh_type == "StructuredGrid") return vtkXMLStructuredGridReader::New();
  if (mesh_type == "ImageData") return vtkXMLImageDataReader::New();

  throw std::runtime_error("Unsupported vtk mesh type");
}

VtkTree* ParseVtkFileInternal(const char* fname, const char* mesh_type,
                              bool load_points = false,
                              bool load_cells = false) {
  std::unordered_map<std::string, std::string> root_attrs;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      point_arr_info;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      points_info;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      cell_arr_info;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      cells_info;

  vtkSmartPointer<vtkXMLReader> reader(CreateReader(mesh_type));
  reader->SetFileName(fname);
  if (!reader->UpdateInformation()) {
    throw std::runtime_error(
        "Failed to parse the input vtk file. Please make sure the file exists "
        "and is valid.");
  }

  vtkXMLDataParser* parser = reader->GetXMLParser();
  const CompressionType codec =
      IdentifyCompressionType(parser->GetCompressor());
  const uint64_t appended_data_pos = parser->GetAppendedDataPosition();

  vtkXMLDataElement* root = parser->GetRootElement();
  vtkXMLDataElement* app = root->FindNestedElementWithName("AppendedData");
  if (!app) {
    // OK!
  } else if (strcmp(app->GetAttribute("encoding"), "raw") != 0) {
    throw std::runtime_error("Unsupported appended data encoding type");
  }
  CopyAttrs(&root_attrs, root);
  const DataType header_type = ParseHeaderType(root_attrs);
  CopyAttrs(&root_attrs, root->FindNestedElementWithName(mesh_type));
  ExtractFieldArrayInfo(reader, root, &point_arr_info, &cell_arr_info);
  if (load_points) ExtractPointsInfo(root, &points_info);
  if (load_cells) {
    ExtractCellsInfo(root, &cells_info);
  }

  struct stat statbuf;
  std::unique_ptr<RandomAccessFile> file(NewOSFile(fname, &statbuf));
  std::unordered_map<std::string, std::unique_ptr<Dir>> subdirs;
  subdirs.emplace("METADATA", new MetadataDir(std::move(root_attrs)));
  subdirs.emplace("pointdata",
                  ParseArrayGroup(file.get(), codec, header_type,
                                  point_arr_info, appended_data_pos));
  if (load_points)
    subdirs.emplace("points", ParseArrayGroup(file.get(), codec, header_type,
                                              points_info, appended_data_pos));
  subdirs.emplace("celldata",
                  ParseArrayGroup(file.get(), codec, header_type, cell_arr_info,
                                  appended_data_pos));
  if (load_cells)
    subdirs.emplace("cells", ParseArrayGroup(file.get(), codec, header_type,
                                             cells_info, appended_data_pos));
  return new VtkTree(std::move(subdirs), &statbuf, file.release(), true);
}

bool EndsWith(std::string_view str, std::string_view suffix) {
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

VtkTree* ParseVtkFile(const char* fname) {
  if (EndsWith(fname, ".vtu"))
    return ParseVtkFileInternal(fname, "UnstructuredGrid", true, true);
  if (EndsWith(fname, ".vts"))
    return ParseVtkFileInternal(fname, "StructuredGrid", true);
  if (EndsWith(fname, ".vti")) return ParseVtkFileInternal(fname, "ImageData");

  throw std::runtime_error("Unsupported vtk file format");
}
