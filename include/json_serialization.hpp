#pragma once
#include <nlohmann/json.hpp>
#include "data_model.hpp"
#include "package_loader.hpp"

namespace xpg {

template <class Json>
void to_json(Json &j, const VersionInfo &info) {
  j["version"] = info.version;
  j["architecture"] = info.architecture;
}

template <class Json>
void to_json(Json &j, const DependencyTree &tree) {
  j["name"] = tree.name;
  j["type"] = tree.type;
  j["version_constraint"] = tree.version_constraint;
  j["architecture"] = tree.architecture;
  auto &jdependencies = j["dependencies"];
  jdependencies["single_dependencies"] = tree.single_dependencies;
  jdependencies["alternative_dependencies"] = tree.alternative_dependencies;
}

template <class Json>
void to_json(Json &j, const DependencyInfo &info) {
  j["name"] = info.name;
  j["type"] = info.type;
  j["version_constraint"] = info.version_constraint;
  j["architecture"] = info.architecture_constraint;
}

template <class Json>
void to_json(Json &j, const DependencyFlat &flat) {
  for (auto i : std::views::iota(0ull, flat.size())) {
    auto &jlevel = j["depth " + std::to_string(i + 1)];
    jlevel["single_dependencies"] = flat[i].single_dependencies;
    jlevel["alternative_dependencies"] = flat[i].alternative_dependencies;
  }
}

template <class Json>
void to_json(Json &j, const std::variant<DependencyTree, DependencyFlat> &result) {
  std::visit([&j]<class T>(const T &value) {
    if constexpr (std::same_as<T, DependencyTree>) {
      j["single_dependencies"] = value.single_dependencies;
      j["alternative_dependencies"] = value.alternative_dependencies;
    } else if constexpr (std::same_as<T, DependencyFlat>) j = value;
  }, result);
}

template <class Json>
void to_json(Json &j, const RepositoryInfo &info) {
  j["enabled"] = info.enabled;
  if (info.type == kDEB) j["type"] = "DEB";
  else if (info.type == kRPM) j["type"] = "RPM";
  j["urls"] = info.urls;
  j["distributions"] = info.distributions;
  j["architectures"] = info.architectures;
}

template <class Json>
void from_json(const Json &j, RepositoryInfo &info) {
  info.enabled = j.value("enabled", true);
  auto type = j.value("type", std::string("DEB"));
  if (type == "DEB" || type == "deb") info.type = kDEB;
  else if (type == "RPM" || type == "rpm") info.type = kRPM;
  info.urls = j.value("urls", decltype(info.urls){});
  info.distributions = j.value("distributions", decltype(info.distributions){});
  info.architectures = j.value("architectures", decltype(info.architectures){});
}

} // namespace xpg
