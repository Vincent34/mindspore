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

#include "backend/common/optimizer/dynamic_shape/dynamic_shape_helper.h"

#include <memory>
#include <stack>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include "mindspore/core/ops/framework_ops.h"
#include "include/backend/anf_runtime_algorithm.h"
#include "runtime/device/ms_device_shape_transfer.h"
#include "include/common/utils/anfalgo.h"
#include "include/common/utils/utils.h"
#include "utils/anf_utils.h"
#include "kernel/framework_utils.h"
#include "utils/ms_context.h"
#include "abstract/ops/primitive_infer_map.h"
#include "mindspore/ccsrc/plugin/device/cpu/kernel/pyexecute/py_execute_cpu_kernel.h"
#include "include/common/profiler.h"

namespace mindspore {
namespace opt::dynamic_shape {
InfPyHandler cpp_infer_py_handler_{nullptr};
void set_cpp_infer_py_handler(const InfPyHandler &infer_handler) { cpp_infer_py_handler_ = infer_handler; }
namespace {
constexpr int64_t kInvalidShape = -2;

void InferShapeForNopNode(const AnfNodePtr &input_node) {
  MS_EXCEPTION_IF_NULL(input_node);
  if (!common::AnfAlgo::IsNopNode(input_node)) {
    MS_LOG(INFO) << "Input node is not a nop node, no need infer.";
    return;
  }
  if (!common::AnfAlgo::IsNeedSkipNopOpExecution(input_node)) {
    MS_LOG(INFO) << "The Nop node need execution, no need the InferShapeForNopNode.";
    return;
  }
  MS_LOG(INFO) << "Infer shape for nop node.";
  std::stack<AnfNodePtr> nop_road;
  nop_road.push(input_node);

  auto in_node = input_node;
  while (true) {
    auto input_node_with_idx = common::AnfAlgo::GetPrevNodeOutput(in_node, 0);
    in_node = input_node_with_idx.first;
    MS_EXCEPTION_IF_NULL(in_node);
    if (common::AnfAlgo::IsNopNode(in_node)) {
      nop_road.push(in_node);
    } else {
      break;
    }
  }

  while (!nop_road.empty()) {
    auto nop_node = nop_road.top();
    MS_EXCEPTION_IF_NULL(nop_node);
    AnfAlgo::InferShape(nop_node->cast<CNodePtr>());
    nop_road.pop();
  }
}

TypeId GetSequenceType(const abstract::AbstractSequencePtr &seq_abs) {
  auto elems = seq_abs->elements();
  if (!elems[0]->isa<abstract::AbstractScalar>()) {
    MS_LOG(EXCEPTION) << "The 0'th element of sequence must be a scalar, but got:" << elems[0]->ToString();
  }

  auto fixed_type = elems[0]->BuildType()->type_id();
  for (size_t i = 1; i < elems.size(); i++) {
    if (!elems[i]->isa<abstract::AbstractScalar>()) {
      MS_LOG(EXCEPTION) << "The " << i << "'th element of sequence must be a scalar, but got:" << elems[i]->ToString();
    }
    auto follow_type = elems[i]->BuildType()->type_id();
    if (fixed_type != follow_type) {
      MS_LOG(EXCEPTION) << "Different type found between 0'th element[Type: " << fixed_type << "] and " << i
                        << "'th element[Type: " << follow_type << "]";
    }
  }
  return fixed_type;
}

tensor::TensorPtr CreateTensorMem(const std::pair<AnfNodePtr, size_t> &input_node_with_index) {
  auto real_input = input_node_with_index.first;
  MS_EXCEPTION_IF_NULL(real_input);
  auto real_input_index = input_node_with_index.second;
  auto abs = real_input->abstract();
  MS_EXCEPTION_IF_NULL(abs);

  ShapeVector shape;
  TypeId type;
  if (abs->isa<abstract::AbstractScalar>()) {
    shape = {1};
    type = abs->BuildType()->type_id();
  } else if (AnfAlgo::IsRealSquenceOutput(real_input)) {
    auto seq_abs = abs->cast<abstract::AbstractSequencePtr>();
    MS_EXCEPTION_IF_NULL(seq_abs);
    auto elem_num = seq_abs->size();
    if (elem_num == 0) {
      MS_LOG(DEBUG) << "Empty sequence for node:" << real_input->fullname_with_scope();
      return nullptr;
    }
    type = GetSequenceType(seq_abs);
    shape = {SizeToLong(elem_num)};
  } else if (abs->isa<abstract::AbstractTensor>() || abs->isa<abstract::AbstractSequence>()) {
    shape = trans::GetRuntimePaddingShape(real_input, real_input_index);
    if (real_input->isa<ValueNode>()) {
      // the type of ValueNode in KernelInfo is kTypeUnknown
      type = common::AnfAlgo::GetOutputInferDataType(real_input, real_input_index);
    } else {
      type = AnfAlgo::GetOutputDeviceDataType(real_input, real_input_index);
      if (type == TypeId::kTypeUnknown) {
        type = common::AnfAlgo::GetOutputInferDataType(real_input, real_input_index);
      }
    }
  } else {
    MS_LOG(EXCEPTION) << "For node:" << real_input->fullname_with_scope() << ", abstract(" << abs->ToString()
                      << ") is invalid.";
  }

  return std::make_shared<tensor::Tensor>(type, shape);
}

tensor::TensorPtr GetDependValueTensor(const AnfNodePtr &node, size_t i,
                                       const std::pair<AnfNodePtr, size_t> &input_node_with_index, bool skip_nop_node,
                                       void *args) {
  MS_EXCEPTION_IF_NULL(node);
  MS_EXCEPTION_IF_NULL(input_node_with_index.first);
  auto depended_value = CreateTensorMem(input_node_with_index);
  if (IsPrimitiveCNode(input_node_with_index.first, prim::kPrimPyExecute)) {
    MS_LOG(DEBUG) << "The input node is " << input_node_with_index.first->ToString()
                  << ", use user data instead of address.";
    return depended_value;
  }
  // First use the data of args.
  if (args != nullptr) {
    auto input_device_address = reinterpret_cast<std::vector<device::DeviceAddress *> *>(args);
    if (i < input_device_address->size() && input_device_address->at(i) != nullptr) {
      uint64_t start_time = 0;
      PROFILER_START(start_time);
      depended_value->data_sync_directly(input_device_address->at(i));
      PROFILER_END(start_time, runtime::ProfilerModule::kKernel, runtime::ProfilerEvent::kKernelInferDataSync,
                   node->fullname_with_scope(), true);
      return depended_value;
    }
    MS_LOG(WARNING) << "There is no valid data for " << i << " input of " << node->DebugString() << ", "
                    << node->fullname_with_scope();
  }

  // Second use the device address of node as fault-tolerant.
  auto output_addr =
    AnfAlgo::GetMutableOutputAddr(input_node_with_index.first, input_node_with_index.second, skip_nop_node);
  if (output_addr != nullptr && output_addr->IsPtrValid()) {
    // The second parameter must be false, otherwise the device address cannot be released and allocated, and the
    // address size will be wrong in the dynamic shape scenario.
    depended_value->set_device_address(output_addr, false);
    uint64_t start_time = 0;
    PROFILER_START(start_time);
    depended_value->data_sync();
    PROFILER_END(start_time, runtime::ProfilerModule::kKernel, runtime::ProfilerEvent::kKernelInferDataSync,
                 node->fullname_with_scope(), true);
    return depended_value;
  }

  MS_LOG(EXCEPTION) << "There is no valid data for " << i << " input of " << node->DebugString() << ", "
                    << node->fullname_with_scope();
}

tensor::TensorPtr GetDependValueTensor(const std::vector<device::DeviceAddressPtr> &device_address_list,
                                       const std::vector<tensor::TensorPtr> &input_tensors, size_t index) {
  if (index >= input_tensors.size()) {
    MS_LOG(EXCEPTION) << "Input index: " << index << "is large than the input tensor's size " << input_tensors.size();
  }

  if (input_tensors[index] != nullptr) {
    return input_tensors[index];
  }

  if (index >= device_address_list.size()) {
    MS_LOG(EXCEPTION) << "Input index: " << index << "is large than the input device addresses's size "
                      << device_address_list.size();
  }

  auto output_addr = device_address_list[index];
  if (output_addr != nullptr && output_addr->IsPtrValid()) {
    auto type = output_addr->type_id();
    auto shape = output_addr->host_shape();
    auto tensor = std::make_shared<tensor::Tensor>(type, shape);
    tensor->set_device_address(output_addr, false);
    uint64_t start_time = 0;
    PROFILER_START(start_time);
    tensor->data_sync();
    PROFILER_END(start_time, runtime::ProfilerModule::kKernel, runtime::ProfilerEvent::kKernelInferDataSync,
                 runtime::kDefaultOpName, true);
    return tensor;
  }

  MS_LOG(EXCEPTION) << "There is no valid data for depend value";
}

abstract::AbstractBasePtr MakeNewAbstractByScalar(const tensor::TensorPtr &depended_value) {
  abstract::AbstractBasePtr new_abs;
  auto type = depended_value->Dtype()->type_id();
  if (type == kNumberTypeInt32) {
    auto tensor_data = reinterpret_cast<int32_t *>(depended_value->data_c());
    MS_EXCEPTION_IF_NULL(tensor_data);
    new_abs = std::make_shared<abstract::AbstractScalar>(*tensor_data);
  } else if (type == kNumberTypeInt64) {
    auto tensor_data = reinterpret_cast<int64_t *>(depended_value->data_c());
    MS_EXCEPTION_IF_NULL(tensor_data);
    new_abs = std::make_shared<abstract::AbstractScalar>(*tensor_data);
  } else if (type == kNumberTypeFloat32) {
    auto tensor_data = reinterpret_cast<float *>(depended_value->data_c());
    MS_EXCEPTION_IF_NULL(tensor_data);
    new_abs = std::make_shared<abstract::AbstractScalar>(*tensor_data);
  } else if (type == kNumberTypeFloat64) {
    auto tensor_data = reinterpret_cast<double *>(depended_value->data_c());
    MS_EXCEPTION_IF_NULL(tensor_data);
    new_abs = std::make_shared<abstract::AbstractScalar>(*tensor_data);
  } else {
    MS_LOG(EXCEPTION) << "Unsupported type: " << type;
  }
  return new_abs;
}

template <typename T>
abstract::AbstractBasePtrList MakeElemsByTensorValue(void *data, size_t size) {
  MS_EXCEPTION_IF_NULL(data);
  T *tensor_data = static_cast<T *>(data);
  AbstractBasePtrList elems;
  for (size_t i = 0; i < size; i++) {
    auto scalar = std::make_shared<abstract::AbstractScalar>(tensor_data[i]);
    (void)elems.emplace_back(scalar);
  }
  return elems;
}

abstract::AbstractBasePtr MakeNewAbstractBySequence(const tensor::TensorPtr &depended_value,
                                                    const abstract::AbstractBasePtr &input_abs) {
  abstract::AbstractBasePtr new_abs;
  auto type = depended_value->Dtype()->type_id();
  AbstractBasePtrList elems;
  switch (type) {
    case kNumberTypeInt32: {
      elems = MakeElemsByTensorValue<int32_t>(depended_value->data_c(), depended_value->DataSize());
      break;
    }
    case kNumberTypeInt64: {
      elems = MakeElemsByTensorValue<int64_t>(depended_value->data_c(), depended_value->DataSize());
      break;
    }
    case kNumberTypeFloat32: {
      elems = MakeElemsByTensorValue<float>(depended_value->data_c(), depended_value->DataSize());
      break;
    }
    case kNumberTypeFloat64: {
      elems = MakeElemsByTensorValue<double>(depended_value->data_c(), depended_value->DataSize());
      break;
    }
    default: {
      MS_LOG(EXCEPTION) << "Unsupported type: " << type;
    }
  }
  if (input_abs->isa<abstract::AbstractTuple>()) {
    new_abs = std::make_shared<abstract::AbstractTuple>(elems);
  } else if (input_abs->isa<abstract::AbstractList>()) {
    new_abs = std::make_shared<abstract::AbstractList>(elems);
  } else {
    MS_LOG(EXCEPTION) << "Unsupported abstract type:" << input_abs->ToString();
  }
  new_abs->set_value(depended_value);
  return new_abs;
}

abstract::AbstractBasePtr MakeNewAbstract(const AnfNodePtr &input, const tensor::TensorPtr &depended_value,
                                          const size_t &input_index) {
  auto abs = input->abstract();
  abstract::AbstractBasePtr new_abs;
  if (abs->isa<abstract::AbstractTensor>()) {
    new_abs = abs->Clone();
    new_abs->set_value(depended_value);
  } else if (abs->isa<abstract::AbstractScalar>()) {
    new_abs = MakeNewAbstractByScalar(depended_value);
  } else if (AnfAlgo::IsRealSquenceOutput(input)) {
    new_abs = MakeNewAbstractBySequence(depended_value, abs);
  } else if (abs->isa<abstract::AbstractSequence>()) {
    auto abstract_seq = abs->cast<abstract::AbstractSequencePtr>();
    MS_EXCEPTION_IF_NULL(abstract_seq);
    MS_EXCEPTION_IF_CHECK_FAIL((input_index < abstract_seq->elements().size()), "Index is out of range.");
    new_abs = abstract_seq->elements()[input_index]->Clone();
    new_abs->set_value(depended_value);
  } else {
    MS_LOG(EXCEPTION) << "Unsupported abstract type:" << abs->ToString();
  }
  // Set user data for PyExecute infer.
  if (input->has_user_data<kernel::PyExecuteOutputUserData>()) {
    const auto &output_data = input->user_data<kernel::PyExecuteOutputUserData>();
    new_abs->set_user_data<kernel::PyExecuteOutputUserData>(output_data);
  }

  return new_abs;
}

void InferShapeForGraph(const CNodePtr &cnode, const FuncGraphPtr &func_graph,
                        const AbstractBasePtrList &args_spec_list) {
  std::map<AnfNodePtr, AbstractBasePtr> node_abs_spec_map;
  if (args_spec_list.size() != func_graph->parameters().size()) {
    MS_LOG(EXCEPTION)
      << "The args_spec_list size should be the same as that of func_graph parameters, but get args_spec_list: "
      << args_spec_list.size() << " vs func_graph parameters: " << func_graph->parameters().size();
  }
  for (size_t i = 0; i < args_spec_list.size(); i++) {
    (void)node_abs_spec_map.emplace(func_graph->parameters()[i], args_spec_list[i]);
  }
  std::vector<AnfNodePtr> nodes = TopoSort(func_graph->get_return());
  for (auto &node : nodes) {
    if (!node->isa<CNode>() || !IsValueNode<Primitive>(node->cast<CNodePtr>()->input(0))) {
      continue;
    }
    if (!IsPrimitiveCNode(node, prim::kPrimReturn)) {
      auto cnode_primitive = GetCNodePrimitive(node);
      MS_EXCEPTION_IF_NULL(cnode_primitive);
      auto prim_cnode = node->cast<CNodePtr>();

      AbstractBasePtrList cnode_args_spec_list;

      for (size_t i = 1; i < prim_cnode->size(); i++) {
        auto input_node = prim_cnode->input(i);
        auto para_spec = node_abs_spec_map.find(input_node);
        if (para_spec != node_abs_spec_map.end()) {
          (void)cnode_args_spec_list.emplace_back(para_spec->second);
        } else {
          (void)cnode_args_spec_list.emplace_back(input_node->abstract());
        }
      }
      opt::CppInferShape(cnode_primitive, cnode_args_spec_list, cnode);
      (void)node_abs_spec_map.emplace(node, cnode->abstract());
    } else {
      auto return_cnode = node->cast<CNodePtr>();
      auto return_spec = node_abs_spec_map.find(return_cnode->input(1));
      if (return_spec == node_abs_spec_map.end()) {
        MS_LOG(EXCEPTION) << "There is no inferred result for the return value of the node: "
                          << return_cnode->DebugString();
      }
      cnode->set_abstract(return_spec->second);
    }
  }
  return;
}

void InferShapeForPrimitive(const CNodePtr &cnode, const PrimitivePtr &primitive,
                            const AbstractBasePtrList &args_spec_list, bool has_py_execute_data) {
  if (!has_py_execute_data && !IsPrimitiveCNode(cnode, prim::kPrimPyExecute)) {
    // Pynative mode is rely on the origin abstract of cnode, so cannot modify the abstract inplace, clone from old
    // abstract instead.
    opt::CppInferShape(primitive, args_spec_list, cnode);
  } else {
    if (cpp_infer_py_handler_ == nullptr) {
      // If run without Python.
      MS_LOG(WARNING) << "\'cpp_infer_py_handler_\' should not be null.";
      const auto &abs = opt::CppInferShapeAndType(primitive, args_spec_list);
      MS_LOG(DEBUG) << "The abstract of " << cnode->fullname_with_scope() << " changes from " << cnode->abstract()
                    << " to " << abs;
      cnode->set_abstract(abs);
      return;
    }
    const auto &abs = cpp_infer_py_handler_(cnode, primitive, args_spec_list);
    cnode->set_abstract(abs);
  }
}

void InferShape(const CNodePtr &cnode, std::map<uint32_t, tensor::TensorPtr> *depend_tensor_map, void *args) {
  MS_EXCEPTION_IF_NULL(cnode);
  MS_EXCEPTION_IF_NULL(depend_tensor_map);
  MS_LOG(DEBUG) << "InferShape start, node:" << cnode->fullname_with_scope();
  std::set<int64_t> depend_list = abstract::GetValueDependArgIndices(cnode);

  depend_tensor_map->clear();
  auto &inputs = cnode->inputs();
  if (inputs.empty()) {
    MS_LOG(EXCEPTION) << "Invalid inputs.";
  }
  auto context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context);
  AbstractBasePtrList args_spec_list;
  auto input_size = common::AnfAlgo::GetInputTensorNum(cnode);
  bool skip_nop_node = !context->get_param<bool>(MS_CTX_ENABLE_MINDRT);
  bool has_py_execute_data = false;
  kernel::PyExecuteOutputUserDataPtr list_user_data = nullptr;
  std::vector<size_t> list_start_index;
  for (size_t i = 0; i < input_size; i++) {
    auto input_node_with_index = common::AnfAlgo::GetPrevNodeOutput(cnode, i, false);
    auto real_input = input_node_with_index.first;
    auto real_input_index = input_node_with_index.second;

    MS_EXCEPTION_IF_NULL(real_input);
    if (skip_nop_node) {
      InferShapeForNopNode(real_input);
    }

    if (depend_list.find(i) != depend_list.end()) {
      auto depended_value = GetDependValueTensor(cnode, i, input_node_with_index, skip_nop_node, args);
      auto ret2 = depend_tensor_map->try_emplace(i, depended_value);
      if (!ret2.second) {
        MS_LOG(EXCEPTION) << "Insert map failed.";
      }

      auto updated_abs = MakeNewAbstract(real_input, depended_value, real_input_index);
      if (updated_abs->has_user_data<kernel::PyExecuteOutputUserData>()) {
        has_py_execute_data = true;
        if (IsPrimitiveCNode(real_input, prim::kPrimPyExecute) &&
            real_input->abstract()->isa<abstract::AbstractSequence>()) {
          auto updated_abs_user_data = updated_abs->user_data<kernel::PyExecuteOutputUserData>();
          if (list_user_data == nullptr || list_user_data != updated_abs_user_data) {
            list_start_index.push_back(i);
            list_user_data = updated_abs_user_data;
          }
        }
      }
      (void)args_spec_list.emplace_back(updated_abs);
    } else {
      auto abs = real_input->abstract();
      MS_EXCEPTION_IF_NULL(abs);
      MS_LOG(DEBUG) << "Real input node:" << real_input->DebugString() << " abs:" << abs->ToString()
                    << " index:" << real_input_index;
      if (abs->isa<abstract::AbstractSequence>() && !AnfAlgo::IsRealSquenceOutput(real_input)) {
        auto abs_seq = abs->cast<abstract::AbstractSequencePtr>();
        MS_EXCEPTION_IF_NULL(abs_seq);
        MS_EXCEPTION_IF_CHECK_FAIL((real_input_index < abs_seq->elements().size()), "Index is out of range.");
        auto abs_index = abs_seq->elements()[real_input_index];
        (void)args_spec_list.emplace_back(abs_index);
      } else {
        (void)args_spec_list.emplace_back(abs);
      }
    }
  }

