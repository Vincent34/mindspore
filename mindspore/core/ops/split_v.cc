/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#include "ops/split_v.h"
#include <set>

#include "ops/op_utils.h"
#include "utils/tensor_construct_utils.h"
#include "abstract/primitive_infer_map.h"

namespace mindspore {
namespace ops {
namespace {
abstract::TupleShapePtr InferShape(const PrimitivePtr &primitive, const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  CheckAndConvertUtils::CheckInteger("input numbers", input_args.size(), kEqual, 1, prim_name);
  auto x_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[0]->BuildShape())[kShape];
  auto x_rank = SizeToLong(x_shape.size());
  CheckAndConvertUtils::CheckInteger("x_rank", x_rank, kGreaterEqual, 1, prim_name);
  auto split_dim = GetValue<int64_t>(primitive->GetAttr("split_dim"));
  CheckAndConvertUtils::CheckInRange("split_dim", split_dim, kIncludeLeft, {-x_rank, x_rank}, prim_name);
  if (split_dim < 0) {
    split_dim += x_rank;
  }
  auto shape_of_split_dim = x_shape[split_dim];
  auto num_split = GetValue<int64_t>(primitive->GetAttr("num_split"));
  CheckAndConvertUtils::CheckInteger("num_split", num_split, kGreaterEqual, 1, prim_name);
  auto size_splits = GetValue<std::vector<int64_t>>(primitive->GetAttr(kSizeSplits));
  CheckAndConvertUtils::Check("num_split", num_split, kEqual, "rank of size_splits", SizeToLong(size_splits.size()),
                              prim_name);
  auto default_idx = std::find(size_splits.begin(), size_splits.end(), -1);
  if (default_idx == size_splits.end()) {
    int sum_of_size_splits = 0;
    for (int64_t i = 0; i < num_split; i++) {
      CheckAndConvertUtils::CheckInRange("elements of size_splits", size_splits[i], kIncludeBoth,
                                         {0, shape_of_split_dim}, prim_name);
      sum_of_size_splits += size_splits[i];
    }
    CheckAndConvertUtils::Check("sum of size_splits", sum_of_size_splits, kEqual, "dimension of value along split_dim",
                                shape_of_split_dim, prim_name);
  } else {
    size_splits.erase(default_idx);
    auto excessive_default_idx = std::find(size_splits.begin(), size_splits.end(), -1);
    if (excessive_default_idx != size_splits.end()) {
      MS_EXCEPTION(ValueError) << "Got more than one default value -1 in size_splits.";
    } else {
      int sum_of_size_splits = 0;
      for (int64_t i = 0; i < num_split - 1; i++) {
        CheckAndConvertUtils::CheckInRange("elements of size_splits", size_splits[i], kIncludeBoth,
                                           {0, shape_of_split_dim}, prim_name);
        sum_of_size_splits += size_splits[i];
      }
      auto default_value = shape_of_split_dim - sum_of_size_splits;
      size_splits.insert(default_idx, default_value);
    }
  }
  std::vector<abstract::BaseShapePtr> shape_tuple;
  for (int64_t i = 0; i < num_split; i++) {
    auto shape = x_shape;
    shape[split_dim] = size_splits[i];
    abstract::ShapePtr out_shape = std::make_shared<abstract::Shape>(shape);
    shape_tuple.push_back(out_shape);
  }
  return std::make_shared<abstract::TupleShape>(shape_tuple);
}

TuplePtr InferType(const PrimitivePtr &prim, const std::vector<AbstractBasePtr> &input_args) {
  for (const auto &item : input_args) {
    MS_EXCEPTION_IF_NULL(item);
  }
  auto num_split = GetValue<int64_t>(prim->GetAttr("num_split"));
  auto infer_type = input_args[0]->BuildType();
  MS_EXCEPTION_IF_NULL(infer_type);
  const std::set<TypePtr> valid_types = {kInt8,   kInt16,  kInt32,  kInt64,   kUInt8,
                                         kUInt16, kUInt32, kUInt64, kFloat16, kFloat32};
  auto type = CheckAndConvertUtils::CheckTensorTypeValid("input_x", infer_type, valid_types, prim->name());
  std::vector<TypePtr> type_tuple;
  for (int64_t i = 0; i < num_split; i++) {
    type_tuple.push_back(type);
  }
  return std::make_shared<Tuple>(type_tuple);
}
}  // namespace

AbstractBasePtr SplitVInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                            const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto infertype = InferType(primitive, input_args);
  auto infershape = InferShape(primitive, input_args);
  return abstract::MakeAbstract(infershape, infertype);
}
REGISTER_PRIMITIVE_EVAL_IMPL(SplitV, prim::kPrimSplitV, SplitVInfer, nullptr, true);
}  // namespace ops
}  // namespace mindspore
