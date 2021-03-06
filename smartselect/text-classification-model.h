/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Inference code for the feed-forward text classification models.

#ifndef LIBTEXTCLASSIFIER_SMARTSELECT_TEXT_CLASSIFICATION_MODEL_H_
#define LIBTEXTCLASSIFIER_SMARTSELECT_TEXT_CLASSIFICATION_MODEL_H_

#include <memory>
#include <set>
#include <string>

#include "common/embedding-network.h"
#include "common/feature-extractor.h"
#include "common/memory_image/embedding-network-params-from-image.h"
#include "common/mmap.h"
#include "smartselect/feature-processor.h"
#include "smartselect/model-params.h"
#include "smartselect/text-classification-model.pb.h"
#include "smartselect/types.h"

namespace libtextclassifier {

// SmartSelection/Sharing feed-forward model.
class TextClassificationModel {
 public:
  // Represents a result of Annotate call.
  struct AnnotatedSpan {
    // Unicode codepoint indices in the input string.
    CodepointSpan span = {kInvalidIndex, kInvalidIndex};

    // Classification result for the span.
    std::vector<std::pair<std::string, float>> classification;
  };

  // Loads TextClassificationModel from given file given by an int
  // file descriptor.
  // Offset is byte a position in the file to the beginning of the model data.
  TextClassificationModel(int fd, int offset, int size);

  // Same as above but the whole file is mapped and it is assumed the model
  // starts at offset 0.
  explicit TextClassificationModel(int fd);

  // Loads TextClassificationModel from given file.
  explicit TextClassificationModel(const std::string& path);

  // Loads TextClassificationModel from given location in memory.
  TextClassificationModel(const void* addr, int size);

  // Returns true if the model is ready for use.
  bool IsInitialized() { return initialized_; }

  // Bit flags for the input selection.
  enum SelectionInputFlags { SELECTION_IS_URL = 0x1, SELECTION_IS_EMAIL = 0x2 };

  // Runs inference for given a context and current selection (i.e. index
  // of the first and one past last selected characters (utf8 codepoint
  // offsets)). Returns the indices (utf8 codepoint offsets) of the selection
  // beginning character and one past selection end character.
  // Returns the original click_indices if an error occurs.
  // NOTE: The selection indices are passed in and returned in terms of
  // UTF8 codepoints (not bytes).
  // Requires that the model is a smart selection model.
  CodepointSpan SuggestSelection(const std::string& context,
                                 CodepointSpan click_indices) const;

  // Classifies the selected text given the context string.
  // Requires that the model is a smart sharing model.
  // Returns an empty result if an error occurs.
  std::vector<std::pair<std::string, float>> ClassifyText(
      const std::string& context, CodepointSpan click_indices,
      int input_flags = 0) const;

  // Annotates given input text. The annotations should cover the whole input
  // context except for whitespaces, and are sorted by their position in the
  // context string.
  std::vector<AnnotatedSpan> Annotate(const std::string& context) const;

 protected:
  // Initializes the model from mmap_ file.
  void InitFromMmap();

  // Extracts chunks from the context. The extraction proceeds from the center
  // token determined by click_span and looks at relative_click_span tokens
  // left and right around the click position.
  // If relative_click_span == {kInvalidIndex, kInvalidIndex} then the whole
  // context is considered, regardless of the click_span.
  // Returns the chunks sorted by their position in the context string.
  std::vector<CodepointSpan> Chunk(const std::string& context,
                                   CodepointSpan click_span,
                                   TokenSpan relative_click_span) const;

  // During evaluation we need access to the feature processor.
  FeatureProcessor* SelectionFeatureProcessor() const {
    return selection_feature_processor_.get();
  }

  void InitializeSharingRegexPatterns(
      const std::vector<SharingModelOptions::RegexPattern>& patterns);

  // Collection name when url hint is accepted.
  const std::string kUrlHintCollection = "url";

  // Collection name when email hint is accepted.
  const std::string kEmailHintCollection = "email";

  // Collection name for other.
  const std::string kOtherCollection = "other";

  // Collection name for phone.
  const std::string kPhoneCollection = "phone";

  SelectionModelOptions selection_options_;
  SharingModelOptions sharing_options_;

 private:
#ifndef LIBTEXTCLASSIFIER_DISABLE_ICU_SUPPORT
  struct CompiledRegexPattern {
    std::string collection_name;
    std::unique_ptr<icu::RegexPattern> pattern;
  };
#endif

  bool LoadModels(const void* addr, int size);

  nlp_core::EmbeddingNetwork::Vector InferInternal(
      const std::string& context, CodepointSpan span,
      const FeatureProcessor& feature_processor,
      const nlp_core::EmbeddingNetwork& network,
      const FeatureVectorFn& feature_vector_fn,
      std::vector<CodepointSpan>* selection_label_spans) const;

  // Returns a selection suggestion with a score.
  std::pair<CodepointSpan, float> SuggestSelectionInternal(
      const std::string& context, CodepointSpan click_indices) const;

  // Returns a selection suggestion and makes sure it's symmetric. Internally
  // runs several times SuggestSelectionInternal.
  CodepointSpan SuggestSelectionSymmetrical(const std::string& context,
                                            CodepointSpan click_indices) const;

  bool initialized_ = false;
  std::unique_ptr<nlp_core::ScopedMmap> mmap_;
  std::unique_ptr<ModelParams> selection_params_;
  std::unique_ptr<FeatureProcessor> selection_feature_processor_;
  std::unique_ptr<nlp_core::EmbeddingNetwork> selection_network_;
  FeatureVectorFn selection_feature_fn_;
  std::unique_ptr<FeatureProcessor> sharing_feature_processor_;
  std::unique_ptr<ModelParams> sharing_params_;
  std::unique_ptr<nlp_core::EmbeddingNetwork> sharing_network_;
  FeatureVectorFn sharing_feature_fn_;
#ifndef LIBTEXTCLASSIFIER_DISABLE_ICU_SUPPORT
  std::vector<CompiledRegexPattern> regex_patterns_;
#endif
};

// If the first or the last codepoint of the given span is a bracket, the
// bracket is stripped if the span does not contain its corresponding paired
// version.
CodepointSpan StripUnpairedBrackets(const std::string& context,
                                    CodepointSpan span);

// Parses the merged image given as a file descriptor, and reads
// the ModelOptions proto from the selection model.
bool ReadSelectionModelOptions(int fd, ModelOptions* model_options);

// Pretty-printing function for TextClassificationModel::AnnotatedSpan.
inline std::ostream& operator<<(
    std::ostream& os, const TextClassificationModel::AnnotatedSpan& span) {
  std::string best_class;
  float best_score = -1;
  if (!span.classification.empty()) {
    best_class = span.classification[0].first;
    best_score = span.classification[0].second;
  }
  return os << "Span(" << span.span.first << ", " << span.span.second << ", "
            << best_class << ", " << best_score << ")";
}

}  // namespace libtextclassifier

#endif  // LIBTEXTCLASSIFIER_SMARTSELECT_TEXT_CLASSIFICATION_MODEL_H_
