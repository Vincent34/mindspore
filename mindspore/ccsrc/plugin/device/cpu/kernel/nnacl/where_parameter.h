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
#ifndef NNACL_WHERE_PARAMETER_H_
#define NNACL_WHERE_PARAMETER_H_

#include "nnacl/op_base.h"

typedef struct WhereParameter {
  // primitive parameter
  OpParameter op_parameter_;

  // other parameter
  int condition_num_;
  int x_num_;
  int y_num_;
  int max_num_;

  int rank_;
  int thread_num_;
} WhereParameter;

#endif  // NNACL_WHERE_PARAMETER_H_