  if (auto primitive = GetValueNode<PrimitivePtr>(inputs[0])) {
    (void)primitive->AddAttr(kAttrListStartIndex, MakeValue(list_start_index));
    InferShapeForPrimitive(cnode, primitive, args_spec_list, has_py_execute_data);
  } else if (auto func_graph = GetValueNode<FuncGraphPtr>(inputs[0])) {
    InferShapeForGraph(cnode, func_graph, args_spec_list);
  } else {
    MS_LOG(EXCEPTION) << "The first input of the cnode should be either a primitive or a function graph, but get: "
                      << inputs[0]->fullname_with_scope();
  }
}

inline bool IsCpuKernelMod(kernel::KernelModType kernel_mod_type) {
  return kernel_mod_type == kernel::KernelModType::NativeCpuKernelMod ||
         kernel_mod_type == kernel::KernelModType::DeprecatedNativeCpuKernelMod;
}
}  // namespace
bool IsRealCNode(const BaseRef &n) {
  if (utils::isa<CNodePtr>(n)) {
    CNodePtr cnode = utils::cast<CNodePtr>(n);
    return AnfUtils::IsRealKernel(cnode);
  }
  return false;
}

AnfNodePtr GenInferNode(const AnfNodePtr &node) {
  MS_EXCEPTION_IF_NULL(node);
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  auto infer_node = AnfUtils::NewInferActorNode([cnode](void *args) { InferOp(cnode, args); }, cnode);
  infer_node->set_kernel_info(std::make_shared<device::KernelInfo>());
  return infer_node;
}

AnfNodePtr GenInitNode(const AnfNodePtr &node) {
  MS_EXCEPTION_IF_NULL(node);
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);

