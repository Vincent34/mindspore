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

#ifndef MINDSPORE_CCSRC_PIPELINE_PYNATIVE_FORWARD_FORWARD_H_
#define MINDSPORE_CCSRC_PIPELINE_PYNATIVE_FORWARD_FORWARD_H_

#include <memory>
#include <string>
#include <map>
#include <utility>
#include <stack>
#include <vector>
#include "pipeline/pynative/forward/do_cast.h"
#include "pipeline/pynative/forward/do_infer.h"
#include "backend/graph_compiler/backend.h"
#include "ir/cell.h"
#include "runtime/pynative/async/async_queue.h"

namespace mindspore {
namespace pynative {
class GradExecutor;
using GradExecutorPtr = std::shared_ptr<GradExecutor>;
using GradExecutorWeakPtr = std::weak_ptr<GradExecutor>;

using MindrtBackendMap = std::map<std::string, std::shared_ptr<compile::MindRTBackend>>;

class ForwardExecutor {
 public:
  ForwardExecutor()
      : cast_operation_(std::make_shared<CastOperation>()),
        infer_operation_(std::make_shared<InferOperation>()),
        frontend_queue_(std::make_shared<AsyncQueue>("frontend_queue", kThreadWaitLevel::kLevelFrontend)),
        backend_queue_(std::make_shared<AsyncQueue>("backend_queue", kThreadWaitLevel::kLevelBackend)) {}
  ~ForwardExecutor() = default;

  void Init();
  std::function<void(const FrontendOpRunInfoPtr &)> RunOpS = [this](auto &&PH1) {
    RunOpFrontend(std::forward<decltype(PH1)>(PH1));
  };

  void DispatchBackendTask(const FrontendOpRunInfoPtr &op_run_info,
                           const session::BackendOpRunInfoPtr &backend_op_run_info);
  void DispatchFrontendTask(const FrontendOpRunInfoPtr &op_run_info);
  void RunOpFrontend(const FrontendOpRunInfoPtr &op_run_info);
  // If sub is true, this function will not convert StubTensor to Tensor.
  // Used to reduce the overhead of StubTensor WaitValue.
  FrontendOpRunInfoPtr GenerateOpRunInfo(const py::args &args, bool stub = false);
  void set_grad_executor(const GradExecutorPtr &grad_executor) { grad_executor_ = GradExecutorWeakPtr(grad_executor); }
  void ClearNodeAbsMap() const;
  void SetNodeAbsMapByValue(const FrontendOpRunInfoPtr &op_run_info) const;
  void SetNodeAbsMapById(const std::string &id, const abstract::AbstractBasePtr &abs) const;
  AbstractBasePtr GetNodeAbsById(const std::string &id) const;
  void ClearRes();
  void set_lazy_build(bool lazy_build) { lazy_build_ = lazy_build; }
  bool EnablePipeline(const std::string &op_name) const;
  bool enable_async() const;
  inline const std::string &device_target() const { return device_target_; }
  const MindrtBackendMap &mindrt_backend() const { return mindrt_backends_; }
  inline bool IsFirstCell() const { return forward_cell_stack_.empty(); }
  void PushForwardCell(const py::object &cell) { forward_cell_stack_.push(cell.cast<CellPtr>()); }
  void PopForwardCell() { forward_cell_stack_.pop(); }
  void ExecuteLazyTask() const;
  void Sync();
  void PrintPyObjInfo(const py::object &obj, const std::string &str, bool is_cell) const;
  void ProcessBeforeNewGraph(const py::object &obj);
  void ProcessAfterNewGraph(const py::object &obj) const;
  void ProcessBeforeEndGraph(const py::object &obj, bool is_cell);
  void ProcessAfterEndGraph(const py::object &obj, bool is_cell) const;
  bool CellNotSetMixedPrecision(const FrontendOpRunInfoPtr &op_run_info);
  inline InferOperationPtr infer_operation() const {
    MS_EXCEPTION_IF_NULL(infer_operation_);
    return infer_operation_;
  }
  inline void set_is_jit_compiling(bool is_jit_compiling) { is_jit_compiling_ = is_jit_compiling; }
  bool is_jit_compiling() const { return is_jit_compiling_; }

  void WorkerJoin() {
    frontend_queue_->WorkerJoin();
    backend_queue_->WorkerJoin();
  }
  void ClearForwardTask();
  void WaitForwardTask();
  bool IsVmOp(const std::string &op_name) const;
  std::string GetCurrentCellObjId() const;
  std::string GetCurrentDeviceTarget(const PrimitivePtr &op_prim) const;
  void ReinitAfterFork();

 private:
  void ReInit();
  GradExecutorPtr grad() const;
  compile::MindRTBackendPtr GetMindRtBackend(const string &cur_device_target);
  inline CastOperationPtr cast_operation() const {
    MS_EXCEPTION_IF_NULL(cast_operation_);
    return cast_operation_;
  }
  ValuePtr RunOpInVM(const FrontendOpRunInfoPtr &op_run_info) const;
  ValuePtr RunOpInMs(const FrontendOpRunInfoPtr &op_run_info, const BackendOpRunInfoPtr &backend_op_run_info);
  ValuePtr RunOpInMsInner(const FrontendOpRunInfoPtr &op_run_info, const BackendOpRunInfoPtr &backend_op_run_info);
  ValuePtr RunOpWithBackendPolicy(const FrontendOpRunInfoPtr &op_run_info,
                                  const BackendOpRunInfoPtr &backend_op_run_info);
  void RunOpBackend(const FrontendOpRunInfoPtr &op_run_info, const BackendOpRunInfoPtr &backend_op_run_info);
  void RunOpBackendSync(const FrontendOpRunInfoPtr &op_run_info);

  VectorRef RunOpBackendInner(const FrontendOpRunInfoPtr &op_run_info, const BackendOpRunInfoPtr &backend_op_run_info);
  // Mix precision and Implicit transform
  void SetCastForInputs(const FrontendOpRunInfoPtr &op_run_info) const;
  // Infer output abstract
  void InferOutputAbstract(const FrontendOpRunInfoPtr &op_run_info) const;
  // Check sync condition in heterogeneous
  void CheckIfNeedSyncForHeterogeneous(const std::string &cur_target);
  void PrepareOpInputs(const FrontendOpRunInfoPtr &op_run_info);
  void PrepareOpOutputs(const FrontendOpRunInfoPtr &op_run_info) const;

 private:
  bool init_{false};
  bool lazy_build_{true};
  bool enable_async_{true};
  bool is_jit_compiling_{false};
  uint32_t device_id_{0};
  std::string device_target_;
  std::string last_target_{"Unknown"};
  std::stack<CellPtr> forward_cell_stack_;
  GradExecutorWeakPtr grad_executor_;
  CastOperationPtr cast_operation_;
  InferOperationPtr infer_operation_;
  MindrtBackendMap mindrt_backends_;
  AsyncQueuePtr frontend_queue_;
  AsyncQueuePtr backend_queue_;
};
}  // namespace pynative
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_PIPELINE_PYNATIVE_FORWARD_FORWARD_H_
