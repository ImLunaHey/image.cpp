#ifndef IMAGECPP_SERVER_OPERATION_API_HPP
#define IMAGECPP_SERVER_OPERATION_API_HPP

#include "imagecpp/imagecpp.hpp"
#include "server/http_server.hpp"

#include <memory>
#include <mutex>

namespace httplib {
class Server;
}

namespace imagecpp::server {

class OperationApi final {
  public:
    OperationApi(imagecpp::Runtime &runtime, const HttpServerConfig &config, std::mutex &model_mutex);
    ~OperationApi();

    OperationApi(const OperationApi &) = delete;
    OperationApi &operator=(const OperationApi &) = delete;

    void register_routes(httplib::Server &server);

  private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace imagecpp::server

#endif
