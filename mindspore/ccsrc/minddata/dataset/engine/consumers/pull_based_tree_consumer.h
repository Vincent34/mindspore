/**
 * Copyright 2021-2023 Huawei Technologies Co., Ltd
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
#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_CONSUMERS_PULL_BASED_TREE_CONSUMER_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_CONSUMERS_PULL_BASED_TREE_CONSUMER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstddef>
#include "minddata/dataset/engine/tree_adapter_lite.h"
#include "minddata/dataset/engine/consumers/tree_consumer.h"

namespace mindspore::dataset {

class TreeAdapterLite;
class TensorRow;

/// Consumer that iterates over the dataset and returns the rows one by one as a in a pull based fashion
class PullBasedIteratorConsumer : public TreeConsumer {
 public:
  /// Constructor
  /// \param num_epochs number of epochs. Default: 1.
  explicit PullBasedIteratorConsumer(int32_t num_epochs = 1) : TreeConsumer(num_epochs) {
    tree_adapter_lite_ = std::make_unique<TreeAdapterLite>();
  }

  ~PullBasedIteratorConsumer() = default;

  Status Init(std::shared_ptr<DatasetNode> root) override;

  /// \brief Returns the next row in a vector format
  /// \note This is currently a placeholder function
  /// \param[in] num_rows the number of rows that we want to get
  /// \return out std::vector of TensorRows
  std::vector<TensorRow> GetRows(int64_t num_rows);

  /// Returns the next row in a vector format
  /// \param[out] out std::vector of Tensors
  /// \return Status error code
  Status GetNextAsVector(std::vector<TensorPtr> *const out) override;

  /// Returns the next row in as a map
  /// \param[out] out std::map of string to Tensor
  /// \return Status error code
  Status GetNextAsMap(std::unordered_map<std::string, TensorPtr> *const out) override;

  /// Returns the next row in as a vector
  /// \param[out] vec std::vector of pairs of string to Tensor
  /// \return Status error code
  Status GetNextAsOrderedPair(std::vector<std::pair<std::string, std::shared_ptr<Tensor>>> *const vec) override;

  /// Function to reset the current consumer to the provided step.
  /// \note Reset is NOT supported for pull-based iterators.
  /// \param step the step to reset the pipeline to.
  /// \param epoch_num the epoch to reset the pipeline to.
  /// \return Status error code
  Status Reset(int64_t step, const int64_t epoch_num) {
    RETURN_STATUS_UNEXPECTED(
      "Failover reset is not supported for pull-based iterators (including when Debug mode is enabled).");
  }

 protected:
  /// Method to return the name of the consumer
  /// \return string
  std::string Name() override { return "PullBasedIteratorConsumer"; }

 private:
  std::unique_ptr<TreeAdapterLite> tree_adapter_lite_;
  std::vector<std::pair<std::string, int32_t>> column_order_;  // key: column name, val: column id
};
}  // namespace mindspore::dataset
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_ENGINE_CONSUMERS_PULL_BASED_TREE_CONSUMER_H_
