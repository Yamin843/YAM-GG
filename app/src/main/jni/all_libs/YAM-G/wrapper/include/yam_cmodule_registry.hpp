#ifndef YAM_CMODULE_REGISTRY_HPP
#define YAM_CMODULE_REGISTRY_HPP

// ===========================================================================
// yam_cmodule_registry.hpp — named CModule registry
// ===========================================================================

#include "yam.hpp"
#include "yam_subsystems.hpp"

namespace yam {

class CModuleRegistry {
public:
    static CModuleRegistry& instance();

    // Register a named module. Overwrites any existing entry with same name.
    Result<void> add(const String& name, Ptr<CModule> m);

    // Look up by name.
    Ptr<CModule> find(const String& name) const;

    // Remove and destroy.
    bool remove(const String& name);

    // List all names.
    std::vector<String> names() const;

    // Clear all.
    void clear();

    usize size() const;

private:
    CModuleRegistry() = default;
    ~CModuleRegistry() = default;

    mutable std::mutex mu_;
    std::unordered_map<String, Ptr<CModule>> modules_;
};

inline CModuleRegistry& cmodules() { return CModuleRegistry::instance(); }

} // namespace yam

#endif // YAM_CMODULE_REGISTRY_HPP
