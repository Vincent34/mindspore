/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_TRANSPOSE_IMPL_CUH_
#define MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_TRANSPOSE_IMPL_CUH_
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/cuda_common.h"
#define TRANSPOSE_MAX_DIMENSION 26
struct TransposeInfo {
  // In Einsum, the shape's size may be 26
  int shape[26] = {0};
  int perm[26] = {0};
};

template <typename T>
CUDA_LIB_EXPORT cudaError_t CalTranspose(const size_t size, const T *input, const TransposeInfo &info,
                                         const size_t shape_size, T *output, cudaStream_t cuda_stream);

template <typename T>
CUDA_LIB_EXPORT void CalNHWC2NCHWInterface(const size_t size, const size_t shape_size, const T *d_input,
                                           const int64_t *input_shape, const int64_t *input_axis,
                                           const TransposeInfo &info, T *output, cudaStream_t cuda_stream);

template <typename T>
CUDA_LIB_EXPORT void CalNCHW2NHWCInterface(const size_t size, const size_t shape_size, const T *d_input,
                                           const int64_t *input_shape, const int64_t *input_axis,
                                           const TransposeInfo &info, T *output, cudaStream_t cuda_stream);

#endif  // MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_TRANSPOSE_IMPL_CUH_
