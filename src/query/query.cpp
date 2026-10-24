#include "dmfr/query/query.h"

namespace dmfr::query {

Engine::Engine(const store::Store& store) : store_(store) {}

ParsedQuery Engine::parse(const std::string& input) const {
    (void)input;
    ParsedQuery q;
    q.store_query.limit = 100;
    return q;
}

std::vector<Event> Engine::execute(const ParsedQuery& query) const {
    return store_.query(query.store_query);
}

} // namespace dmfr::query
