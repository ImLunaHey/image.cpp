#include "server/http_server.hpp"

#include "httplib.h"

#include <string>
#include <thread>

int main() {
    imagecpp::server::HttpServerConfig config;
    config.port = 0;
    imagecpp::server::HttpServer server(config);
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    const httplib::Result health = client.Get("/healthz");
    const httplib::Result operations = client.Get("/v1/operations");
    const httplib::Result unavailable = client.Post("/v1/caption", "image", "application/octet-stream");
    const httplib::Result missing = client.Get("/does-not-exist");

    const bool passed = health && health->status == 200 && health->body.find("\"status\":\"ok\"") != std::string::npos &&
                        health->body.find("\"vlm_loaded\":false") != std::string::npos && operations &&
                        operations->status == 200 && operations->body.find("\"operations\"") != std::string::npos &&
                        unavailable && unavailable->status == 503 &&
                        unavailable->body.find("model_not_loaded") != std::string::npos && missing &&
                        missing->status == 404 && missing->body.find("not_found") != std::string::npos;

    server.stop();
    listener.join();
    return passed ? 0 : 1;
}
