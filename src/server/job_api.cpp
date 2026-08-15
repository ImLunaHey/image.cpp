#include "server/job_api.hpp"

#include "httplib.h"
#include "server/http_common.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace imagecpp::server {
namespace {

using Json = detail::Json;

Json snapshot_json(const JobSnapshot &snapshot) {
    Json queue_position = nullptr;
    if (snapshot.queue_position >= 0) {
        queue_position = snapshot.queue_position;
    }
    Json result = {{"id", snapshot.id},
                   {"operation", snapshot.operation},
                   {"status", job_status_name(snapshot.status)},
                   {"progress", snapshot.progress},
                   {"stage", snapshot.stage},
                   {"http_status", snapshot.http_status == 0 ? Json(nullptr) : Json(snapshot.http_status)},
                   {"created_at_ms", snapshot.created_at_ms},
                   {"started_at_ms", snapshot.started_at_ms == 0 ? Json(nullptr) : Json(snapshot.started_at_ms)},
                   {"finished_at_ms", snapshot.finished_at_ms == 0 ? Json(nullptr) : Json(snapshot.finished_at_ms)},
                   {"queue_position", std::move(queue_position)},
                   {"status_url", "/v1/jobs/" + snapshot.id},
                   {"result_url", "/v1/jobs/" + snapshot.id + "/result"}};
    if (!snapshot.error.empty()) {
        result["error"] = snapshot.error;
    }
    return result;
}

Json manager_info_json(const JobManagerInfo &info) {
    return {{"worker_count", info.worker_count},
            {"max_queued_jobs", info.max_queued_jobs},
            {"max_retained_jobs", info.max_retained_jobs},
            {"retention_seconds", info.retention_seconds},
            {"queued_jobs", info.queued_jobs},
            {"running_jobs", info.running_jobs},
            {"retained_jobs", info.retained_jobs}};
}

bool true_value(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value == "1" || value == "true" || value == "yes";
}

bool wants_async(const httplib::Request &request) {
    if (request.get_header_value("Prefer").find("respond-async") != std::string::npos) {
        return true;
    }
    if (request.has_param("async")) {
        return true_value(request.get_param_value("async"));
    }
    return request.is_multipart_form_data() && request.form.has_field("async") &&
           true_value(request.form.get_field("async"));
}

bool wants_stream(const httplib::Request &request) {
    if (request.get_header_value("Accept").find("text/event-stream") != std::string::npos) {
        return true;
    }
    if (request.has_param("stream") && true_value(request.get_param_value("stream"))) {
        return true;
    }
    return request.is_multipart_form_data() && request.form.has_field("stream") &&
           true_value(request.form.get_field("stream"));
}

std::shared_ptr<httplib::Request> stored_request(const httplib::Request &request) {
    auto stored = std::make_shared<httplib::Request>();
    stored->method = request.method;
    stored->path = request.path;
    stored->matched_route = request.matched_route;
    stored->params = request.params;
    stored->headers = request.headers;
    stored->body = request.body;
    stored->version = request.version;
    stored->target = request.target;
    stored->form = request.form;
    stored->path_params = request.path_params;
    stored->is_connection_closed = [] { return false; };
    return stored;
}

bool hop_by_hop_header(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return name == "connection" || name == "content-length" || name == "keep-alive" || name == "proxy-authenticate" ||
           name == "proxy-authorization" || name == "te" || name == "trailer" || name == "transfer-encoding" ||
           name == "upgrade";
}

} // namespace

JobApi::JobApi(JobManagerConfig config) : manager_(config) {}
JobApi::~JobApi() = default;

