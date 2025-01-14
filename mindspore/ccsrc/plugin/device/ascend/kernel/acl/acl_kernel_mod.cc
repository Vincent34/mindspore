/**
 * Copyright 2022-2023 Huawei Technologies Co., Ltd
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
#include "plugin/device/ascend/kernel/acl/acl_kernel_mod.h"
#include <functional>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include "ir/tensor.h"
#include "runtime/stream.h"
#include "runtime/device/kernel_runtime.h"
#include "plugin/device/ascend/optimizer/ascend_helper.h"
#include "transform/acl_ir/acl_helper.h"
#include "abstract/ops/primitive_infer_map.h"

namespace mindspore {
namespace kernel {
namespace {
std::string MsTensorDescString(const TensorParams &param) {
  std::stringstream ss;
  ss << "[TensorDesc] ";
  ss << "DataType = " << TypeIdToString(param.data_type);
  ss << ", Origin Format = " << param.ori_format;
  ss << ", Origin Shape = " << VectorToString(param.ori_shape);
  ss << ", Device Format = " << param.dev_format;
  ss << ", Device Shape = " << VectorToString(param.dev_shape);
  return ss.str();
}

tensor::TensorPtr GetDependValueTensor(const AddressPtr &address, const TypeId type, const ShapeVector &shape,
                                       void *stream_ptr) {
  MS_EXCEPTION_IF_NULL(stream_ptr);
  if (address != nullptr && address->addr != nullptr) {
    auto tensor = std::make_shared<tensor::Tensor>(type, shape);
    MS_EXCEPTION_IF_NULL(tensor);
    auto status = aclrtMemcpyAsync(tensor->data_c(), tensor->Size(), address->addr, address->size,
                                   ACL_MEMCPY_DEVICE_TO_HOST, stream_ptr);
    if (status != ACL_ERROR_NONE) {
      MS_LOG(EXCEPTION) << "aclrtMemcpyAsync depend tensor failed! tensor size is " << tensor->Size()
                        << " and address size is " << address->size;
    }
    auto sync_status = rtStreamSynchronize(stream_ptr);
    if (sync_status != RT_ERROR_NONE) {
      MS_LOG(EXCEPTION) << "rtStreamSynchronize depend tensor failed! tensor size is " << tensor->Size()
                        << " and address size is " << address->size;
    }
    return tensor;
  }
  return nullptr;
}
}  // namespace

void AclKernelMod::PackageInput(const size_t idx, const std::string &format, ShapeVector *shape) {
  MS_EXCEPTION_IF_NULL(shape);
  TensorParams params;
  auto device_type = input_device_types_[idx];
  params.data_type = device_type;
  if (!format.empty()) {
    params.ori_format = format;
  } else {
    params.ori_format =
      transform::AclHelper::ConvertOriginShapeAndFormat(kernel_name_, idx, input_device_formats_[idx], shape);
  }

  params.ori_shape = *shape;
  params.dev_format = input_device_formats_[idx];
  auto groups = transform::AclHelper::GetFracZGroupFromAttr(primitive_ptr_);
  params.dev_shape = trans::TransShapeToDevice(params.ori_shape, params.dev_format, device_type, groups);
  (void)input_params_.emplace_back(params);
}

void AclKernelMod::PackageOutput(const size_t idx, const ShapeVector &shape) {
  TensorParams params;
  auto device_type = output_device_types_[idx];
  params.data_type = device_type;
  size_t type_size = GetTypeByte(TypeIdToType(params.data_type));
  size_t tensor_size = 0;
  params.ori_format = shape.size() == kDim4 ? kOpFormat_NCHW : kOpFormat_DEFAULT;
  params.dev_format = output_device_formats_[idx];
  auto groups = transform::AclHelper::GetFracZGroupFromAttr(primitive_ptr_);
  const auto &dev_shape = trans::TransShapeToDevice(shape, params.dev_format, device_type, groups);
  tensor_size = dev_shape.empty()
                  ? type_size
                  : std::accumulate(dev_shape.begin(), dev_shape.end(), type_size, std::multiplies<size_t>());
  tensor_size = std::max(tensor_size, type_size);
  params.ori_shape = shape;
  params.dev_shape = dev_shape;
  (void)output_params_.emplace_back(params);
  (void)output_size_list_.emplace_back(tensor_size);
}

void AclKernelMod::SetPrimitive(const PrimitivePtr &primitive) {
  MS_EXCEPTION_IF_NULL(primitive);
  primitive_ptr_ = primitive;
  kernel_name_ = primitive_ptr_->name();
}

void AclKernelMod::GetInputInfo(const std::vector<KernelTensorPtr> &inputs) {
  if (input_device_formats_.size() != inputs.size()) {
    MS_LOG(INTERNAL_EXCEPTION) << "Acl kernel's input size is not equal with format's size:"
                               << input_device_formats_.size() << " - input's size:" << inputs.size();
  }

  std::string format = transform::AclHelper::GetFormatFromAttr(primitive_ptr_);
  size_t idx = 0;
  for (auto &input : inputs) {
    MS_EXCEPTION_IF_NULL(input);
    auto shape = input->GetShapeVector();
    if (!IsValidShape(shape)) {
      // early stop if any input shape contains -1/-2, which means input shape is dynamic
      MS_LOG(INTERNAL_EXCEPTION) << "In Resize function, input shape must be valid!";
    }
    PackageInput(idx, format, &shape);
    ++idx;
  }
}

int AclKernelMod::GetOutputInfo(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &outputs) {
  int ret = KRET_OK;
  if (output_device_formats_.size() != outputs.size()) {
    MS_LOG(INTERNAL_EXCEPTION) << "Acl kernel's output size is not equal with format's size:"
                               << output_device_formats_.size() << " - output's size:" << outputs.size();
  }

  size_t idx = 0;
  for (auto &output : outputs) {
    MS_EXCEPTION_IF_NULL(output);
    auto shape = output->GetShapeVector();
    if (!IsValidShape(shape)) {
      shape = output->GetMaxShape();
      if (shape.empty()) {
        auto primitive = base_operator->GetPrim();
        MS_ERROR_IF_NULL(primitive);
        MS_LOG(EXCEPTION) << "For " << primitive->name()
                          << ", the max_shape should not be empty when input shape is known.";
      }
      ret = KRET_UNKNOWN_OUT_SHAPE;
    }
    PackageOutput(idx, shape);
    ++idx;
  }
  return ret;
}

int AclKernelMod::Resize(const std::vector<KernelTensorPtr> &inputs, const std::vector<KernelTensorPtr> &outputs,
                         const std::map<uint32_t, tensor::TensorPtr> &inputs_on_host) {
  int ret = KRET_OK;
  primitive_ptr_ = op_->GetPrim();
  MS_ERROR_IF_NULL(primitive_ptr_);
  kernel_name_ = primitive_ptr_->name();

  this->inputs_ = inputs;
  this->outputs_ = outputs;

  input_size_list_.clear();
  output_size_list_.clear();
  workspace_size_list_.clear();
  input_params_.clear();
  output_params_.clear();
  ms_attr_str_.clear();

  GetInputInfo(inputs);
  ret = GetOutputInfo(op_, outputs);

  inputs_on_host_ = inputs_on_host;
  return ret;
}

std::string AclKernelMod::DebugString() const {
  std::stringstream ss;
  ss << "[MsLaunchInfo]OpType:" << kernel_name_ << std::endl;
  for (size_t i = 0; i < input_params_.size(); ++i) {
    auto param = input_params_[i];
    ss << "InputDesc[" << i << "]:";
    ss << MsTensorDescString(param) << std::endl;
  }
  for (size_t i = 0; i < ms_attr_str_.size(); ++i) {
    ss << "Attr[" << i << "] " << ms_attr_str_[i] << std::endl;
  }
  for (size_t i = 0; i < output_params_.size(); ++i) {
    auto param = output_params_[i];
    ss << "OutputDesc[" << i << "]:";
    ss << MsTensorDescString(param) << std::endl;
  }
  return ss.str();
}

bool AclKernelMod::Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
                          const std::vector<AddressPtr> &outputs, void *stream_ptr) {
  if (stream_ptr == nullptr) {
    MS_LOG(ERROR) << "stream_ptr should not be nullptr.";
    return false;
  }

  if (need_convert_host_tensor_) {
    auto anf_node = anf_node_.lock();
    MS_EXCEPTION_IF_NULL(anf_node);
    auto cnode = anf_node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    std::set<int64_t> depend_list = abstract::GetValueDependArgIndices(cnode);
    inputs_on_host_.clear();
    for (size_t i = 0; i < inputs.size(); ++i) {
      if (depend_list.find(i) != depend_list.end()) {
        auto input_param = input_params_.at(i);
        auto depended_value = GetDependValueTensor(inputs[i], input_param.data_type, input_param.ori_shape, stream_ptr);
        if (depended_value == nullptr) {
          continue;
        }
        auto emplace_ret = inputs_on_host_.try_emplace(i, depended_value);
        if (!emplace_ret.second) {
          MS_LOG(EXCEPTION) << "Insert depended value to map failed.";
        }
      }
    }
    need_convert_host_tensor_ = false;
  }

  MS_EXCEPTION_IF_NULL(converter_);
  converter_->Reset();
  converter_->ConvertToAclOpType(kernel_name_);
  converter_->ResizeAclOpInputs(primitive_ptr_);
  converter_->ConvertAttrToAclInput(primitive_ptr_->attrs(), kernel_name_, &inputs_on_host_);
  converter_->ConvertToAclInput(primitive_ptr_, inputs_on_host_, inputs, input_params_);
  converter_->ConvertToAclOutput(kernel_name_, outputs, output_params_);
  converter_->ConvertToAclAttr(primitive_ptr_->attrs(), kernel_name_, &ms_attr_str_);
  if (!inputs_on_host_.empty()) {
    converter_->ConvertInputToAclAttr(inputs_on_host_, kernel_name_);
  }
  converter_->SetRunnerSpecialInfo(kernel_name_, output_params_);
  // cppcheck-suppress unreadVariable
  auto lock = device::KernelRuntime::LockRuntime(stream_ptr);
  MS_LOG(DEBUG) << this->DebugString();
  MS_LOG(DEBUG) << converter_->DebugString();
  converter_->Run(stream_ptr);
  return true;
}

void AclKernelMod::SetDeviceInfo(const std::vector<std::string> &input_device_formats,
                                 const std::vector<std::string> &output_device_formats,
                                 const std::vector<TypeId> &input_device_types,
                                 const std::vector<TypeId> &output_device_types) {
  if (input_device_formats.size() != input_device_types.size()) {
    MS_LOG(INTERNAL_EXCEPTION) << "Acl kernel's input size is not equal with format's size:"
                               << input_device_formats.size() << " and type's size:" << input_device_types.size();
  }
  if (output_device_formats.size() != output_device_types.size()) {
    MS_LOG(INTERNAL_EXCEPTION) << "Acl kernel's output size is not equal with format's size:"
                               << output_device_formats.size() << " and type's size:" << output_device_types.size();
  }
  input_device_formats_ = input_device_formats;
  output_device_formats_ = output_device_formats;
  input_device_types_ = input_device_types;
  output_device_types_ = output_device_types;
}

std::vector<TaskInfoPtr> AclKernelMod::GenTask(const std::vector<AddressPtr> &, const std::vector<AddressPtr> &,
                                               const std::vector<AddressPtr> &, uint32_t) {
  MS_LOG(EXCEPTION) << "Acl kernels do not support task sink mode.";
  return {};
}

void AclKernelMod::SyncOutputShape() {
  MS_EXCEPTION_IF_NULL(converter_);
  std::vector<std::vector<int64_t>> output_shape = converter_->SyncData();
  for (size_t i = 0; i < output_shape.size(); ++i) {
    outputs_[i]->SetShapeVector(output_shape[i]);
  }
}

bool AclKernelMod::IsNeedRetrieveOutputShape() {
  MS_EXCEPTION_IF_NULL(converter_);
  return converter_->is_need_retrieve_output_shape();
}
}  // namespace kernel
}  // namespace mindspore
