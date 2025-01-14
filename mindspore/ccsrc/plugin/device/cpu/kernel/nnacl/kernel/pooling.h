/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#ifndef NNACL_KERNEL_POOLING_H_
#define NNACL_KERNEL_POOLING_H_

#include "nnacl/op_base.h"
#include "nnacl/tensor_c.h"
#include "nnacl/kernel.h"

typedef struct PoolingComputeParam {
  int input_w_;
  int input_h_;
  int input_batch_;
  int input_channel_;
  int output_w_;
  int output_h_;
  int output_batch_;
  int output_channel_;
  int window_w_;
  int window_h_;
  float minf;
  float maxf;
} PoolingComputeParam;

typedef struct PoolingStruct {
  KernelBase base_;
  PoolingComputeParam compute_;
  int data_type_;
} PoolingStruct;

KernelBase *CreatePooling(OpParameter *param, int data_type);

#endif  // NNACL_KERNEL_POOLING_H_