  auto kernel_mod = AnfAlgo::GetKernelMod(cnode);
  MS_EXCEPTION_IF_NULL(kernel_mod);
  AnfUtils::CustomActorCallback actor_func = [kernel_mod, cnode](void *) {
    auto args = cnode->user_data<kernel::KernelArgs>();
    if (args == nullptr) {
      args = std::make_shared<kernel::KernelArgs>();
    }
    MS_LOG(DEBUG) << "resize for cnode:" << cnode->fullname_with_scope();
    if (kernel_mod->Resize(args->inputs, args->outputs, args->depend_tensor_map) ==
        static_cast<int>(kernel::KRET_RESIZE_FAILED)) {
      MS_LOG(EXCEPTION) << "Node " << cnode->fullname_with_scope() << " Resize failed.";
    }
  };

  auto init_node = AnfUtils::NewInitActorNode(actor_func, cnode);
  init_node->set_kernel_info(std::make_shared<device::KernelInfo>());
  return init_node;
}

void InferOp(const CNodePtr &cnode, void *args) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto kernel_mod = AnfAlgo::GetKernelMod(cnode);
  MS_EXCEPTION_IF_NULL(kernel_mod);

  kernel::KernelArgs kernel_args;
  MS_LOG(DEBUG) << "infer shape for node:" << cnode->fullname_with_scope();
  InferShape(cnode, &kernel_args.depend_tensor_map, args);
  auto kernel_mod_type = kernel_mod->GetKernelModType();
  auto update = kernel::AbstractArgsFromCNode(cnode);
  update.depend_tensor_map = std::move(kernel_args.depend_tensor_map);
  kernel::SetInputsByDependMap(update.depend_tensor_map, &update.inputs, IsCpuKernelMod(kernel_mod_type));
  kernel::SetArgsToCNode(cnode, update);
}

