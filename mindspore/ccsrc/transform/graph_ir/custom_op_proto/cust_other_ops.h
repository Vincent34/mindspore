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

#ifndef MINDSPORE_CCSRC_GRAPH_IR_CUSTOM_OP_PROTO_CUST_OTHER_OPS_H_
#define MINDSPORE_CCSRC_GRAPH_IR_CUSTOM_OP_PROTO_CUST_OTHER_OPS_H_

#include "graph/operator_reg.h"
#include "graph/operator.h"

/* clang-format off */

namespace ge {
REG_OP(KVCacheMgr)
  .INPUT(past, TensorType({DT_FLOAT16}))
  .INPUT(cur, TensorType({DT_FLOAT16}))
  .INPUT(index, TensorType({DT_INT32}))
  .OUTPUT(past, TensorType({DT_FLOAT16}))
  .OP_END_FACTORY_REG(KVCacheMgr)
}  // namespace ge
#endif  // MINDSPORE_CCSRC_GRAPH_IR_CUSTOM_OP_PROTO_CUST_OTHER_OPS_H_
