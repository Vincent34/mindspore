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
#include <memory>
#include "backend/kernel_compiler/gpu/rl/tensor_array_write_kernel.h"
#include "backend/kernel_compiler/common_utils.h"
#include "runtime/device/gpu/gpu_tensor_array.h"
#include "runtime/device/tensor_array_manager.h"

namespace mindspore {
namespace kernel {
constexpr size_t kSecondInputIndex = 2;
using mindspore::device::TensorArrayMgr;
using mindspore::device::gpu::GPUTensorArray;
using mindspore::device::gpu::GPUTensorArrayPtr;
TensorArrayWriteKernel::TensorArrayWriteKernel() : value_size_(0) {}

const std::vector<size_t> &TensorArrayWriteKernel::GetInputSizeList() const { return input_size_list_; }

const std::vector<size_t> &TensorArrayWriteKernel::GetOutputSizeList() const { return output_size_list_; }

const std::vector<size_t> &TensorArrayWriteKernel::GetWorkspaceSizeList() const { return workspace_size_list_; }

bool TensorArrayWriteKernel::Init(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  type_ = AnfAlgo::GetInputDeviceDataType(kernel_node, kSecondInputIndex);
  shapes_ = AnfAlgo::GetInputDeviceShape(kernel_node, kSecondInputIndex);
  value_size_ = GetTypeByte(TypeIdToType(type_));
  for (auto i : shapes_) {
    value_size_ *= i;
  }
  InitSizeLists();
  return true;
}

void TensorArrayWriteKernel::InitSizeLists() {
  input_size_list_.push_back(sizeof(int64_t));
  input_size_list_.push_back(sizeof(int64_t));
  output_size_list_.push_back(sizeof(int64_t));
}

bool TensorArrayWriteKernel::Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &outputs,
                                    const std::vector<AddressPtr> &, void *stream) {
  auto handle_addr = GetDeviceAddress<int64_t>(inputs, 0);
  auto index = GetDeviceAddress<int64_t>(inputs, 1);
  auto value = GetDeviceAddress<unsigned char>(inputs, 2);

  int64_t index_host = 0;
  CHECK_CUDA_RET_WITH_EXCEPT(kernel_node_,
                             cudaMemcpyAsync(&index_host, index, sizeof(int64_t), cudaMemcpyDeviceToHost,
                                             reinterpret_cast<cudaStream_t>(stream)),
                             "Get indexd failed");
  GPUTensorArrayPtr tensors_ =
    std::dynamic_pointer_cast<GPUTensorArray>(TensorArrayMgr::GetInstance().GetTensorArray(handle_addr));
  MS_EXCEPTION_IF_NULL(tensors_);
  if (!tensors_->CheckValue(type_, shapes_)) {
    MS_LOG(EXCEPTION) << "Invalid input data for tensor array write op.";
  }
  // Manage the value : create/reuse a device memory, and copy the input value to it.
  AddressPtr dev_addr = std::make_shared<kernel::Address>();
  MS_EXCEPTION_IF_NULL(dev_addr);
  if (tensors_->GetRealSize() > LongToSize(index_host)) {
    dev_addr->addr = tensors_->Read(index_host)->addr;
  } else {
    dev_addr->addr = device::gpu::GPUMemoryAllocator::GetInstance().AllocTensorMem(value_size_);
    MS_LOG(DEBUG) << "Create tensor " << dev_addr->addr << ", size " << value_size_;
  }
  MS_EXCEPTION_IF_NULL(dev_addr->addr);
  dev_addr->size = value_size_;
  CHECK_CUDA_RET_WITH_EXCEPT(kernel_node_,
                             cudaMemcpyAsync(dev_addr->addr, value, value_size_, cudaMemcpyDeviceToDevice,
                                             reinterpret_cast<cudaStream_t>(stream)),
                             "Copy value failed");

  if (tensors_->Write(index_host, dev_addr)) {
    MS_LOG(DEBUG) << "Write to tensorarry succeed, index " << index_host;
  } else {
    MS_LOG(EXCEPTION) << "Failed to write.";
  }
  return true;
}
}  // namespace kernel
}  // namespace mindspore
