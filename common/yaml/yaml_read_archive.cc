#include "drake/common/yaml/yaml_read_archive.h"

#include <algorithm>
#include <cstring>

#include <fmt/ostream.h>

#include "drake/common/nice_type_name.h"

namespace drake {
namespace yaml {

YamlReadArchive::YamlReadArchive(const YAML::Node& root)
    : YamlReadArchive(root, Options{}) {}

YamlReadArchive::YamlReadArchive(const YAML::Node& root, const Options& options)
    : owned_root_(root),
      root_(&owned_root_),
      mapish_item_key_(nullptr),
      mapish_item_value_(nullptr),
      options_(options),
      parent_(nullptr) {
  // Reprocess the owned_root for merge keys only after all member fields are
  // initialized; otherwise, the method might access invalid member data.
  RewriteMergeKeys(const_cast<YAML::Node*>(&owned_root_));
}

namespace {

// The source and destination are both of type Map.  Copy the key-value pairs
// from source into destination, but don't overwrite any existing keys.
void CopyWithMergeKeySemantics(
    const YAML::Node& source, YAML::Node* destination) {
  for (const auto& key_value : source) {
    const YAML::Node& key = key_value.first;
    const YAML::Node& value = key_value.second;
    YAML::Node entry = (*destination)[key.Scalar()];
    if (!entry) {
      // N.B. This assignment indirectly mutates the `destination`!  With the
      // yaml-cpp API, a YAML::Node returned from a mapping lookup (operator[])
      // is a back-pointer into the mapping -- something akin to an iterator.
      entry = value;
    }
  }
}

}  // namespace

void YamlReadArchive::RewriteMergeKeys(YAML::Node* node) const {
  DRAKE_DEMAND(node != nullptr);
  DRAKE_DEMAND(node->Type() == YAML::NodeType::Map);
  const YAML::Node& merge_key = (*node)["<<"];
  if (!merge_key) {
    return;
  }
  (*node).remove("<<");
  switch (merge_key.Type()) {
    case YAML::NodeType::Map: {
      // Merge `merge_key` Map into `node` Map.
      CopyWithMergeKeySemantics(merge_key, node);
      return;
    }
    case YAML::NodeType::Sequence: {
      // Merge each Map in `merge_key` Sequence-of-Maps into the `node` Map.
      for (const YAML::Node& merge_key_item : merge_key) {
        if (merge_key_item.Type() != YAML::NodeType::Map) {
          ReportError(
              "has invalid merge key type (Sequence-of-non-Mapping) within");
        }
        CopyWithMergeKeySemantics(merge_key_item, node);
      }
      return;
    }
    case YAML::NodeType::Scalar: {
      ReportError("has invalid merge key type (Scalar) within");
      return;
    }
    case YAML::NodeType::Null: {
      ReportError("has invalid merge key type (Null) within");
      return;
    }
    case YAML::NodeType::Undefined: {
      // We should never reach here due to the "if (!merge_key)" guard above.
      DRAKE_UNREACHABLE();
    }
  }
  DRAKE_UNREACHABLE();
}

bool YamlReadArchive::has_root() const {
  if (mapish_item_key_ != nullptr) {
    return true;
  }
  DRAKE_DEMAND(root_ != nullptr);
  return !(root_->IsNull());
}

YAML::Node YamlReadArchive::MaybeGetSubNode(const char* name) const {
  DRAKE_DEMAND(name != nullptr);
  if (mapish_item_key_ != nullptr) {
    DRAKE_DEMAND(mapish_item_value_ != nullptr);
    if (std::strcmp(mapish_item_key_, name) == 0) {
      return *mapish_item_value_;
    }
    return YAML::Node(YAML::NodeType::Undefined);
  }
  DRAKE_DEMAND(root_ != nullptr);
  return (*root_)[name];
}

YAML::Node YamlReadArchive::GetSubNode(
    const char* name, YAML::NodeType::value expected_type) const {
  YAML::Node result = MaybeGetSubNode(name);
  if (!result) {
    if (!options_.allow_cpp_with_no_yaml) {
      ReportError("is missing");
    }
    return result;
  }
  const auto& actual_type = result.Type();
  if (actual_type != expected_type) {
    ReportError(fmt::format(
        "has non-{} ({})", to_string(expected_type), to_string(actual_type)));
    result = YAML::Node(YAML::NodeType::Undefined);
    return result;
  }
  if (expected_type == YAML::NodeType::Map) {
    RewriteMergeKeys(&result);
  }
  return result;
}

void YamlReadArchive::CheckAllAccepted() const {
  // This function is only ever called on Serializeable nodes (i.e., where we
  // have a real Mapping node).  Calling it with a map-ish key (i.e., while
  // parsing a sequence) would mean that YamlReadArchive went off the rails.
  DRAKE_DEMAND(mapish_item_key_ == nullptr);
  DRAKE_DEMAND(root_->Type() == YAML::NodeType::Map);
  if (options_.allow_yaml_with_no_cpp) {
    return;
  }
  for (const auto& map_pair : *root_) {
    const std::string& name = map_pair.first.as<std::string>();
    if (visited_names_.count(name) == 0) {
      ReportError(fmt::format(
          "key {} did not match any visited value", name));
    }
  }
}

void YamlReadArchive::ReportError(const std::string& note) const {
  std::ostringstream e;  // A buffer for the error message text.
  this->PrintNodeSummary(e);
  fmt::print(e, " {} entry for ", note);
  PrintVisitNameType(e);
  for (auto* archive = parent_; archive; archive = archive->parent_) {
    fmt::print(e, " while accepting ");
    archive->PrintNodeSummary(e);
    if (archive->debug_visit_name_ != nullptr) {
      fmt::print(e, " while visiting ");
      archive->PrintVisitNameType(e);
    }
  }
  fmt::print(e, ".");
  throw std::runtime_error(e.str());
}

void YamlReadArchive::PrintNodeSummary(std::ostream& s) const {
  YAML::Node to_print;
  if (mapish_item_key_ != nullptr) {
    DRAKE_DEMAND(mapish_item_value_ != nullptr);
    to_print[mapish_item_key_] = *mapish_item_value_;
  } else {
    DRAKE_DEMAND(root_ != nullptr);
    to_print = *root_;
  }
  fmt::print(s, "YAML node of type {}", to_string(to_print.Type()));
  switch (to_print.Type()) {
    case YAML::NodeType::Undefined:
    case YAML::NodeType::Null:
    case YAML::NodeType::Scalar:
    case YAML::NodeType::Sequence: {
      // Don't log any details of these.
      break;
    }
    case YAML::NodeType::Map: {
      const size_t size = to_print.size();
      fmt::print(s, " (with size {} and keys {{", size);
      // Sort the keys so that our error message is deterministic.
      std::vector<std::string> keys;
      keys.reserve(size);
      for (const auto& map_pair : to_print) {
        keys.emplace_back(map_pair.first.as<std::string>());
      }
      std::sort(keys.begin(), keys.end());
      DRAKE_DEMAND(keys.size() == size);
      for (size_t i = 0; i < size; ++i) {
        if (i == 0) {
          fmt::print(s, "{}", keys[i]);
        } else {
          fmt::print(s, ", {}", keys[i]);
        }
      }
      fmt::print(s, "}})");
      break;
    }
  }
}

void YamlReadArchive::PrintVisitNameType(std::ostream& s) const {
  if (debug_visit_name_ == nullptr) {
    s << "<root>";
    return;
  }
  DRAKE_DEMAND(debug_visit_name_ != nullptr);
  DRAKE_DEMAND(debug_visit_type_ != nullptr);
  fmt::print(s, "{} {}",
             drake::NiceTypeName::Get(*debug_visit_type_),
             debug_visit_name_);
}

const char* YamlReadArchive::to_string(YAML::NodeType::value x) {
  switch (x) {
    case YAML::NodeType::Undefined: return "Undefined";
    case YAML::NodeType::Null: return "Null";
    case YAML::NodeType::Scalar: return "Scalar";
    case YAML::NodeType::Sequence: return "Sequence";
    case YAML::NodeType::Map: return "Mapping";
  }
  return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, const YamlReadArchive::Options& x) {
  return os << "{.allow_yaml_with_no_cpp = "
            << x.allow_yaml_with_no_cpp
            << ", .allow_cpp_with_no_yaml = "
            << x.allow_cpp_with_no_yaml
            << ", .retain_map_defaults = "
            << x.retain_map_defaults << "}";
}

}  // namespace yaml
}  // namespace drake
