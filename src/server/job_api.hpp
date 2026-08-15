#ifndef IMAGECPP_SERVER_JOB_API_HPP
#define IMAGECPP_SERVER_JOB_API_HPP

#include "server/job_manager.hpp"

#include <functional>
#include <string>

namespace httplib {
struct Request;
struct Response;
class Server;
} // namespace httplib

namespace imagecpp::server {

class JobApi final {
  public:
    using Handler = std::function<void(const httplib::Request &, httplib::Response &)>;

    explicit JobApi(JobManagerConfig config);
    ~JobApi();

    JobApi(const JobApi &) = delete;
    JobApi &operator=(const JobApi &) = delete;

    void register_routes(httplib::Server &server);
    void dispatch(const std::string &operation, const httplib::Request &request, httplib::Response &response,
                  Handler handler);
    JobManagerInfo info();
    void shutdown() noexcept;

  private:
    JobManager manager_;
};

} // namespace imagecpp::server

#endif
