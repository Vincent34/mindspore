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

#include "src/runtime/kernel/arm/int8/matmul_int8.h"
#include "src/runtime/kernel/arm/int8/matmul_dynamic_int8.h"
#include "nnacl/int8/matmul_int8.h"
#include "nnacl/common_func.h"
#include "include/errorcode.h"
#include "src/kernel_registry.h"

using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_MatMulFusion;

namespace mindspore::kernel {
int MatmulInt8CPUKernel::Prepare() {
  CHECK_LESS_RETURN(in_tensors_.size(), 2);
  CHECK_LESS_RETURN(out_tensors_.size(), 1);
  InitParameter();

  auto ret = MatmulBaseInt8CPUKernel::Prepare();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "ParallelLaunch failed";
    return ret;
  }

  if (!InferShapeDone()) {
    return RET_OK;
  }

  return ReSize();
}

int MatmulInt8CPUKernel::ReSize() {
  int batch = 1;
  auto x_shape = in_tensors_.at(0)->shape();
  auto o_shape = out_tensors_.at(0)->shape();
  MS_ASSERT(x_shape.size() >= 2);
  for (size_t i = 0; i < x_shape.size() - 2; ++i) {
    batch *= x_shape[i];
  }
  param_->batch = batch;
  MS_ASSERT(o_shape.size() >= 2);
  param_->row_ = o_shape[o_shape.size() - 2];
  param_->col_ = o_shape[o_shape.size() - 1];
  param_->deep_ = param_->a_transpose_ ? x_shape[x_shape.size() - 2] : x_shape[x_shape.size() - 1];

  auto ret = MatmulBaseInt8CPUKernel::ReSize();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "MatmulBaseInt8CPUKernel failed";
    return ret;
  }
  return RET_OK;
}

kernel::InnerKernel *MatmulInt8CPUKernelCreator(const std::vector<lite::Tensor *> &inputs,
                                                const std::vector<lite::Tensor *> &outputs, OpParameter *parameter,
                                                const lite::Context *ctx, const kernel::KernelKey &desc) {
  if (parameter == nullptr) {
    MS_LOG(ERROR) << "parameter is nullptr.";
    return nullptr;
  }

  InnerKernel *kernel = nullptr;
  if (parameter->quant_type_ == schema::QuantType_QUANT_ALL) {
    kernel =
      new (std::nothrow) MatmulInt8CPUKernel(parameter, inputs, outputs, static_cast<const lite::InnerContext *>(ctx));
  } else if (parameter->quant_type_ == schema::QuantType_QUANT_DYNAMIC) {
    kernel = new (std::nothrow)
      MatmulDynamicInt8CPUKernel(parameter, inputs, outputs, static_cast<const lite::InnerContext *>(ctx));
  } else {
    MS_LOG(ERROR) << "kernel: " << parameter->name_ << " is unsupported quant type:" << parameter->quant_type_;
    free(parameter);
    return nullptr;
  }
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel: " << parameter->name_ << "is nullptr.";
    free(parameter);
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeInt8, PrimitiveType_MatMulFusion, MatmulInt8CPUKernelCreator)
}  // namespace mindspore::kernel
