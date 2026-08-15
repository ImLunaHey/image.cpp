#include "server/job_manager.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::optional<imagecpp::server::JobSnapshot> wait_for_terminal(imagecpp::server::JobManager &manager,
                                                               const std::string &id) {
    for (size_t attempt = 0; attempt < 400; ++attempt) {
        std::optional<imagecpp::server::JobSnapshot> snapshot = manager.get(id);
        if (!snapshot || snapshot->status == imagecpp::server::JobStatus::completed ||
            snapshot->status == imagecpp::server::JobStatus::failed ||
            snapshot->status == imagecpp::server::JobStatus::cancelled) {
            return snapshot;
        }
        std::this_thread::sleep_for(5ms);
    }
    return std::nullopt;
}

bool test_queue_and_cancellation() {
    imagecpp::server::JobManagerConfig config;
    config.worker_count = 1;
    config.max_queued_jobs = 1;
    config.max_retained_jobs = 4;
    config.retention_time = 60s;
    imagecpp::server::JobManager manager(config);

    std::promise<void> started;
    std::promise<void> release;
    const std::shared_future<void> released = release.get_future().share();
    const imagecpp::server::JobSnapshot running =
        manager.submit("blocking", [&started, released](const imagecpp::server::JobContext &context) {
            context.report(0.5F, "halfway");
            started.set_value();
            released.wait();
            return imagecpp::server::JobOutput{200, {}, "done"};
        });
    started.get_future().wait();

    const std::optional<imagecpp::server::JobSnapshot> halfway = manager.get(running.id);
    if (!halfway || halfway->status != imagecpp::server::JobStatus::running || halfway->progress != 0.5F ||
        halfway->stage != "halfway") {
        return false;
    }

    const imagecpp::server::JobSnapshot queued =
        manager.submit("queued", [](const imagecpp::server::JobContext &) { return imagecpp::server::JobOutput{}; });
    bool queue_full = false;
    try {
        (void)manager.submit("overflow",
                             [](const imagecpp::server::JobContext &) { return imagecpp::server::JobOutput{}; });
    } catch (const imagecpp::server::JobQueueFull &) {
        queue_full = true;
    }
    const std::optional<imagecpp::server::JobSnapshot> cancelled_queued = manager.cancel(queued.id);
    const std::optional<imagecpp::server::JobSnapshot> cancellation_requested = manager.cancel(running.id);
    release.set_value();
    const std::optional<imagecpp::server::JobSnapshot> cancelled_running = wait_for_terminal(manager, running.id);

    return queue_full && cancelled_queued && cancelled_queued->status == imagecpp::server::JobStatus::cancelled &&
           cancellation_requested && cancellation_requested->stage == "cancellation_requested" && cancelled_running &&
           cancelled_running->status == imagecpp::server::JobStatus::cancelled && !manager.output(running.id);
}

bool test_results_and_failures() {
    imagecpp::server::JobManager manager({1, 4, 4, 60s});
    const imagecpp::server::JobSnapshot success =
        manager.submit("success", [](const imagecpp::server::JobContext &context) {
            context.report(0.8F, "encoding");
            return imagecpp::server::JobOutput{201, {{"Content-Type", "text/plain"}}, "created"};
        });
    const std::optional<imagecpp::server::JobSnapshot> completed = wait_for_terminal(manager, success.id);
    const std::optional<imagecpp::server::JobOutput> output = manager.output(success.id);
    if (!completed || completed->status != imagecpp::server::JobStatus::completed || !output || output->status != 201 ||
        output->body != "created") {
        return false;
    }

    const imagecpp::server::JobSnapshot failure = manager.submit("failure", [](const imagecpp::server::JobContext &) {
        return imagecpp::server::JobOutput{422, {{"Content-Type", "application/json"}}, "error"};
    });
    const std::optional<imagecpp::server::JobSnapshot> failed = wait_for_terminal(manager, failure.id);
    const imagecpp::server::JobManagerInfo info = manager.info();
    return failed && failed->status == imagecpp::server::JobStatus::failed && failed->http_status == 422 &&
           manager.output(failure.id).has_value() && info.worker_count == 1 && info.retained_jobs == 2;
}

} // namespace

int main() {
    if (!test_queue_and_cancellation()) {
        std::cerr << "job queue/cancellation test failed\n";
        return 1;
    }
    if (!test_results_and_failures()) {
        std::cerr << "job result/failure test failed\n";
        return 1;
    }
    return 0;
}
