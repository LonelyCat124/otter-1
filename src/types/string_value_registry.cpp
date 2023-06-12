#include <cassert>
#include <string>
#include <map>
#include "public/types/string_value_registry.hpp"

struct string_registry {
    using mapping = std::map<std::string, uint32_t>;
    using mapped_type = mapping::mapped_type;
    mapping label_map;
    labeller_fn *get_label;
    const mapped_type default_label;
};

string_registry* string_registry_make(labeller_fn *labeller)
{
    assert(labeller != nullptr);
    string_registry* registry = new string_registry{};
    registry->get_label = labeller;
    return registry;
}

void string_registry_delete(string_registry* registry, deleter_fn* deleter, deleter_data data)
{
    if (deleter) {
        for (auto&[key, value] : registry->label_map) {
            deleter(key.c_str(), value, data);
        }
    }
    delete registry;
}

uint32_t string_registry_insert(string_registry* registry, const char* str)
{
    auto label = registry->label_map[str];
    if (label == registry->default_label) {
        label = registry->get_label();
        registry->label_map[str] = label;
    }
    return label;
}
