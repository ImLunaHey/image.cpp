#include "server/job_manager.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace imagecpp::server {

struct JobManager::Job {
    std::string id;
    std::string operation;
    JobStatus status = JobStatus::queued;
    float progress = 0.0F;
    std::string stage = "queued";
    int http_status = 0;
    int64_t created_at_ms = 0;
    int64_t started_at_ms = 0;
    int64_t finished_at_ms = 0;
    std::string error;
    std::atomic<bool> cancellation_requested{false};
    Work work;
    std::optional<JobOutput> output;
};

JobContext::JobContext(std::function<bool()> cancelled, std::function<void(float, std::string)> reporter)
    : cancelled_(std::move(cancelled)), reporter_(std::move(reporter)) {}

bool JobContext::cancellation_requested() const { return cancelled_(); }

void JobContext::report(float progress, std::string stage) const { reporter_(progress, std::move(stage)); }

JobQueueFull::JobQueueFull() : std::runtime_error("asynchronous job queue is full") {}

const char *job_status_name(JobStatus status) noexcept {
    switch (status) {
    case JobStatus::queued:
        return "queued";
    case JobStatus::running:
        return "running";
    case JobStatus::completed:
        return "completed";
    case JobStatus::failed:
        return "failed";
    case JobStatus::cancelled:
        return "cancelled";
    }
    return "unknown";
}

JobManager::JobManager(JobManagerConfig config) : config_(config) {
    if (config_.worker_count == 0 || config_.max_queued_jobs == 0 || config_.max_retained_jobs == 0 ||
        config_.retention_time.count() <= 0) {
        throw std::invalid_argument("job manager limits must be positive");
    }
    workers_.reserve(config_.worker_count);
    for (size_t index = 0; index < config_.worker_count; ++index) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

JobManager::~JobManager() { shutdown(); }

int64_t JobManager::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool JobManager::terminal(JobStatus status) {
    return status == JobStatus::completed || status == JobStatus::failed || status == JobStatus::cancelled;
}

JobSnapshot JobManager::snapshot_locked(const std::shared_ptr<Job> &job) const {
    JobSnapshot result{job->id,          job->operation,     job->status,        job->progress,       job->stage,
                       job->http_status, job->created_at_ms, job->started_at_ms, job->finished_at_ms, -1,
                       job->error};
    if (job->status == JobStatus::queued) {
        const auto found = std::find(pending_.begin(), pending_.end(), job);
        if (found != pending_.end()) {
            result.queue_position = static_cast<int64_t>(std::distance(pending_.begin(), found)) + 1;
        }
    }
    return result;
}

void JobManager::prune_locked(int64_t now) {
    const int64_t retention_ms = std::chrono::duration_cast<std::chrono::milliseconds>(config_.retention_time).count();
    for (auto iterator = jobs_.begin(); iterator != jobs_.end();) {
        const std::shared_ptr<Job> &job = iterator->second;
        if (terminal(job->status) && job->finished_at_ms > 0 && now - job->finished_at_ms >= retention_ms) {
            iterator = jobs_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    std::vector<std::shared_ptr<Job>> retained;
    for (const auto &item : jobs_) {
        if (terminal(item.second->status)) {
            retained.push_back(item.second);
        }
    }
    std::sort(retained.begin(), retained.end(),
              [](const auto &left, const auto &right) { return left->finished_at_ms < right->finished_at_ms; });
    while (retained.size() > config_.max_retained_jobs) {
        jobs_.erase(retained.front()->id);
        retained.erase(retained.begin());
    }
}

JobSnapshot JobManager::submit(std::string operation, Work work) {
    if (operation.empty() || !work) {
        throw std::invalid_argument("job operation and work are required");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
        throw std::runtime_error("job manager is shutting down");
    }
    prune_locked(now_ms());
    if (pending_.size() >= config_.max_queued_jobs) {
        throw JobQueueFull();
    }

    std::ostringstream identifier;
    identifier << "job-" << std::hex << std::setw(16) << std::setfill('0') << next_id_++;
    auto job = std::make_shared<Job>();
    job->id = identifier.str();
    job->operation = std::move(operation);
    job->created_at_ms = now_ms();
    job->work = std::move(work);
    jobs_.emplace(job->id, job);
    pending_.push_back(job);
    const JobSnapshot result = snapshot_locked(job);
    ready_.notify_one();
    return result;
}

std::optional<JobSnapshot> JobManager::get(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now_ms());
    const auto found = jobs_.find(id);
    return found == jobs_.end() ? std::nullopt : std::optional<JobSnapshot>(snapshot_locked(found->second));
}

std::vector<JobSnapshot> JobManager::list(size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now_ms());
    std::vector<std::shared_ptr<Job>> sorted;
    sorted.reserve(jobs_.size());
    for (const auto &item : jobs_) {
        sorted.push_back(item.second);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &left, const auto &right) { return left->created_at_ms > right->created_at_ms; });
    if (sorted.size() > limit) {
        sorted.resize(limit);
    }
    std::vector<JobSnapshot> result;
    result.reserve(sorted.size());
    for (const auto &job : sorted) {
        result.push_back(snapshot_locked(job));
    }
    return result;
}