void InferShape(std::map<uint32_t, tensor::TensorPtr> *depend_tensor_map,
                const pynative::ExecuteKernelInfo &execute_kernel,
                const std::vector<tensor::TensorPtr> &input_tensors) {
  MS_EXCEPTION_IF_NULL(execute_kernel.kernel_);
  MS_EXCEPTION_IF_NULL(depend_tensor_map);
  MS_LOG(DEBUG) << "InferShape start, node:" << execute_kernel.kernel_->fullname_with_scope();
  std::set<int64_t> depend_list = abstract::GetValueDependArgIndices(execute_kernel.kernel_);

  depend_tensor_map->clear();
  auto context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context);
  AbstractBasePtrList args_spec_list;
  auto primitive = execute_kernel.primitive_;
  auto input_size = execute_kernel.inputs_device_address_.size();
  for (size_t i = 0; i < input_size; i++) {
    auto input_address = execute_kernel.inputs_device_address_[i];
    if (depend_list.find(i) != depend_list.end()) {
      auto depended_value = GetDependValueTensor(execute_kernel.inputs_device_address_, input_tensors, i);
      auto ret2 = depend_tensor_map->try_emplace(i, depended_value);
      if (!ret2.second) {
        MS_LOG(EXCEPTION) << "Insert map failed.";
      }
      (void)args_spec_list.emplace_back(depended_value->ToAbstract());
    } else {
      auto abs =
        std::make_shared<abstract::AbstractTensor>(TypeIdToType(input_address->type_id()), input_address->host_shape());
      (void)args_spec_list.emplace_back(abs);
    }
  }

  CppInferShape(primitive, args_spec_list, execute_kernel.kernel_);
}

