#pragma once

#include "dmfr/snapshot/snapshot.h"

#include <nlohmann/json.hpp>
#include <string>

namespace dmfr::diff {

// Compare two snapshot manifests and return a structured diff report.
nlohmann::json compare(const dmfr::snapshot::SnapshotManifest& before,
                       const dmfr::snapshot::SnapshotManifest& after);

// Convenience helpers for common collections.
nlohmann::json compare_string_maps(const std::map<std::string, std::string>& before,
                                   const std::map<std::string, std::string>& after);

nlohmann::json compare_string_sets(const std::vector<std::string>& before,
                                   const std::vector<std::string>& after);

nlohmann::json compare_manifest_files(const std::vector<dmfr::snapshot::ManifestFile>& before,
                                      const std::vector<dmfr::snapshot::ManifestFile>& after);

} // namespace dmfr::diff
