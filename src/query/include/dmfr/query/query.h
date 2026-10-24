#pragma once

#include "dmfr/common/event.h"
#include "dmfr/store/store.h"

#include <string>
#include <vector>

namespace dmfr::query {

struct ParsedQuery {
    store::Query store_query;
    std::string display_mode{"timeline"};
};

class Engine {
public:
    explicit Engine(const store::Store& store);

    ParsedQuery parse(const std::string& input) const;
    std::vector<Event> execute(const ParsedQuery& query) const;

private:
    const store::Store& store_;
};

} // namespace dmfr::query
