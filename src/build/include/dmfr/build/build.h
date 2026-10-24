#pragma once

#include "dmfr/store/store.h"
#include "dmfr/scrubber/scrubber.h"

namespace dmfr::build {

// Record the calling process environment before executing argv without a shell.
// Child output and terminal input pass through. Returns the child's exit status.
int record(const std::vector<std::string>& argv, const std::string& tool,
           store::Store& store, scrubber::Scrubber& scrubber);

int unrecorded(const std::vector<std::string>& argv);

// Compare the selected failure (or latest in project) with the previous success
// for the same command and argument vector. Missing evidence is explicit.
nlohmann::json diagnose(store::Store& store, scrubber::Scrubber& scrubber,
                        const std::string& project, const std::string& event_id = "");
}