void JobApi::dispatch(const std::string &operation, const httplib::Request &request, httplib::Response &response,
                      Handler handler) {
    if (!wants_async(request)) {
        handler(request, response);
        return;
    }
    if (wants_stream(request)) {
        detail::set_error(response, 400, "async_stream_unsupported",
                          "streaming responses cannot be submitted as background jobs");
        return;
    }

    const std::shared_ptr<httplib::Request> stored = stored_request(request);
    try {
        const JobSnapshot snapshot =
            manager_.submit(operation, [stored, handler = std::move(handler)](const JobContext &context) {
                if (context.cancellation_requested()) {
                    return JobOutput{};
                }
                context.report(0.1F, "processing");
                httplib::Response generated;
                handler(*stored, generated);
                context.report(0.95F, "finalizing");
                JobOutput output;
                output.status = generated.status < 0 ? 200 : generated.status;
                output.body = std::move(generated.body);
                output.headers.reserve(generated.headers.size());
                for (const auto &header : generated.headers) {
                    if (!hop_by_hop_header(header.first)) {
                        output.headers.emplace_back(header.first, header.second);
                    }
                }
                return output;
            });
        response.status = 202;
        response.set_header("Location", "/v1/jobs/" + snapshot.id);
        response.set_header("Preference-Applied", "respond-async");
        response.set_header("Retry-After", "1");
        response.set_content(snapshot_json(snapshot).dump(), "application/json; charset=utf-8");
    } catch (const JobQueueFull &error) {
        response.set_header("Retry-After", "1");
        detail::set_error(response, 429, "job_queue_full", error.what());
    }
}

void JobApi::register_routes(httplib::Server &server) {
    server.Get("/v1/jobs", [this](const httplib::Request &request, httplib::Response &response) {
        size_t limit = 50;
        try {
            if (request.has_param("limit")) {
                limit = detail::parse_uint32(request.get_param_value("limit"), "limit");
                limit = std::min<size_t>(limit, 100);
            }
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
            return;
        }
        Json jobs = Json::array();
        for (const JobSnapshot &snapshot : manager_.list(limit)) {
            jobs.push_back(snapshot_json(snapshot));
        }
        detail::set_json(response, 200, {{"jobs", std::move(jobs)}, {"queue", manager_info_json(manager_.info())}});
    });
    server.Get(R"(/v1/jobs/([^/]+)/result)", [this](const httplib::Request &request, httplib::Response &response) {
        const std::string id = request.matches[1];
        const std::optional<JobSnapshot> snapshot = manager_.get(id);
        if (!snapshot) {
            detail::set_error(response, 404, "job_not_found", "job was not found or has expired");
            return;
        }
        if (snapshot->status == JobStatus::queued || snapshot->status == JobStatus::running) {
            response.set_header("Retry-After", "1");
            detail::set_error(response, 409, "job_not_ready", "job result is not ready");
            return;
        }
        if (snapshot->status == JobStatus::cancelled) {
            detail::set_error(response, 410, "job_cancelled", "job was cancelled");
            return;
        }
        const std::optional<JobOutput> output = manager_.output(id);
        if (!output) {
            detail::set_error(response, 500, "job_failed",
                              snapshot->error.empty() ? "job failed without a result" : snapshot->error);
            return;
        }
        response.status = output->status;
        response.body = output->body;
        for (const auto &header : output->headers) {
            response.headers.emplace(header.first, header.second);
        }
    });
    server.Get(R"(/v1/jobs/([^/]+))", [this](const httplib::Request &request, httplib::Response &response) {
        const std::optional<JobSnapshot> snapshot = manager_.get(request.matches[1]);
        if (!snapshot) {
            detail::set_error(response, 404, "job_not_found", "job was not found or has expired");
            return;
        }
        detail::set_json(response, 200, snapshot_json(*snapshot));
    });
    server.Delete(R"(/v1/jobs/([^/]+))", [this](const httplib::Request &request, httplib::Response &response) {
        const std::optional<JobSnapshot> snapshot = manager_.cancel(request.matches[1]);
        if (!snapshot) {
            detail::set_error(response, 404, "job_not_found", "job was not found or has expired");
            return;
        }
        detail::set_json(response, 200, snapshot_json(*snapshot));
    });
}

JobManagerInfo JobApi::info() { return manager_.info(); }

void JobApi::shutdown() noexcept { manager_.shutdown(); }

} // namespace imagecpp::server
