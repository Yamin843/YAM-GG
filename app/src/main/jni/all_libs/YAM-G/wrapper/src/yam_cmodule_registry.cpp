// ===========================================================================
// yam_cmodule_registry.cpp
// ===========================================================================

#include "yam_cmodule_registry.hpp"

namespace yam {

CModuleRegistry& CModuleRegistry::instance() {
    static CModuleRegistry inst;
    return inst;
}

Result<void> CModuleRegistry::add(const String& name, Ptr<CModule> m) {
    if (name.empty()) return Result<void>::err(ErrorCode::InvalidArgument, "empty name");
    if (!m || !m->valid())
        return Result<void>::err(ErrorCode::InvalidArgument, "invalid module");
    std::lock_guard<std::mutex> lk(mu_);
    modules_[name] = std::move(m);
    return Result<void>::ok();
}

Ptr<CModule> CModuleRegistry::find(const String& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = modules_.find(name);
    return it == modules_.end() ? nullptr : it->second;
}

bool CModuleRegistry::remove(const String& name) {
    std::lock_guard<std::mutex> lk(mu_);
    return modules_.erase(name) > 0;
}

std::vector<String> CModuleRegistry::names() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<String> out;
    out.reserve(modules_.size());
    for (auto& p : modules_) out.push_back(p.first);
    return out;
}

void CModuleRegistry::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    modules_.clear();
}

usize CModuleRegistry::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return modules_.size();
}

} // namespace yam
