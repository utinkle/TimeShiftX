#include "timeshiftx/qt_network_plugin.hpp"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <future>
#include <memory>

namespace timeshiftx {
namespace {

ErrorCode mapHttpStatusToError(long http_code) {
    if (http_code >= 200 && http_code < 300) {
        return ErrorCode::OK;
    }
    if (http_code == 404) {
        return ErrorCode::ERR_CATCHUP_EXPIRED;
    }
    return ErrorCode::ERR_NETWORK_HTTP_STATUS;
}

class QtNetworkPlugin final : public INetworkPlugin {
public:
    QtNetworkPlugin() {
        context_.moveToThread(&worker_thread_);
        worker_thread_.start();

        QMetaObject::invokeMethod(
            &context_,
            [this]() {
                manager_ = std::make_unique<QNetworkAccessManager>();
            },
            Qt::BlockingQueuedConnection);
    }

    ~QtNetworkPlugin() override {
        QMetaObject::invokeMethod(
            &context_,
            [this]() {
                manager_.reset();
            },
            Qt::BlockingQueuedConnection);

        worker_thread_.quit();
        worker_thread_.wait();
    }

    std::string name() const override {
        return "qt";
    }

    NetworkResponse perform(const NetworkRequest& request) override {
        auto promise = std::make_shared<std::promise<NetworkResponse>>();
        std::future<NetworkResponse> fut = promise->get_future();

        performAsync(request, [promise](NetworkResponse resp) {
            promise->set_value(std::move(resp));
        });

        return fut.get();
    }

    void performAsync(const NetworkRequest& request, NetworkCallback callback) override {
        auto shared_cb = std::make_shared<NetworkCallback>(std::move(callback));

        QMetaObject::invokeMethod(
            &context_,
            [this, request, shared_cb]() {
                executeAttempt(request, shared_cb);
            },
            Qt::QueuedConnection);
    }

private:
    void executeAttempt(const NetworkRequest& request, const std::shared_ptr<NetworkCallback>& callback) {
        if (!manager_) {
            (*callback)({Error{ErrorCode::ERR_INTERNAL, "Qt network manager not initialized"}, 0, {}, {}, 0});
            return;
        }

        if (request.url.empty()) {
            (*callback)({Error{ErrorCode::ERR_INVALID_ARGUMENT, "HTTP URL is empty"}, 0, {}, {}, 0});
            return;
        }

        const auto begin = std::chrono::steady_clock::now();

        QNetworkRequest qreq(QUrl(QString::fromStdString(request.url)));
        qreq.setTransferTimeout(static_cast<int>(request.timeout_seconds * 1000));
        for (const auto& kv : request.headers) {
            qreq.setRawHeader(QByteArray::fromStdString(kv.first), QByteArray::fromStdString(kv.second));
        }
        qreq.setRawHeader("User-Agent", QByteArray::fromStdString(request.user_agent));

        QNetworkReply* reply = nullptr;
        if (request.method == NetworkMethod::HEAD) {
            reply = manager_->head(qreq);
        } else {
            reply = manager_->get(qreq);
        }

        if (!request.strict_ssl) {
            QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>&) {
                reply->ignoreSslErrors();
            });
        }

        QTimer* timeout_timer = new QTimer(reply);
        timeout_timer->setSingleShot(true);
        QObject::connect(timeout_timer, &QTimer::timeout, reply, [reply]() {
            reply->abort();
        });
        timeout_timer->start(static_cast<int>(request.timeout_seconds * 1000));

        QObject::connect(reply, &QNetworkReply::finished, &context_, [callback, begin, reply]() {
            const auto end = std::chrono::steady_clock::now();
            const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

            const long http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toLongLong();
            NetworkResponse resp;
            resp.http_status = http_code;
            resp.latency_ms = latency;

            if (reply->error() != QNetworkReply::NoError) {
                resp.error = {ErrorCode::ERR_NETWORK_TIMEOUT, reply->errorString().toStdString()};
            } else {
                resp.body = QString::fromUtf8(reply->readAll()).toStdString();
                const ErrorCode mapped = mapHttpStatusToError(http_code);
                if (mapped == ErrorCode::OK) {
                    resp.error = {ErrorCode::OK, "HTTP request successful"};
                } else {
                    resp.error = {mapped, std::string("HTTP status code abnormal: ") + std::to_string(http_code)};
                }
            }

            reply->deleteLater();
            (*callback)(std::move(resp));
        });
    }

private:
    QThread worker_thread_;
    QObject context_;
    std::unique_ptr<QNetworkAccessManager> manager_;
};

} // namespace

std::shared_ptr<INetworkPlugin> createQtNetworkPlugin() {
    return std::make_shared<QtNetworkPlugin>();
}

} // namespace timeshiftx