void UpdateOutputDeviceShape(const std::vector<device::DeviceAddressPtr> &output_device_address_list,
                             const AbstractBasePtr &abstract) {
  auto output_num = output_device_address_list.size();
  if (abstract->isa<abstract::AbstractTuple>()) {
    auto abstract_tuple = abstract->cast<abstract::AbstractTuplePtr>();
    for (size_t i = 0; i < output_num; ++i) {
      auto real_abs = abstract_tuple->elements()[i];
      output_device_address_list[i]->set_host_shape(BaseShapeToShape(real_abs->BuildShape()));
    }
  } else {
    output_device_address_list[0]->set_host_shape(BaseShapeToShape(abstract->BuildShape()));
  }
}

kernel::KernelArgs GetKernelArgsForNode(const CNodePtr &cnode, const pynative::ExecuteKernelInfo &execute_kernel,
                                        const kernel::KernelArgs &kernel_args) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto kernel_mod = AnfAlgo::GetKernelMod(cnode);
  MS_EXCEPTION_IF_NULL(kernel_mod);

  UpdateOutputDeviceShape(execute_kernel.outputs_device_address_, cnode->abstract());

  auto kernel_mod_type = kernel_mod->GetKernelModType();
  auto update = kernel::AbstractArgsFromDeviceAddress(kernel_mod, execute_kernel.inputs_device_address_,
                                                      execute_kernel.outputs_device_address_, cnode->abstract());
  update.depend_tensor_map = kernel_args.depend_tensor_map;
  kernel::SetInputsByDependMap(update.depend_tensor_map, &update.inputs, IsCpuKernelMod(kernel_mod_type));
  return update;
}

