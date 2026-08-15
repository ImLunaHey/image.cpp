#ifndef IMAGECPP_SERVER_JOB_MANAGER_HPP
#define IMAGECPP_SERVER_JOB_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace imagecpp::server {

enum class JobStatus {
    queued,
    running,
    completed,
    failed,
    cancelled,
};

struct JobManagerConfig {
    size_t worker_count = 1;
    size_t max_queued_jobs = 16;
    size_t max_retained_jobs = 64;
    std::chrono::seconds retention_time{900};
};

struct JobOutput {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct JobSnapshot {
    std::string id;
    std::string operation;
    JobStatus status = JobStatus::queued;
    float progress = 0.0F;
    std::string stage;
    int http_status = 0;
    int64_t created_at_ms = 0;
    int64_t started_at_ms = 0;
    int64_t finished_at_ms = 0;
    int64_t queue_position = -1;
    std::string error;
};

struct JobManagerInfo {
    size_t worker_count = 0;
    size_t max_queued_jobs = 0;
    size_t max_retained_jobs = 0;
    int64_t retention_seconds = 0;
    size_t queued_jobs = 0;
    size_t running_jobs = 0;
    size_t retained_jobs = 0;
};

class JobContext final {
  public:
    bool cancellation_requested() const;
    void report(float progress, std::string stage) const;

  private:
    friend class JobManager;
    JobContext(std::function<bool()> cancelled, std::function<void(float, std::string)> reporter);

    std::function<bool()> cancelled_;
    std::function<void(float, std::string)> reporter_;
};

class JobQueueFull final : public std::runtime_error {
  public:
    JobQueueFull();
};

class JobManager final {
  public:
    using Work = std::function<JobOutput(const JobContext &)>;

    explicit JobManager(JobManagerConfig config);
    ~JobManager();

    JobManager(const JobManager &) = delete;
    JobManager &operator=(const JobManager &) = delete;

    JobSnapshot submit(std::string operation, Work work);
    std::optional<JobSnapshot> get(const std::string &id);
    std::vector<JobSnapshot> list(size_t limit);
    std::optional<JobSnapshot> cancel(const std::string &id);
    std::optional<JobOutput> output(const std::string &id);
    JobManagerInfo info();
    void shutdown() noexcept;

  private:
    struct Job;

    static int64_t now_ms();
    static bool terminal(JobStatus status);
    JobSnapshot snapshot_locked(const std::shared_ptr<Job> &job) const;
    void report(const std::shared_ptr<Job> &job, float progress, std::string stage);
    void prune_locked(int64_t now);
    void worker_loop();

    JobManagerConfig config_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> pending_;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
    std::vector<std::thread> workers_;
    uint64_t next_id_ = 1;
    bool stopping_ = false;
};

const char *job_status_name(JobStatus status) noexcept;

} // namespace imagecpp::server

#endif
