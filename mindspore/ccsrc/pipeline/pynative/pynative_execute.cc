/**
 * Copyright 2019-2022 Huawei Technologies Co., Ltd
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

#include "pipeline/pynative/pynative_execute.h"
#include "pipeline/pynative/pynative_utils.h"
#include "pipeline/pynative/predict_out_type_map.h"
#include "pipeline/jit/debug/trace.h"
#include "pybind_api/pybind_patch.h"
#include "pybind_api/gil_scoped_long_running.h"
#include "include/common/utils/config_manager.h"
#include "include/common/pybind_api/api_register.h"
#include "frontend/optimizer/ad/grad.h"
#include "pipeline/jit/pass.h"
#include "runtime/pynative/op_executor.h"
#include "runtime/pynative/op_compiler.h"
#include "include/common/profiler.h"
#include "pipeline/jit/parse/data_converter.h"
#include "ir/cell.h"
#include "abstract/utils.h"
#include "include/common/utils/stub_tensor.h"
#include "include/common/utils/python_utils.h"
#include "frontend/operator/ops_front_infer_function.h"
#include "backend/operator/ops_backend_infer_function.h"
#include "include/common/utils/python_fallback_running.h"

namespace mindspore::pynative {
std::shared_ptr<PyNativeExecutor> PyNativeExecutor::executor_ = nullptr;
ForwardExecutorPtr PyNativeExecutor::forward_executor_ = nullptr;
GradExecutorPtr PyNativeExecutor::grad_executor_ = nullptr;
std::mutex PyNativeExecutor::instance_lock_;

namespace {
enum class AsyncRunOpArgsEnum : size_t { PY_PRIM = 0, PY_INPUTS, PY_ARGS_NUM };
template <typename T, typename... Args>
T PyNativeExecutorTry(const std::function<T(const Args &...)> &method, const Args &... args) {
  const auto &inst = PyNativeExecutor::GetInstance();
  MS_EXCEPTION_IF_NULL(inst);
  MS_EXCEPTION_IF_NULL(method);
  auto already_set_error_handler = [&inst]() {
    // Print function call stack info before release.
    std::ostringstream oss;
    trace::TraceGraphEval();
    trace::GetEvalStackInfo(oss);
    // Call py::print to output function call stack to STDOUT, in case of output the log to file, the user can see
    // these info from screen, no need to open log file to find these info.
    py::print(oss.str());
    MS_LOG(ERROR) << oss.str();
    inst->ClearRes();
  };

  if constexpr (std::is_same_v<T, void>) {
    HandleExceptionRethrow([&method, &args...]() { method(args...); }, already_set_error_handler,
                           [&inst]() { inst->ClearRes(); }, [&inst]() { inst->ClearRes(); });
  } else {
    T res;
    HandleExceptionRethrow([&res, &method, &args...]() { res = method(args...); }, already_set_error_handler,
                           [&inst]() { inst->ClearRes(); }, [&inst]() { inst->ClearRes(); });
    return res;
  }
}

// Tensor may be used before the execution of the asynchronous task.
void SetCallbackForInputTensor(const FrontendOpRunInfoPtr &op_run_info) {
  for (auto &input : op_run_info->op_grad_info->input_value) {
    MS_EXCEPTION_IF_NULL(input);
    if (input->isa<tensor::Tensor>()) {
      auto tensor = input->cast<tensor::TensorPtr>();
      MS_EXCEPTION_IF_NULL(tensor);
      tensor->set_lazy_callback([]() { runtime::OpExecutor::GetInstance().WaitAll(); });
    }
  }
}
}  // namespace

void PyNativeExecutor::StoreAsyncStatus(const FrontendOpRunInfoPtr &op_run_info) const {
  op_run_info->async_status.disable_mix_precision =
    (forward_executor()->IsFirstCell() || forward_executor()->CellNotSetMixedPrecision(op_run_info));
  op_run_info->async_status.is_jit_compiling = forward_executor()->is_jit_compiling();
  op_run_info->async_status.custom_bprop_cell_count = grad_executor()->custom_bprop_cell_count();
}

py::object PyNativeExecutor::RunOpStub(const py::args &args) const {
  runtime::ProfilerStageRecorder recorder(runtime::ProfilerStage::kRunOp);
  FrontendOpRunInfoPtr op_run_info = forward_executor()->GenerateOpRunInfo(args, true);
  SetCallbackForInputTensor(op_run_info);

  StoreAsyncStatus(op_run_info);
  const auto &op_name = op_run_info->base_op_run_info.op_name;
  // 1. get top_type from Primitive::PredictOutputType
  auto top_type = PredictOutTypeByName(op_name);
  // 2. if disable PyTraceAsync, return after infer(half-asynchronous) or run(synchronous mode)
  if (!forward_executor()->EnablePipeline(op_name)) {
    // Wait for async task finish
    forward_executor()->WaitForwardTask();
    PyNativeAlgo::Common::StubNodeToValue(op_run_info);
    // RunOp sync
    PyNativeExecutorTry(forward_executor()->RunOpS, op_run_info);
    return PyNativeAlgo::DataConvert::ValueToPyObj(op_run_info->real_out);
  }
  // 3. create top stub node
  auto node = stub::MakeTopNode(top_type);
  // The task in the AsyncQueue may need to acquire gil.
  GilReleaseWithCheck release_gil;
  // 4. set abstract and value in asynchronous thread after infer and run
  op_run_info->stub_output = node.second;
  forward_executor()->DispatchFrontendTask(op_run_info);
  // 5. return stub node
  return node.first;
}

py::object PyNativeExecutor::RealRunOp(const py::args &args) const {
  FrontendOpRunInfoPtr op_run_info = forward_executor()->GenerateOpRunInfo(args);
  StoreAsyncStatus(op_run_info);
  PyNativeExecutorTry(forward_executor()->RunOpS, op_run_info);
  if (PyGILState_Check() == 0) {
    py::gil_scoped_acquire acquire;
    return PyNativeAlgo::DataConvert::ValueToPyObj(op_run_info->real_out);
  } else {
    return PyNativeAlgo::DataConvert::ValueToPyObj(op_run_info->real_out);
  }
}

py::object PyNativeExecutor::CallConstantFolding(const py::args &args) const {
  return forward_executor()->infer_operation()->CallConstantFolding(args);
}

void PyNativeExecutor::set_py_exe_path(const py::object &py_exe_path) const {
  if (!py::isinstance<py::str>(py_exe_path)) {
    MS_LOG(EXCEPTION) << "Failed, py_exe_path input is not a str";
  }
  const auto &py_exe_path_s = py_exe_path.cast<std::string>();
  auto ms_context = MsContext::GetInstance();
  ms_context->set_param<std::string>(MS_CTX_PYTHON_EXE_PATH, py_exe_path_s);
}

void PyNativeExecutor::set_kernel_build_server_dir(const py::object &kernel_build_server_dir) const {
  if (!py::isinstance<py::str>(kernel_build_server_dir)) {
    MS_LOG(EXCEPTION) << "Failed, kernel_build_server_dir input is not a str";
  }
  const auto &kernel_build_server_dir_s = kernel_build_server_dir.cast<std::string>();
  auto ms_context = MsContext::GetInstance();
  ms_context->set_param<std::string>(MS_CTX_KERNEL_BUILD_SERVER_DIR, kernel_build_server_dir_s);
}

void PyNativeExecutor::ClearRes() const {
  forward_executor()->ClearForwardTask();
  runtime::OpExecutor::GetInstance().Reset();
  // Clear forward tasks before clear op graphs cache.
  pynative::OpCompiler::GetInstance().ClearAllCache();
  pynative::autograd::ClearPyNativeAutoGradStaticRes();

  // Maybe exit in runop step
  auto ms_context = MsContext::GetInstance();
  if (ms_context != nullptr) {
    ms_context->set_param<bool>(MS_CTX_ENABLE_PYNATIVE_INFER, false);
  }
  ConfigManager::GetInstance().ResetIterNum();
  if (forward_executor_ != nullptr) {
    forward_executor_->ClearRes();
  }
  if (grad_executor_ != nullptr) {
    grad_executor_->ClearRes();
  }
  ad::CleanRes();
  pipeline::ReclaimOptimizer();
  MS_LOG(DEBUG) << "Clear all res";
}

void PyNativeExecutor::Init() {
  MS_LOG(DEBUG) << "Init PyNativeExecutor";
  forward_executor_ = std::make_shared<ForwardExecutor>();
  forward_executor_->Init();
  grad_executor_ = std::make_shared<GradExecutor>(forward_executor_);
  grad_executor_->Init();
  forward_executor_->set_grad_executor(grad_executor_);
}

void PyNativeExecutor::Sync() const {
  forward_executor()->Sync();
  runtime::ProfilerAnalyzer::GetInstance().EndStep();
  runtime::ProfilerAnalyzer::GetInstance().StartStep();
}

void PyNativeExecutor::SetHookChanged(const py::object &cell) const {
  if (!py::isinstance<Cell>(cell)) {
    MS_LOG(EXCEPTION) << "The 'set_hook_changed' function is only supported on Cell object!";
  }
  grad_executor()->SetHookChanged(cell);
}

bool PyNativeExecutor::grad_flag() const { return grad_executor()->grad_flag(); }

void PyNativeExecutor::set_grad_flag(bool flag) const { grad_executor()->set_grad_flag(flag); }

bool PyNativeExecutor::enable_grad() const { return grad_executor()->enable_grad(); }

void PyNativeExecutor::set_enable_grad(bool enable_grad) const { grad_executor()->set_enable_grad(enable_grad); }

py::object PyNativeExecutor::CheckAlreadyRun(const prim::GradOperationPtr &grad, const py::object &obj,
                                             const py::object &weights, const py::object &grad_hash_id,
                                             const py::args &args) const {
  return grad_executor()->CheckAlreadyRun(grad, obj, weights, grad_hash_id, args);
}

void PyNativeExecutor::NewGraph(const py::object &obj, const py::args &args) const {
  forward_executor()->ProcessBeforeNewGraph(obj);

  if (!grad_executor()->RequiresGrad()) {
    MS_LOG(DEBUG) << "Grad flag is false";
    return;
  }
  PyNativeExecutorTry(grad_executor()->InitGraph, obj, args);
  forward_executor()->ProcessAfterNewGraph(obj);
}

void PyNativeExecutor::EndGraph(const py::object &obj, const py::object &out, const py::args &args) const {
  bool is_cell = py::isinstance<Cell>(obj);
  forward_executor()->ProcessBeforeEndGraph(obj, is_cell);

  if (!grad_executor()->RequiresGrad()) {
    MS_LOG(DEBUG) << "Grad flag is false";
    return;
  }
  PyNativeExecutorTry(grad_executor()->LinkGraph, obj, out, args);
  forward_executor()->ProcessAfterEndGraph(obj, is_cell);
}

py::object PyNativeExecutor::Run() const {
  runtime::ProfilerStageRecorder recorder(runtime::ProfilerStage::kRunGradGraph);
  const auto &ret = PyNativeExecutorTry(grad_executor()->RunGraph);
  return ret;
}

void PyNativeExecutor::GradNet(const prim::GradOperationPtr &grad, const py::object &cell, const py::object &weights,
                               const py::object &grad_position, const py::args &args) const {
  runtime::ProfilerStageRecorder recorder(runtime::ProfilerStage::kCompileGradGraph);
  PyNativeExecutorTry(grad_executor()->GradGraph, grad, cell, weights, grad_position, args);
}

py::object PyNativeExecutor::GradJit(const py::object &out, const py::args &args) const {
  const auto &ret = grad_executor()->jit()->GradJit(out, args);
  return ret;
}

void PyNativeExecutor::SetLazyBuild(bool enable) const { forward_executor()->set_lazy_build(enable); }

bool PyNativeExecutor::IsFirstCell() const { return forward_executor()->IsFirstCell(); }

void PyNativeExecutor::WorkerJoin() {
  GilReleaseWithCheck release_gil;
  forward_executor_->WorkerJoin();
}

void PyNativeExecutor::SetJitCompileStatus(bool is_compiling, const std::string &phase) const {
  forward_executor()->set_is_jit_compiling(is_compiling);
  grad_executor()->jit()->set_graph_phase(phase);
}

void PyNativeExecutor::SetDynamicInput(const py::object &cell) const {
  grad_executor()->SaveDynamicInputsCells(cell);
  MS_LOG(INFO) << "Set dynamic shape by set inputs";
}

void RegPyNativeExecutor(const py::module *m) {
  stub::RegStubNodes(m);

  (void)py::class_<PyNativeExecutor, std::shared_ptr<PyNativeExecutor>>(*m, "PyNativeExecutor_")
    .def_static("get_instance", &PyNativeExecutor::GetInstance, "PyNativeExecutor get_instance.")
    .def("is_first_cell", &PyNativeExecutor::IsFirstCell, "check if the first cell.")
    .def("new_graph", &PyNativeExecutor::NewGraph, "pynative new a graph.")
    .def("end_graph", &PyNativeExecutor::EndGraph, "pynative end a graph.")
    .def("check_run", &PyNativeExecutor::CheckAlreadyRun, "pynative check graph run before.")
    .def("grad_jit", &PyNativeExecutor::GradJit, "pynative grad for jit.")
    .def("grad_net", &PyNativeExecutor::GradNet, "pynative grad graph.")
    .def("clear_res", &PyNativeExecutor::ClearRes, "pynative clear exception res.")
    .def("sync", &PyNativeExecutor::Sync, "pynative sync stream.")
    .def("set_lazy_build", &PyNativeExecutor::SetLazyBuild, "pynative build kernel async")
    .def("__call__", &PyNativeExecutor::Run, "pynative executor run grad graph.")
    .def("grad_flag", &PyNativeExecutor::grad_flag, "pynative grad flag")
    .def("enable_grad", &PyNativeExecutor::enable_grad, "pynative enable grad, used for with no_grad")
    .def("set_hook_changed", &PyNativeExecutor::SetHookChanged, "set pynative hook changed")
    .def("set_grad_flag", &PyNativeExecutor::set_grad_flag, py::arg("flag") = py::bool_(false),
         "Executor set grad flag.")
    .def("set_enable_grad", &PyNativeExecutor::set_enable_grad, py::arg("enable_grad") = py::bool_(true),
         "pynative set enable grad")
    .def("set_dynamic_input", &PyNativeExecutor::SetDynamicInput, "set dynamic input")
    .def("set_py_exe_path", &PyNativeExecutor::set_py_exe_path, py::arg("py_exe_path") = py::str(""),
         "set python executable path.")
    .def("set_kernel_build_server_dir", &PyNativeExecutor::set_kernel_build_server_dir,
         py::arg("kernel_build_server_dir") = py::str(""), "set kernel build server directory path.")
    .def("set_jit_compile_status", &PyNativeExecutor::SetJitCompileStatus, "set jit compile status.")
    .def("real_run_op", &PyNativeExecutor::RealRunOp, "Run op pynatively.")
    .def("run_op_async", &PyNativeExecutor::RunOpStub, "run op asynchronously")
    .def("constant_folding", &PyNativeExecutor::CallConstantFolding, "Call Constant Folding Primitive");
}
}  // namespace mindspore::pynative
