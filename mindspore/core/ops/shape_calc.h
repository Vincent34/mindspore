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
#ifndef MINDSPORE_CORE_OPS_SHAPE_CALC_H_
#define MINDSPORE_CORE_OPS_SHAPE_CALC_H_

#include <memory>
#include <vector>
#include "include/common/utils/utils.h"
#include "ir/anf.h"
#include "ir/functor.h"
#include "mindapi/base/macros.h"
#include "ops/base_operator.h"

namespace mindspore {
namespace ops {
constexpr auto kAttrValueDepend = "value_depend";

class MIND_API ShapeCalc : public BaseOperator {
 public:
  MIND_API_BASE_MEMBER(ShapeCalc);
  ShapeCalc() : BaseOperator(kShapeCalc) { InitIOName({"inputs"}, {"outputs"}); }

  ShapeCalcFunctorPtr get_functor() const;
  std::vector<int64_t> get_value_depend() const;
};
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_SHAPE_CALC_H_