std::optional<JobSnapshot> JobManager::cancel(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now_ms());
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) {
        return std::nullopt;
    }
    const std::shared_ptr<Job> &job = found->second;
    if (job->status == JobStatus::queued) {
        job->cancellation_requested.store(true);
        const auto pending = std::find(pending_.begin(), pending_.end(), job);
        if (pending != pending_.end()) {
            pending_.erase(pending);
        }
        job->status = JobStatus::cancelled;
        job->progress = 1.0F;
        job->stage = "cancelled";
        job->finished_at_ms = now_ms();
        job->work = {};
    } else if (job->status == JobStatus::running) {
        job->cancellation_requested.store(true);
        job->stage = "cancellation_requested";
    }
    return snapshot_locked(job);
}

std::optional<JobOutput> JobManager::output(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now_ms());
    const auto found = jobs_.find(id);
    if (found == jobs_.end() || !found->second->output) {
        return std::nullopt;
    }
    return found->second->output;
}

JobManagerInfo JobManager::info() {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now_ms());
    JobManagerInfo result;
    result.worker_count = config_.worker_count;
    result.max_queued_jobs = config_.max_queued_jobs;
    result.max_retained_jobs = config_.max_retained_jobs;
    result.retention_seconds = config_.retention_time.count();
    result.retained_jobs = jobs_.size();
    for (const auto &item : jobs_) {
        result.queued_jobs += item.second->status == JobStatus::queued ? 1U : 0U;
        result.running_jobs += item.second->status == JobStatus::running ? 1U : 0U;
    }
    return result;
}

void JobManager::report(const std::shared_ptr<Job> &job, float progress, std::string stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (job->status != JobStatus::running || job->cancellation_requested.load()) {
        return;
    }
    job->progress = std::clamp(progress, 0.05F, 0.95F);
    if (!stage.empty()) {
        job->stage = std::move(stage);
    }
}

void JobManager::worker_loop() {
    while (true) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty()) {
                return;
            }
            job = pending_.front();
            pending_.pop_front();
            if (job->status != JobStatus::queued) {
                continue;
            }
            job->status = JobStatus::running;
            job->progress = 0.05F;
            job->stage = "running";
            job->started_at_ms = now_ms();
        }

        JobContext context([job] { return job->cancellation_requested.load(); },
                           [this, job](float progress, std::string stage) { report(job, progress, std::move(stage)); });
        try {
            JobOutput output = job->work(context);
            std::lock_guard<std::mutex> lock(mutex_);
            if (job->cancellation_requested.load()) {
                job->status = JobStatus::cancelled;
                job->stage = "cancelled";
            } else {
                job->http_status = output.status;
                job->output = std::move(output);
                job->status =
                    job->http_status >= 200 && job->http_status < 400 ? JobStatus::completed : JobStatus::failed;
                job->stage = job->status == JobStatus::completed ? "completed" : "failed";
            }
        } catch (const std::exception &error) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (job->cancellation_requested.load()) {
                job->status = JobStatus::cancelled;
                job->stage = "cancelled";
            } else {
                job->status = JobStatus::failed;
                job->stage = "failed";
                job->error = error.what();
                job->http_status = 500;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            job->status = job->cancellation_requested.load() ? JobStatus::cancelled : JobStatus::failed;
            job->stage = job->status == JobStatus::cancelled ? "cancelled" : "failed";
            job->error = job->status == JobStatus::failed ? "unexpected job failure" : "";
            job->http_status = job->status == JobStatus::failed ? 500 : 0;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            job->progress = 1.0F;
            job->finished_at_ms = now_ms();
            job->work = {};
            prune_locked(job->finished_at_ms);
        }
    }
}

void JobManager::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        const int64_t finished = now_ms();
        for (const auto &job : pending_) {
            job->cancellation_requested.store(true);
            job->status = JobStatus::cancelled;
            job->progress = 1.0F;
            job->stage = "cancelled";
            job->finished_at_ms = finished;
            job->work = {};
        }
        pending_.clear();
        for (const auto &item : jobs_) {
            if (item.second->status == JobStatus::running) {
                item.second->cancellation_requested.store(true);
            }
        }
    }
    ready_.notify_all();
    for (std::thread &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

} // namespace imagecpp::server
