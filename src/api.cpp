// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/net.hpp"
#include "yolo/api.hpp"
#include <chrono>
#include <mutex>
#include <thread>
namespace yolo {
using json=nlohmann::json;
json api_snapshot(const DashboardView &v) {
  json devices=json::array();
  for (auto &d:v.devices) {
    json item={{"label",d.label},{"name",d.name},{"pci_bus",d.pci_bus},
      {"hashrate",d.hashes_per_second},{"hashes",d.hashes},{"accepted",d.accepted},
      {"rejected",d.rejected},{"stale",d.stale}};
    item["temperature"]=d.sensors.temperature ? json(*d.sensors.temperature):json(nullptr);
    item["fan"]=d.sensors.fan_percent ? json(*d.sensors.fan_percent):json(nullptr);
    devices.push_back(item);
  }
  return {{"version","0.2.0"},{"state",v.state},{"coin",v.coin},{"uptime",v.elapsed},
    {"hashes",v.hashes},{"accepted",v.accepted},{"rejected",v.rejected},{"stale",v.stale},
    {"pending",v.pending},{"block_candidates",v.block_candidates},{"devices",devices}};
}
struct ApiServer::Impl {
  net::Socket listener=net::invalid;
  std::mutex mutex;
  json snapshot=api_snapshot(DashboardView{});
  std::jthread thread;
  explicit Impl(unsigned port) {
    if (!port) return;
    if (port>65535) throw std::runtime_error("API port out of range");
    listener=net::socket(AF_INET);
    if (listener==net::invalid) throw std::runtime_error("API socket failed");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (::bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || ::listen(listener,8)) {
      net::close(listener);listener=net::invalid;
      throw std::runtime_error("API cannot bind 127.0.0.1:"+std::to_string(port));
    }
    try { thread=std::jthread([this](std::stop_token stop){
      while (!stop.stop_requested()) {
        net::Poll p{listener,POLLIN,0};
        if(net::poll(&p,1,100)<=0) continue;
        auto client=::accept(listener,nullptr,nullptr);
        if(client==net::invalid) continue;
        if (!net::nonblock(client)) {net::close(client);continue;}
        // One bounded, read-only request; slow clients cannot block mining or shutdown.
        std::string request;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while (!stop.stop_requested() && request.size()<4096 &&
               std::chrono::steady_clock::now()<deadline && request.find("\r\n\r\n")==std::string::npos) {
          net::Poll cp{client,POLLIN,0};if(net::poll(&cp,1,50)<=0) continue;
          char b[1024];int n=::recv(client,b,sizeof(b),0);if(n<=0) break;request.append(b,n);
        }
        std::string body="{\"error\":\"not found\"}",status="404 Not Found";
        if (request.find("\r\n\r\n")!=std::string::npos &&
            (request.starts_with("GET /summary HTTP/1.") || request.starts_with("GET / HTTP/1."))) {
          std::lock_guard lock(mutex);body=snapshot.dump();status="200 OK";
        }
        std::string reply="HTTP/1.1 "+status+"\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
        size_t off=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!stop.stop_requested() && off<reply.size() && std::chrono::steady_clock::now()<deadline) {
          int n=::send(client,reply.data()+off,static_cast<int>(reply.size()-off),net::send_flags);
          if(n>0) off+=n;
          else if(net::again() || net::interrupted()) {net::Poll cp{client,POLLOUT,0};net::poll(&cp,1,50);}
          else break;
        }
        net::close(client);
      }
    });} catch(...) {net::close(listener);throw;}
  }
  ~Impl(){thread.request_stop();if(thread.joinable())thread.join();if(listener!=net::invalid)net::close(listener);}
};
ApiServer::ApiServer(unsigned port):impl(std::make_unique<Impl>(port)){}
ApiServer::~ApiServer()=default;
void ApiServer::update(const DashboardView &v){std::lock_guard lock(impl->mutex);impl->snapshot=api_snapshot(v);}
}
