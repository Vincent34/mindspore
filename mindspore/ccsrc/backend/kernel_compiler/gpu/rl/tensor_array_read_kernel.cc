/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "backend/kernel_compiler/gpu/rl/tensor_array_read_kernel.h"
#include "backend/kernel_compiler/common_utils.h"
#include "runtime/device/gpu/gpu_tensor_array.h"
#include "runtime/device/tensor_array_manager.h"

namespace mindspore {
namespace kernel {
using mindspore::device::TensorArrayMgr;
using mindspore::device::TensorArrayPtr;
TensorArrayReadKernel::TensorArrayReadKernel() : value_size_(0), type_(nullptr) {}

const std::vector<size_t> &TensorArrayReadKernel::GetInputSizeList() const { return input_size_list_; }

const std::vector<size_t> &TensorArrayReadKernel::GetOutputSizeList() const { return output_size_list_; }

const std::vector<size_t> &TensorArrayReadKernel::GetWorkspaceSizeList() const { return workspace_size_list_; }

bool TensorArrayReadKernel::Init(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  shapes_ = GetAttr<std::vector<int64_t>>(kernel_node, "element_shape");
  type_ = GetAttr<TypePtr>(kernel_node, "dtype");
  value_size_ = GetTypeByte(type_);
  for (auto i : shapes_) {
    value_size_ *= i;
  }
  InitSizeLists();
  return true;
}

void TensorArrayReadKernel::InitSizeLists() {
  input_size_list_.push_back(sizeof(int64_t));
  input_size_list_.push_back(sizeof(int64_t));
  output_size_list_.push_back(value_size_);
}

bool TensorArrayReadKernel::Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
                                   const std::vector<AddressPtr> &outputs, void *stream) {
  auto handle_addr = GetDeviceAddress<int64_t>(inputs, 0);
  auto index = GetDeviceAddress<int64_t>(inputs, 1);
  auto out_value = GetDeviceAddress<unsigned char>(outputs, 0);
  MS_ERROR_IF_NULL(out_value);
  int64_t index_host = 0;
  CHECK_CUDA_RET_WITH_EXCEPT(kernel_node_,
                             cudaMemcpyAsync(&index_host, index, sizeof(int64_t), cudaMemcpyDeviceToHost,
                                             reinterpret_cast<cudaStream_t>(stream)),
                             "Get index failed");
  TensorArrayPtr tensors_ = TensorArrayMgr::GetInstance().GetTensorArray(handle_addr);
  MS_ERROR_IF_NULL(tensors_);
  if (!tensors_->CheckReadIndexLogical(index_host)) {
    MS_LOG(EXCEPTION) << "Invalid index " << index_host << " for read.";
  }
  auto value_addr = tensors_->Read(index_host);
  MS_LOG(DEBUG) << "Read value index:" << index_host;
  CHECK_CUDA_RET_WITH_EXCEPT(kernel_node_,
                             cudaMemcpyAsync(out_value, value_addr->addr, value_size_, cudaMemcpyDeviceToDevice,
                                             reinterpret_cast<cudaStream_t>(stream)),
                             "Get value failed");
  return true;
}
}  // namespace kernel
}  // namespace mindspore
