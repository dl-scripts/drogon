/**
 *
 *  @file ListenerManager.h
 *  @author An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */

#pragma once

#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/callbacks.h>
#include <trantor/utils/NonCopyable.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "impl_forwards.h"

namespace trantor
{
class InetAddress;
}

namespace drogon
{
class ListenerManager : public trantor::NonCopyable
{
  public:
    ~ListenerManager() = default;
    void addListener(const std::string &ip,
                     uint16_t port,
                     bool useSSL = false,
                     const std::string &certFile = "",
                     const std::string &keyFile = "",
                     bool useOldTLS = false,
                     const std::vector<std::pair<std::string, std::string>>
                         &sslConfCmds = {});
    std::vector<trantor::InetAddress> getListeners() const;
    void createListeners(
        const std::string &globalCertFile,
        const std::string &globalKeyFile,
        const std::vector<std::pair<std::string, std::string>> &sslConfCmds,
        const std::vector<trantor::EventLoop *> &ioLoops);
    void startListening();
    void stopListening();

    void setBeforeListenSockOptCallback(std::function<void(int)> cb)
    {
        beforeListenSetSockOptCallback_ = std::move(cb);
    }

    void setAfterAcceptSockOptCallback(std::function<void(int)> cb)
    {
        afterAcceptSetSockOptCallback_ = std::move(cb);
    }

    void setConnectionCallback(
        std::function<void(const trantor::TcpConnectionPtr &)> cb)
    {
        connectionCallback_ = std::move(cb);
    }

    void reloadSSLFiles();

  private:
    struct ListenerInfo
    {
        ListenerInfo(
            std::string ip,
            uint16_t port,
            bool useSSL,
            std::string certFile,
            std::string keyFile,
            bool useOldTLS,
            std::vector<std::pair<std::string, std::string>> sslConfCmds)
            : ip_(std::move(ip)),
              port_(port),
              useSSL_(useSSL),
              certFile_(std::move(certFile)),
              keyFile_(std::move(keyFile)),
              useOldTLS_(useOldTLS),
              sslConfCmds_(std::move(sslConfCmds))
        {
        }

        std::string ip_;
        uint16_t port_;
        bool useSSL_;
        std::string certFile_;
        std::string keyFile_;
        bool useOldTLS_;
        std::vector<std::pair<std::string, std::string>> sslConfCmds_;
    };

    std::vector<ListenerInfo> listeners_;
    std::vector<std::shared_ptr<HttpServer>> servers_;

    // should have value when and only when on OS that one port can only be
    // listened by one thread
    std::unique_ptr<trantor::EventLoopThread> listeningThread_;
    std::function<void(int)> beforeListenSetSockOptCallback_;
    std::function<void(int)> afterAcceptSetSockOptCallback_;
    std::function<void(const trantor::TcpConnectionPtr &)> connectionCallback_;
};

}  // namespace drogon
