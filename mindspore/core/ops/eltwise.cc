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

#include "ops/eltwise.h"
#include "mindapi/base/shared_ptr.h"
#include "mindapi/ir/value.h"
#include "ops/op_name.h"
#include "ops/primitive_c.h"
#include "utils/log_adapter.h"
#include "mindapi/src/helper.h"

namespace mindspore {
namespace ops {
MIND_API_OPERATOR_IMPL(Eltwise, BaseOperator);
void Eltwise::Init(const EltwiseMode &mode) { this->set_mode(mode); }
void Eltwise::set_mode(const EltwiseMode &mode) {
  int64_t m = mode;
  (void)this->AddAttr(kMode, api::MakeValue(m));
}
EltwiseMode Eltwise::get_mode() const {
  auto value_ptr = this->GetAttr(kMode);
  return EltwiseMode(GetValue<int64_t>(value_ptr));
}
REGISTER_PRIMITIVE_C(kNameEltwise, Eltwise);
}  // namespace ops
}  // namespace mindspore
