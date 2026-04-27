#include "timeshiftx/network_service.hpp"
#include "timeshiftx/qt_network_plugin.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdlib>
#include <deque>
#include <optional>
#include <string>

using namespace timeshiftx;

namespace {

struct MockReply {
    int status {200};
    QByteArray body;
    int delay_ms {0};
    bool never_respond {false};
};

class MockHttpServer {
public:
    MockHttpServer() {
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this]() {
            while (server_.hasPendingConnections()) {
                QTcpSocket* socket = server_.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    socket->readAll();
                    if (replies_.empty()) {
                        socket->disconnectFromHost();
                        return;
                    }

                    MockReply reply = replies_.front();
                    replies_.pop_front();
                    if (reply.never_respond) {
                        return;
                    }

                    QTimer::singleShot(reply.delay_ms, socket, [socket, reply]() {
                        const QByteArray text = QByteArray("HTTP/1.1 ") + QByteArray::number(reply.status) + " OK\r\n" +
                                                "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n" +
                                                "Connection: close\r\n\r\n" + reply.body;
                        socket->write(text);
                        socket->flush();
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }

    bool listen() {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(server_.serverPort()) + "/demo";
    }

    void enqueue(const MockReply& reply) {
        replies_.push_back(reply);
    }

    QTcpServer& raw() { return server_; }

private:
    QTcpServer server_;
    std::deque<MockReply> replies_;
};

NetworkResponse runAsync(INetworkPlugin& plugin, const NetworkRequest& req, int timeout_ms = 6000) {
    QEventLoop loop;
    std::optional<NetworkResponse> out;

    NetworkService::requestAsync(req, [&](NetworkResponse resp) {
        out = std::move(resp);
        loop.quit();
    }, {}, &plugin);

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(timeout_ms);

    loop.exec();
    if (!out.has_value()) {
        return {Error{ErrorCode::ERR_INTERNAL, "async callback timeout"}, 0, {}, {}, 0};
    }
    return out.value();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    MockHttpServer server;
    if (!server.listen()) return EXIT_FAILURE;
    QSignalSpy conn_spy(&server.raw(), &QTcpServer::newConnection);

    auto plugin = createQtNetworkPlugin();
    if (!plugin) return EXIT_FAILURE;

    NetworkRequest req;
    req.url = server.url();
    req.timeout_seconds = 1;
    req.max_retries = 2;
    req.retry_delay_ms = 50;

    // success
    server.enqueue({200, "ok", 0, false});
    auto success = runAsync(*plugin, req);
    if (!success.error.ok() || success.body != "ok") return EXIT_FAILURE;
    if (conn_spy.count() < 1) return EXIT_FAILURE;

    // 404
    server.enqueue({404, "notfound", 0, false});
    auto not_found = runAsync(*plugin, req);
    if (not_found.error.code != ErrorCode::ERR_CATCHUP_EXPIRED) return EXIT_FAILURE;

    // timeout + retry success
    server.enqueue({200, "", 0, true});
    server.enqueue({200, "retry-ok", 0, false});
    auto retry_success = runAsync(*plugin, req);
    if (!retry_success.error.ok() || retry_success.body != "retry-ok") return EXIT_FAILURE;

    // timeout + retry fail
    server.enqueue({200, "", 0, true});
    server.enqueue({200, "", 0, true});
    auto retry_fail = runAsync(*plugin, req);
    if (retry_fail.error.code == ErrorCode::OK) return EXIT_FAILURE;

    // head
    NetworkRequest head_req = req;
    head_req.method = NetworkMethod::HEAD;
    server.enqueue({200, "", 0, false});
    auto head_ok = runAsync(*plugin, head_req);
    if (!head_ok.error.ok()) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
