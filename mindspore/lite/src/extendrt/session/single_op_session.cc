/**
 * Copyright 2019-2023  uawei Technologies Co., Ltd
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

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <map>

#include "src/extendrt/session/single_op_session.h"
#include "src/extendrt/infer_device_address.h"

#include "plugin/factory/ms_factory.h"
#include "include/common/utils/anfalgo.h"
#include "include/backend/anf_runtime_algorithm.h"
#include "kernel/common_utils.h"
#include "plugin/device/cpu/kernel/cpu_kernel_mod.h"
#include "src/extendrt/utils/kernel_build_utils.h"
#include "src/extendrt/kernel/ascend/plugin/ascend_kernel_plugin.h"
#include "src/common/common.h"
#include "src/litert/cxx_api/tensor/tensor_impl.h"
#include "mindspore/core/ops/custom.h"
#include "extendrt/session/factory.h"
#include "extendrt/utils/runtime_utils.h"
#include "extendrt/utils/tensor_default_impl.h"
#include "extendrt/utils/func_graph_utils.h"
#include "tools/optimizer/common/gllo_utils.h"

namespace mindspore {
const size_t tensor_max_size = 0x1000000;
constexpr auto kNameCustomAscend = "CustomAscend";

Status SingleOpInferSession::AscendInit(const std::shared_ptr<Context> &context) {
  auto device_list = context->MutableDeviceInfo();
  for (const auto &device_info : device_list) {
    if (device_info == nullptr) {
      MS_LOG(ERROR) << "Device info get from Context cannot be nullptr";
      return kLiteError;
    }
    if (device_info->GetDeviceType() == DeviceType::kAscend) {
      if (!kernel::AscendKernelPlugin::Register()) {
        MS_LOG(ERROR) << "Failed to register Ascend plugin";
        return kLiteError;
      }
      auto ascend_device_info = device_info->Cast<mindspore::AscendDeviceInfo>();
      if (ascend_device_info == nullptr) {
        MS_LOG(ERROR) << "Failed to cast device info to AscendDeviceInfo";
        return kLiteError;
      }
      device_id_ = ascend_device_info->GetDeviceID();
      return kSuccess;
    }
  }
  MS_LOG(DEBUG) << "There is no ascend device info, no need to register ascend plugin.";
  return kSuccess;
}

Status SingleOpInferSession::Init(const std::shared_ptr<Context> &context, const ConfigInfos &config_info) {
  MS_LOG(INFO) << "SingleOpInferSession::Init";
  if (context == nullptr) {
    MS_LOG(ERROR) << "Input argument context cannot be nullptr";
    return kLiteError;
  }
  if (AscendInit(context) != kSuccess) {
    MS_LOG(ERROR) << "Init ascend failed.";
    return kLiteError;
  }
  config_infos_ = config_info;
  return kSuccess;
}

void SingleOpInferSession::SetCustomAscendOpAttrs(const kernel::BaseOperatorPtr &op) {
  if (config_infos_.find(lite::kAscendContextSection) == config_infos_.end() &&
      config_infos_.find("inner_common") == config_infos_.end()) {
    MS_LOG(DEBUG) << "There is no ascend context info in config infos.";
    return;
  }
  // set custom op attrs
  auto custom_op = std::dynamic_pointer_cast<ops::Custom>(op);
  if (custom_op == nullptr) {
    MS_LOG(ERROR) << "Cast Custom op failed, can't set custom attrs.";
    return;
  }
  auto dst_prim = custom_op->GetPrim();
  if (dst_prim == nullptr) {
    MS_LOG(ERROR) << "Get prim from custom op failed.";
    return;
  }
  auto share_mem = config_infos_["inner_common"];
  if (share_mem.find("inner_calc_workspace_size") != share_mem.end()) {
    auto value = share_mem["inner_calc_workspace_size"];
    is_multi_model_sharing_mem_prepare_ = value == "true" ? true : false;
    dst_prim->AddAttr("inner_calc_workspace_size", MakeValue(is_multi_model_sharing_mem_prepare_));
    MS_LOG(INFO) << "inner_calc_workspace_size: " << is_multi_model_sharing_mem_prepare_;
  }
  if (share_mem.find("inner_sharing_workspace") != share_mem.end()) {
    auto value = share_mem["inner_sharing_workspace"];
    bool is_inner_sharing_workspace = value == "true" ? true : false;
    dst_prim->AddAttr("inner_sharing_workspace", MakeValue(is_inner_sharing_workspace));
    MS_LOG(INFO) << "is_inner_sharing_workspace: " << is_inner_sharing_workspace;
  }
  auto ascend_context = config_infos_[lite::kAscendContextSection];
  std::string profiling_path;
  if (ascend_context.find(lite::kProfilingPathKey) != ascend_context.end()) {
    profiling_path = ascend_context[lite::kProfilingPathKey];
    dst_prim->AddAttr(lite::kProfilingPathKey, MakeValue(profiling_path));
  }
  if (ascend_context.find(lite::kDumpPathKey) != ascend_context.end()) {
    if (!profiling_path.empty()) {
      MS_LOG(ERROR) << "Profiling and dump can't be set at the same time.";
      return;
    }
    auto dump_path = ascend_context[lite::kDumpPathKey];
    dst_prim->AddAttr(lite::kDumpPathKey, MakeValue(dump_path));
  }
}

std::tuple<kernel::KernelModPtr, kernel::KernelArgs> SingleOpInferSession::BuildCustomAscendKernelImpl(
  const CNodePtr &cnode) {
  auto kernel_name = kNameCustomAscend;
  std::shared_ptr<kernel::KernelMod> kernel_mod = kernel::Factory<kernel::KernelMod>::Instance().Create(kernel_name);
  if (kernel_mod == nullptr) {
    MS_LOG(ERROR) << "Kernel mod is nullptr, kernel name: " << kernel_name;
    return std::make_tuple(nullptr, kernel::KernelArgs{});
  }
  MS_LOG(INFO) << "SingleOpInferSession::Kernels " << kernel_name;
  kernel_mod->SetDevicedId(device_id_);

  auto make_kernel_tensor = [](TypeId type_id, const ShapeVector &shape) {
    auto kernel_tensor = std::make_shared<kernel::KernelTensor>();
    auto base = std::make_shared<mindspore::abstract::AbstractTensor>(TypeIdToType(type_id),
                                                                      std::make_shared<abstract::Shape>(shape));
    kernel::TensorInfo tensor_info;
    tensor_info.base_ = base;
    kernel_tensor->SetTensorInfo(tensor_info);
    return kernel_tensor;
  };

  kernel::KernelArgs args;
  BaseOperatorPtr op;
  if (!FuncGraphUtils::GetCNodeOperator(cnode, &op)) {
    MS_LOG(ERROR) << "Failed to create operator for cnode " << cnode->fullname_with_scope();
    return std::make_tuple(nullptr, kernel::KernelArgs{});
  }
  std::vector<tensor::TensorPtr> tensor_cache;
  std::map<AnfWithOutIndex, kernel::KernelTensorPtr> kernel_tensor_map;
  std::vector<AnfWithOutIndex> inputs;
  std::vector<AnfWithOutIndex> outputs;
  FuncGraphUtils::GetCNodeInputsOutputs(cnode, &inputs, &outputs);
  for (size_t i = 0; i < inputs.size(); i++) {
    auto &input = inputs[i];
    auto data_type = FuncGraphUtils::GetTensorDataType(input);
    auto shape = FuncGraphUtils::GetTensorShape(input);
    auto kernel_tensor = make_kernel_tensor(static_cast<TypeId>(data_type), shape);
    auto tensor_data = FuncGraphUtils::GetConstNodeValue(input.first);
    if (tensor_data) {
      tensor_cache.push_back(tensor_data);
      kernel_tensor->SetData(std::make_shared<kernel::Address>(tensor_data->data_c(), tensor_data->Size()));
    }
    args.inputs.push_back(kernel_tensor);
    kernel_tensor_map[input] = kernel_tensor;
  }
  for (size_t i = 0; i < outputs.size(); i++) {
    auto &output = outputs[i];
    kernel::KernelTensorPtr kernel_tensor;
    auto it = kernel_tensor_map.find(output);
    if (it != kernel_tensor_map.end()) {  // use input as output
      kernel_tensor = it->second;
    } else {
      auto data_type = FuncGraphUtils::GetTensorDataType(output);
      auto shape = FuncGraphUtils::GetTensorShape(output);
      kernel_tensor = make_kernel_tensor(static_cast<TypeId>(data_type), shape);
    }
    args.outputs.push_back(kernel_tensor);
  }
  SetCustomAscendOpAttrs(op);
  auto ret = kernel_mod->Init_(op, args.inputs, args.outputs);
  MS_LOG(INFO) << "SingleOpInferSession::Kernels ret " << ret;
  if (!ret) {
    MS_LOG(ERROR) << "kernel init failed " << kernel_name;
    return std::make_tuple(nullptr, kernel::KernelArgs{});
  }
  if (is_multi_model_sharing_mem_prepare_) {
    MS_LOG(INFO) << "is multi model sharing mem prepare";
    return std::make_tuple(nullptr, kernel::KernelArgs{});
  }
  // remove const input, OM graph data input
  args.inputs = kernel_mod->GetInputs();
  args.outputs = kernel_mod->GetOutputs();
  return std::make_tuple(kernel_mod, args);
}

Status SingleOpInferSession::BuildCustomAscendKernel(const CNodePtr &cnode) {
  kernel::KernelModPtr kernel_mod;
  kernel::KernelArgs args;
  std::tie(kernel_mod, args) = BuildCustomAscendKernelImpl(cnode);
  if (kernel_mod == nullptr) {
    MS_LOG(ERROR) << "Build ascend kernel failed for node: " << cnode->fullname_with_scope();
    return kLiteError;
  }
  kernel_mod_ = kernel_mod;
  kernel_args_ = args;
  return kSuccess;
}

Status SingleOpInferSession::InitInputOutputInfos(const FuncGraphPtr &graph) {
  std::vector<AnfWithOutIndex> input_tensors;
  std::vector<AnfWithOutIndex> output_tensors;
  FuncGraphUtils::GetFuncGraphInputs(graph, &input_tensors);
  FuncGraphUtils::GetFuncGraphOutputs(graph, &output_tensors);
  if (kernel_args_.inputs.size() != input_tensors.size()) {
    MS_LOG(ERROR) << "Graph inputs size " << input_tensors.size() << " != custom inputs size "
                  << kernel_args_.inputs.size();
    return kCoreFailed;
  }
  if (kernel_args_.outputs.size() != output_tensors.size()) {
    MS_LOG(ERROR) << "Graph outputs size " << output_tensors.size() << " != custom inputs size "
                  << kernel_args_.outputs.size();
    return kCoreFailed;
  }
  for (size_t i = 0; i < input_tensors.size(); i++) {
    auto &tensor = input_tensors[i];
    auto &kernel_tensor = kernel_args_.inputs[i];
    auto tensor_name = FuncGraphUtils::GetTensorName(tensor);
    auto data_type = static_cast<DataType>(kernel_tensor->GetDtype());
    auto shape = kernel_tensor->GetShapeVector();
    ShapeVector temp_shape;
    if (IsDynamicShape(shape)) {
      temp_shape = {1};
    } else {
      temp_shape = shape;
    }
    auto tensor_impl = LiteTensorImpl::CreateTensorImpl(tensor_name, data_type, temp_shape, nullptr, 0);
    if ((tensor_impl == nullptr) || (tensor_impl->lite_tensor() == nullptr)) {
      MS_LOG(ERROR) << "Create tensor failed.";
      return kCoreFailed;
    }
    tensor_impl->SetShape(shape);
    inputs_.push_back(tensor_impl);
    input_names_.push_back(FuncGraphUtils::GetTensorName(tensor));
  }
  for (size_t i = 0; i < output_tensors.size(); i++) {
    auto &tensor = output_tensors[i];
    auto &kernel_tensor = kernel_args_.outputs[i];
    auto tensor_name = FuncGraphUtils::GetTensorName(tensor);
    auto data_type = static_cast<DataType>(kernel_tensor->GetDtype());
    auto shape = kernel_tensor->GetShapeVector();
    ShapeVector temp_shape;
    if (IsDynamicShape(shape)) {
      dyn_outshape_ = true;
      MS_LOG(INFO) << "The output shape is dynamic: " << shape;
      temp_shape = {1};
    } else {
      temp_shape = shape;
    }
    auto tensor_impl = LiteTensorImpl::CreateTensorImpl(tensor_name, data_type, temp_shape, nullptr, 0);
    if ((tensor_impl == nullptr) || (tensor_impl->lite_tensor() == nullptr)) {
      MS_LOG(ERROR) << "Create tensor failed.";
      return kCoreFailed;
    }
    tensor_impl->SetShape(shape);
    outputs_.push_back(tensor_impl);
    output_names_.push_back(FuncGraphUtils::GetTensorName(tensor));
  }
  return kSuccess;
}

Status SingleOpInferSession::CompileGraph(FuncGraphPtr graph, const void *data, size_t size, uint32_t *) {
  MS_LOG(INFO) << "SingleOpInferSession::CompileGraph";

  auto nodes = graph->TopoSort(graph->get_return());
  if (nodes.empty()) {
    MS_LOG(ERROR) << "There are no nodes in the graph";
    return mindspore::kLiteNullptr;
  }
  size_t cnode_count = 0;
  for (const auto &node : nodes) {
    auto cnode = node->cast<CNodePtr>();
    if (!cnode || !AnfUtils::IsRealKernel(cnode)) {
      continue;
    }
    std::string kernel_name = common::AnfAlgo::GetCNodeName(cnode);
    if (kernel_name != kNameCustomAscend) {
      MS_LOG(ERROR) << "Only support " << kNameCustomAscend << ", but got " << kernel_name << ", node "
                    << cnode->fullname_with_scope();
      return kLiteError;
    }
    cnode_count += 1;
    if (cnode_count > 1) {
      MS_LOG(ERROR) << "Only support one " << kNameCustomAscend << " node, but got " << kernel_name << ", node "
                    << cnode->fullname_with_scope();
      return kLiteError;
    }
    auto ret = BuildCustomAscendKernel(cnode);
    if (ret != kSuccess) {
      MS_LOG(ERROR) << "Failed to Build custom ascend kernel";
      return ret;
    }
  }
  if (is_multi_model_sharing_mem_prepare_) {
    MS_LOG(INFO) << "is multi model sharing mem prepare";
    return kSuccess;
  }
  auto ret = InitInputOutputInfos(graph);
  if (ret != kSuccess) {
    MS_LOG(ERROR) << "Failed to init graph input and output infos";
    return ret;
  }
  return kSuccess;
}

Status SingleOpInferSession::RunGraph(uint32_t graph_id, const std::vector<lite::Tensor *> &inputs,
                                      std::vector<lite::Tensor *> *outputs, const MSKernelCallBack &before,
                                      const MSKernelCallBack &after) {
  return RunGraph(graph_id, inputs, outputs);
}

Status SingleOpInferSession::SetBackOutputIfDynamic(std::vector<lite::Tensor *> *outputs) {
  if (dyn_outshape_) {
    for (size_t i = 0; i < kernel_args_.outputs.size(); ++i) {
      ShapeVector shape = kernel_args_.outputs[i]->GetShapeVector();
      kernel::AddressPtr addr = kernel_args_.outputs[i]->GetHostData();
      TypeId out_type = kernel_args_.outputs[i]->GetDtype();
      std::vector<int> new_shapes(shape.size());
      std::transform(shape.begin(), shape.end(), new_shapes.begin(), [](int64_t c) { return static_cast<int>(c); });
      (*outputs)[i]->FreeData();
      (*outputs)[i]->set_shape(new_shapes);
      (*outputs)[i]->set_data_type(out_type);
      if ((*outputs)[i]->Size() != addr->size) {
        MS_LOG(ERROR) << "Tensor " << (*outputs)[i]->tensor_name() << " shape invalid.";
        return kLiteError;
      }
      void *dst_ptr = (*outputs)[i]->MutableData();
      if (dst_ptr == nullptr) {
        MS_LOG(ERROR) << "dst_ptr is nullptr.";
        return kLiteError;
      }
      (void)memcpy(dst_ptr, addr->addr, addr->size);
    }
  }
  return kSuccess;
}

Status SingleOpInferSession::InitInputOutputData(const std::vector<lite::Tensor *> &inputs,
                                                 std::vector<lite::Tensor *> *outputs) {
  if (inputs.size() != kernel_args_.inputs.size()) {
    MS_LOG(ERROR) << "Given inputs size " << inputs.size() << " != graph inputs size " << kernel_args_.inputs.size();
    return kLiteError;
  }
  for (size_t i = 0; i < inputs.size(); i++) {
    auto &input = inputs[i];
    auto &kernel_input = kernel_args_.inputs[i];
    if (input->Size() != kernel_input->GetSizeInBytes()) {
      MS_LOG(ERROR) << "Byte size of input " << i << " != the size expected, given size " << input->Size()
                    << ", expected size " << kernel_input->GetSizeInBytes()
                    << ", input shape: " << kernel_input->GetShapeVector();
      return kLiteError;
    }
    auto input_device_address = input->device_data();
    if (input_device_address != nullptr) {
      kernel_args_.inputs[i]->SetData(std::make_shared<kernel::Address>(input_device_address, input->Size()));
      kernel_args_.inputs[i]->SetHostData(nullptr);
    } else {
      kernel_args_.inputs[i]->SetHostData(std::make_shared<kernel::Address>(input->data(), input->Size()));
      kernel_args_.inputs[i]->SetData(nullptr);
    }
  }

  if (outputs->size() != kernel_args_.outputs.size()) {
    MS_LOG(ERROR) << "Given outputs size " << outputs->size() << " != graph inputs size "
                  << kernel_args_.outputs.size();
    return kLiteError;
  }
  for (size_t i = 0; i < outputs->size(); i++) {
    auto &output = (*outputs)[i];
    auto &kernel_output = kernel_args_.outputs[i];
    if (!dyn_outshape_ && output->Size() != kernel_output->GetSizeInBytes()) {
      MS_LOG(ERROR) << "Byte size of output " << i << " != the size expected, given size " << output->Size()
                    << ", expected size " << kernel_output->GetSizeInBytes()
                    << ", output shape: " << kernel_output->GetShapeVector();
      return kLiteError;
    }
    auto output_device_address = output->device_data();
    if (output_device_address != nullptr) {
      kernel_args_.outputs[i]->SetData(std::make_shared<kernel::Address>(output_device_address, output->Size()));
      kernel_args_.outputs[i]->SetHostData(nullptr);
    } else {
      kernel_args_.outputs[i]->SetHostData(std::make_shared<kernel::Address>(output->MutableData(), output->Size()));
      kernel_args_.outputs[i]->SetData(nullptr);
    }
  }
  return kSuccess;
}

Status SingleOpInferSession::RunGraph(uint32_t graph_id, const std::vector<lite::Tensor *> &inputs,
                                      std::vector<lite::Tensor *> *outputs) {
  if (outputs == nullptr) {
    MS_LOG(ERROR) << "outputs cannot be nullptr";
    return kLiteError;
  }
  if (kernel_mod_ == nullptr) {
    MS_LOG(ERROR) << "Model has not been built";
    return kLiteError;
  }
  MS_LOG(DEBUG) << "SingleOpInferSession::RunGraph with input and outputs";
  std::vector<ShapeVector> new_shapes(inputs.size());
  for (size_t i = 0; i < inputs.size(); i++) {
    auto shape = inputs[i]->shape();
    new_shapes[i].resize(shape.size());
    std::transform(shape.begin(), shape.end(), new_shapes[i].begin(), [](int c) { return static_cast<int64_t>(c); });
  }
  auto ret = OnNewInputShapes(new_shapes);
  if (ret != kSuccess) {
    return ret;
  }
  ret = InitInputOutputData(inputs, outputs);
  if (ret != kSuccess) {
    return ret;
  }
  try {
    std::vector<kernel::AddressPtr> ignore_datas;
    if (!kernel_mod_->Launch(ignore_datas, ignore_datas, ignore_datas, nullptr)) {
      MS_LOG(ERROR) << "Failed to launch kernel";
      return kLiteError;
    }
  } catch (std::exception &e) {
    MS_LOG(ERROR) << "Failed to launch kernel, exception: " << e.what();
    return kLiteError;
  }
  return SetBackOutputIfDynamic(outputs);
}

Status SingleOpInferSession::OnNewInputShapes(const std::vector<ShapeVector> &new_shapes) {
  if (kernel_mod_ == nullptr) {
    MS_LOG(ERROR) << "Model has not been built";
    return kLiteError;
  }
  if (inputs_.size() != new_shapes.size()) {
    MS_LOG(ERROR) << "Graph inputs size " << inputs_.size() << " != resize input size " << new_shapes.size();
    return kLiteError;
  }
  auto input_changed = false;
  for (size_t i = 0; i < inputs_.size(); i++) {
    auto new_shape = new_shapes[i];
    if (std::any_of(new_shape.begin(), new_shape.end(), [](auto dim) { return dim < 0; })) {
      MS_LOG(ERROR) << "New shape of input " << i << " cannot be dynamic, new shape: " << new_shape;
      return kLiteError;
    }
    if (inputs_[i]->Shape() != new_shapes[i]) {
      input_changed = true;
      kernel_args_.inputs[i]->SetShapeVector(new_shapes[i]);
    }
  }
  if (!input_changed) {
    return kSuccess;
  }
  MS_LOG(INFO) << "SingleOpInferSession::Resize";

  if (kernel_mod_->Resize(kernel_args_.inputs, kernel_args_.outputs) != kSuccess) {
    MS_LOG(ERROR) << "Failed to resize custom ascend kernel";
    return kLiteError;
  }
  // shapes of inputs and outputs should be updated in CustomAscendKernelMod::Resize
  for (size_t i = 0; i < inputs_.size(); i++) {
    inputs_[i]->SetShape(kernel_args_.inputs[i]->GetShapeVector());
  }
  for (size_t i = 0; i < outputs_.size(); i++) {
    outputs_[i]->SetShape(kernel_args_.outputs[i]->GetShapeVector());
  }
  return kSuccess;
}

Status SingleOpInferSession::Resize(uint32_t, const std::vector<lite::Tensor *> &,
                                    const std::vector<std::vector<int64_t>> &dims) {
  return OnNewInputShapes(dims);
}

std::vector<MutableTensorImplPtr> SingleOpInferSession::GetOutputs(uint32_t) { return outputs_; }
std::vector<MutableTensorImplPtr> SingleOpInferSession::GetInputs(uint32_t) { return inputs_; }
std::vector<std::string> SingleOpInferSession::GetOutputNames(uint32_t) { return output_names_; }
std::vector<std::string> SingleOpInferSession::GetInputNames(uint32_t) { return input_names_; }

MutableTensorImplPtr SingleOpInferSession::GetOutputByTensorName(uint32_t, const std::string &tensor_name) {
  for (size_t idx = 0; idx < output_names_.size(); ++idx) {
    if (output_names_[idx] == tensor_name) {
      if (idx < outputs_.size()) {
        return outputs_[idx];
      }
    }
  }
  MS_LOG(ERROR) << "Can't found tensor name " << tensor_name;
  return nullptr;
}

MutableTensorImplPtr SingleOpInferSession::GetInputByTensorName(uint32_t, const std::string &tensor_name) {
  for (size_t idx = 0; idx < input_names_.size(); ++idx) {
    if (input_names_[idx] == tensor_name) {
      if (idx < inputs_.size()) {
        return inputs_[idx];
      }
    }
  }
  MS_LOG(ERROR) << "Can't found tensor name " << tensor_name;
  return nullptr;
}

static std::shared_ptr<InferSession> SingleOpSessionCreator(const std::shared_ptr<Context> &ctx,
                                                            const ConfigInfos &config_infos) {
  auto session = std::make_shared<SingleOpInferSession>();
  auto ret = session->Init(ctx, config_infos);
  if (ret != kSuccess) {
    MS_LOG(ERROR) << "Init session failed.";
    return nullptr;
  }
  return session;
}
REG_SESSION(kSingleOpSession, SingleOpSessionCreator);
}  // namespace mindspore
