/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_CLIP_BY_NORM_GPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_CLIP_BY_NORM_GPU_KERNEL_H_

#include <map>
#include <string>
#include <vector>
#include "mindspore/core/ops/clip_by_norm.h"
#include "plugin/device/gpu/kernel/gpu_kernel.h"

namespace mindspore {
namespace kernel {
template <typename T, typename S>
class ClipByNormGpuKernelMod : public NativeGpuKernelMod {
 public:
  ClipByNormGpuKernelMod() = default;

  ~ClipByNormGpuKernelMod() override { DestroyResource(); }

  bool Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
            const std::vector<KernelTensorPtr> &outputs) override;

  int Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &,
             const std::vector<KernelTensorPtr> &, const std::map<uint32_t, tensor::TensorPtr> &) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override;

  std::vector<KernelAttr> GetOpSupport() override;

  void DestroyResource() noexcept override;

 protected:
  void InitResource() override;

 private:
  void ResetResource();
  void InitIOShape(const std::vector<KernelTensorPtr> &inputs, const std::vector<KernelTensorPtr> &outputs);
  bool ExistNullShape();
  void InitAxisAndEpsilon(const ops::ClipByNormPtr &prim);
  void InitSizeLists();
  // Do broadcast infer
  void BroadcastInfer();
  // Chose `cudnnReduceNorm2` to achieve `l2_norm` calculation
  void ChoseCudnnReduceTensorOp();
  // Determine data shape, type and format for `inputA_descriptor` and `outputC_descriptor`
  void DetermineDeviceDataInfoForCudnn(const KernelTensorPtr &x_tensor);
  // Launch `ClipByNorm` calculation
  bool DoLaunch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
                const std::vector<AddressPtr> &outputs, void *stream_ptr);

  // Define cudnn variables for running `cudnnReduceTensor`
  cudnnHandle_t cudnn_handle_{nullptr};
  cudnnDataType_t data_type_{CUDNN_DATA_FLOAT};
  cudnnNanPropagation_t nan_prop_{CUDNN_NOT_PROPAGATE_NAN};
  cudnnReduceTensorOp_t reduce_tensor_op_{CUDNN_REDUCE_TENSOR_NORM2};
  cudnnReduceTensorIndices_t reduce_indices_{CUDNN_REDUCE_TENSOR_NO_INDICES};
  cudnnReduceTensorDescriptor_t reduce_tensor_descriptor_{nullptr};
  cudnnTensorDescriptor_t inputA_descriptor_{nullptr};
  cudnnTensorDescriptor_t outputC_descriptor_{nullptr};
  // basic attribute
  bool is_null_shape_{false};
  bool all_match_{true};
  bool clip_norm_need_broadcast_{false};
  float epsilon_{0.000001f};
  size_t x_dim_{0};
  size_t x_size_{0};
  size_t clip_norm_size_{0};
  size_t clip_norm_cast_size_{0};
  size_t l2_norm_output_size_{0};
  size_t l2_norm_workspace_size_{0};
  size_t output_size_{0};
  // variables are used for `l2_norm' calculation
  std::vector<size_t> axis_;
  std::vector<size_t> x_shape_;
  std::vector<size_t> l2_norm_output_shape_;
  // variables are used for 'clip_norm' coefficient calculation.
  std::vector<size_t> clip_norm_shape_;
  // variables are used for broadcast calculation
  std::vector<size_t> l2_norm_lhs_shape_;    // broadcast
  std::vector<size_t> l2_norm_rhs_shape_;    // broadcast
  std::vector<size_t> l2_norm_ouths_shape_;  // broadcast
  std::vector<size_t> clip_norm_rhs_shape_;  // broadcast
  // final output shape of `ClipByNorm`
  std::vector<size_t> output_shape_;
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_CLIP_BY_NORM_GPU_KERNEL_H_
