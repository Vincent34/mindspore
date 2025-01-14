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
#ifndef NNACL_TILE_PARAMETER_H_
#define NNACL_TILE_PARAMETER_H_

#include "nnacl/op_base.h"

typedef struct TileParameter {
  OpParameter op_parameter_;
  size_t dims_size_;
  int dims_[MAX_SHAPE_SIZE];
  int multiples_[MAX_SHAPE_SIZE];

  /* used in micro */
  int in_dim_;
  int in_shape_[MAX_SHAPE_SIZE];
  int out_shape_[MAX_SHAPE_SIZE];
  int in_strides_[MAX_SHAPE_SIZE];
  int out_strides_[MAX_SHAPE_SIZE];
} TileParameter;

#endif  // NNACL_TILE_PARAMETER_H_
