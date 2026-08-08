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

#include <vtkNew.h>
#include <vtkXMLDataElement.h>
#include <vtkXMLDataParser.h>
#include <vtkXMLImageDataReader.h>

namespace {

inline void CopyAttrs(std::unordered_map<std::string, std::string>* des,
                      vtkXMLDataElement* src) {
  for (int i = 0; i < src->GetNumberOfAttributes(); i++) {
    (*des)[src->GetAttributeName(i)] = src->GetAttributeValue(i);
  }
}

FileMap* ParseAppendedArray(RandomAccessFile* file, const std::string& name,
                            ArrayType type, uint64_t offset) {
  CompressedArray arr;
  ParseCompressed(file, offset, &arr);
  FileMap* map = BuildMap(name, type, arr);
  delete[] arr.compressed_blk_sz;
  return map;
}

FileMap* ParseFieldArray(
    RandomAccessFile* file, const std::string& name,
    const std::unordered_map<std::string, std::string>& map,
    uint64_t appended_data_pos) {
  ArrayType type = ArrayType::UNKNOWN;
  const std::string& t = map.at("type");
  if (t == "Int8") {
    type = ArrayType::INT8;
  } else if (t == "UInt8") {
    type = ArrayType::UINT8;
  } else if (t == "Float32") {
    type = ArrayType::FLOAT32;
  } else {
    throw std::runtime_error("Unsupported point array type");
  }
  const std::string& format = map.at("format");
  if (format == "appended") {
    return ParseAppendedArray(
        file, name, type, atoll(map.at("offset").c_str()) + appended_data_pos);
  } else {
    throw std::runtime_error("Unsupported point array format");
  }
}

Dir* ParseFieldData(
    RandomAccessFile* file,
    const std::unordered_map<
        std::string, std::unordered_map<std::string, std::string>>& arr_info,
    uint64_t appended_data_pos) {
  std::unordered_map<std::string, std::unique_ptr<FileMap>> maps;
  for (auto& [name, info] : arr_info) {
#if 0
    if (name == "vtkValidPointMask") continue;
    if (name == "vtkGhostType") continue;
#endif
    maps.insert({name, std::unique_ptr<FileMap>(ParseFieldArray(
                           file, name, info, appended_data_pos))});
  }
  return new ArrayDir(std::move(maps), file, false);
}

VtkTree* ParseVtiFile(const char* fname) {
  std::unordered_map<std::string, std::string> root_attrs;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      point_arr_info;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      cell_arr_info;
  uint64_t appended_data_pos = 0;

  {
    vtkNew<vtkXMLImageDataReader> reader;
    reader->SetFileName(fname);
    reader->UpdateInformation();
    vtkXMLDataParser* parser = reader->GetXMLParser();
    appended_data_pos = parser->GetAppendedDataPosition();
    vtkXMLDataElement* root = parser->GetRootElement();
    CopyAttrs(&root_attrs, root);
    CopyAttrs(&root_attrs, root->FindNestedElementWithName("ImageData"));
    {
      vtkXMLDataElement* pd = root->LookupElementWithName("PointData");
      for (int i = 0; i < reader->GetNumberOfPointArrays(); i++) {
        CopyAttrs(&point_arr_info[reader->GetPointArrayName(i)],
                  pd->GetNestedElement(i));
      }
    }
    {
      vtkXMLDataElement* pd = root->LookupElementWithName("CellData");
      for (int i = 0; i < reader->GetNumberOfCellArrays(); i++) {
        CopyAttrs(&cell_arr_info[reader->GetCellArrayName(i)],
                  pd->GetNestedElement(i));
      }
    }
  }

  struct stat statbuf;
  std::unique_ptr<RandomAccessFile> file(NewOSFile(fname, &statbuf));
  std::unordered_map<std::string, std::unique_ptr<Dir>> subdirs;
  subdirs.insert({"METADATA", std::unique_ptr<Dir>(
                                  new MetadataDir(std::move(root_attrs)))});
  subdirs.insert(
      {"pointdata", std::unique_ptr<Dir>(ParseFieldData(
                        file.get(), point_arr_info, appended_data_pos))});
  subdirs.insert(
      {"celldata", std::unique_ptr<Dir>(ParseFieldData(
                       file.get(), cell_arr_info, appended_data_pos))});
  return new VtkTree(std::move(subdirs), &statbuf, file.release(), true);
}

}  // namespace

VtkTree* ParseVtkFile(const char* fname) { return ParseVtiFile(fname); }