kernel::KernelArgs InferOp(const CNodePtr &cnode, const pynative::ExecuteKernelInfo &execute_kernel,
                           const std::vector<tensor::TensorPtr> &input_tensors) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto kernel_mod = AnfAlgo::GetKernelMod(cnode);
  MS_EXCEPTION_IF_NULL(kernel_mod);

  kernel::KernelArgs kernel_args;
  InferShape(&kernel_args.depend_tensor_map, execute_kernel, input_tensors);

  return GetKernelArgsForNode(cnode, execute_kernel, kernel_args);
}

kernel::KernelArgs SetOpArgs(const CNodePtr &cnode, const pynative::ExecuteKernelInfo &execute_kernel,
                             const std::vector<tensor::TensorPtr> &input_tensors) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto kernel_mod = AnfAlgo::GetKernelMod(cnode);
  MS_EXCEPTION_IF_NULL(kernel_mod);
  kernel::KernelArgs kernel_args;
  std::set<int64_t> depend_list = abstract::GetValueDependArgIndices(cnode);
  auto input_size = execute_kernel.inputs_device_address_.size();
  for (size_t i = 0; i < input_size; i++) {
    if (depend_list.find(i) != depend_list.end()) {
      auto depended_value = GetDependValueTensor(execute_kernel.inputs_device_address_, input_tensors, i);
      auto ret2 = kernel_args.depend_tensor_map.try_emplace(i, depended_value);
      if (!ret2.second) {
        MS_LOG(EXCEPTION) << "Insert map failed.";
      }
    }
  }

  return GetKernelArgsForNode(cnode, execute_kernel, kernel_args);
}

CustomActorNodeManager &CustomActorNodeManager::Instance() {
  static CustomActorNodeManager instance{};
  return instance;
}
}  // namespace opt::dynamic_shape
}  // namespace mindspore
