#ifndef IMAGECPP_SERVER_MODEL_CACHE_HPP
#define IMAGECPP_SERVER_MODEL_CACHE_HPP

#include "imagecpp/imagecpp.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace imagecpp::server {

struct ModelCacheInfo {
    size_t capacity = 0;
    size_t size = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t clears = 0;
    std::vector<std::string> loaded_families;
};

class ModelCache final {
  public:
    using Loader = std::function<std::shared_ptr<imagecpp::Model>()>;

    explicit ModelCache(size_t capacity);

    ModelCache(const ModelCache &) = delete;
    ModelCache &operator=(const ModelCache &) = delete;

    std::shared_ptr<imagecpp::Model> acquire(const std::string &family, const Loader &loader);
    size_t clear();
    ModelCacheInfo info() const;

  private:
    struct Entry {
        std::shared_ptr<imagecpp::Model> model;
        std::list<std::string>::iterator recency;
    };

    const size_t capacity_;
    mutable std::mutex mutex_;
    std::list<std::string> recency_;
    std::unordered_map<std::string, Entry> entries_;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
    uint64_t clears_ = 0;
};

} // namespace imagecpp::server

#endif
