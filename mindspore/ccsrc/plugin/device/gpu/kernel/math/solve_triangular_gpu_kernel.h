/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_MATH_MATRIX_TRIANGULAR_SOLVE_GPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_MATH_MATRIX_TRIANGULAR_SOLVE_GPU_KERNEL_H_
#include <cublas_v2.h>
#include <cuda_runtime_api.h>
#include <type_traits>
#include <vector>
#include <string>
#include <map>
#include <utility>
#include "plugin/device/gpu/kernel/gpu_kernel.h"
#include "plugin/device/gpu/kernel/gpu_kernel_factory.h"
#include "plugin/device/gpu/kernel/kernel_constants.h"
#include "plugin/device/gpu/kernel/gpu_kernel_utils.h"

namespace mindspore {
namespace kernel {
constexpr auto kAVectorxDimNum = 1;
constexpr auto kAMatrixDimNum = 2;
constexpr size_t kRowIndex = 2;
constexpr size_t kColIndex = 1;
constexpr size_t kShape3D = 3;
constexpr size_t kIndexAArray = 0;
constexpr size_t kIndexDstArray = 1;
constexpr size_t kIndexBBuffer = 2;
constexpr size_t kIndexBTransposeShape = 3;
constexpr size_t kIndexBTransposeAxis = 4;

class SolveTriangularGpuKernelMod : public NativeGpuKernelMod, public MatchKernelHelper<SolveTriangularGpuKernelMod> {
 public:
  SolveTriangularGpuKernelMod() = default;
  ~SolveTriangularGpuKernelMod() override = default;

  bool Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
            const std::vector<KernelTensorPtr> &outputs) override;

  int Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
             const std::vector<KernelTensorPtr> &outputs, const std::map<uint32_t, tensor::TensorPtr> &) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override {
    if (is_null_input_) {
      return true;
    }
    cuda_stream_ = reinterpret_cast<cudaStream_t>(stream_ptr);
    return kernel_func_(this, inputs, workspace, outputs);
  }

  const std::vector<std::pair<KernelAttr, KernelRunFunc>> &GetFuncList() const override;

  std::vector<KernelAttr> GetOpSupport() override { return OpSupport(); }

 private:
  template <typename T>
  bool LaunchKernel(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
                    const std::vector<AddressPtr> &outputs);

  size_t m_{0};
  size_t n_{0};
  size_t batch_{1};
  int lda_{0};
  int ldb_{0};
  bool is_null_input_{false};

  cublasHandle_t blas_handle_{nullptr};
  cublasFillMode_t uplo_{CUBLAS_FILL_MODE_UPPER};
  cublasOperation_t trans_{CUBLAS_OP_N};
  cublasDiagType_t unit_diagonal_{CUBLAS_DIAG_NON_UNIT};

  cudaStream_t cuda_stream_{nullptr};
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_MATH_MATRIX_TRIANGULAR_SOLVE_GPU_KERNEL_H_
