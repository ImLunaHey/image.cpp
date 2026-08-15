#include "server/model_cache.hpp"

#include <stdexcept>
#include <utility>

namespace imagecpp::server {

ModelCache::ModelCache(size_t capacity) : capacity_(capacity) {}

std::shared_ptr<imagecpp::Model> ModelCache::acquire(const std::string &family, const Loader &loader) {
    if (family.empty()) {
        throw std::invalid_argument("model cache family cannot be empty");
    }
    if (!loader) {
        throw std::invalid_argument("model cache loader cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = entries_.find(family);
    if (existing != entries_.end()) {
        ++hits_;
        recency_.splice(recency_.begin(), recency_, existing->second.recency);
        return existing->second.model;
    }

    ++misses_;
    std::shared_ptr<imagecpp::Model> model = loader();
    if (!model) {
        throw std::runtime_error("model cache loader returned no model");
    }
    if (capacity_ == 0) {
        return model;
    }

    while (entries_.size() >= capacity_) {
        const std::string evicted = recency_.back();
        recency_.pop_back();
        entries_.erase(evicted);
        ++evictions_;
    }
    recency_.push_front(family);
    entries_.emplace(family, Entry{model, recency_.begin()});
    return model;
}

size_t ModelCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t removed = entries_.size();
    entries_.clear();
    recency_.clear();
    ++clears_;
    return removed;
}

ModelCacheInfo ModelCache::info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ModelCacheInfo result;
    result.capacity = capacity_;
    result.size = entries_.size();
    result.hits = hits_;
    result.misses = misses_;
    result.evictions = evictions_;
    result.clears = clears_;
    result.loaded_families.assign(recency_.begin(), recency_.end());
    return result;
}

} // namespace imagecpp::server
