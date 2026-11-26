#include "qt_http_transport.hpp"

#include <algorithm>
#include <limits>

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace talestate_desktop {

storyforge::agent::HttpResult QtHttpTransport::send(
    const storyforge::agent::HttpRequest& request) {
    if (request.method != "POST")
        return storyforge::agent::TransportError{"only POST is supported", false};
    const QUrl url(QString::fromStdString(request.url));
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http"))
        return storyforge::agent::TransportError{"invalid provider URL", false};
    QNetworkRequest outgoing(url);
    outgoing.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::ManualRedirectPolicy);
    const auto timeout = std::clamp<std::int64_t>(request.timeout.count(), 1,
        std::numeric_limits<int>::max());
    outgoing.setTransferTimeout(static_cast<int>(timeout));
    for (const auto& [key, value] : request.headers)
        outgoing.setRawHeader(QByteArray::fromStdString(key), QByteArray::fromStdString(value));

    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.post(outgoing, QByteArray::fromStdString(request.body));
    QEventLoop loop;
    QByteArray body;
    bool oversized = false;
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
        body += reply->readAll();
        if (static_cast<std::size_t>(body.size()) > request.max_response_bytes) {
            oversized = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished()) loop.exec();
    body += reply->readAll();
    if (oversized || static_cast<std::size_t>(body.size()) > request.max_response_bytes)
        return storyforge::agent::TransportError{"provider response exceeds size limit", false};
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
        // Do not echo a URL or arbitrary network diagnostic: some providers embed credentials.
        return storyforge::agent::TransportError{"network or TLS connection failed", true};
    }
    storyforge::agent::HttpResponse response;
    response.status = status;
    response.body = body.toStdString();
    for (const auto& header : reply->rawHeaderPairs())
        response.headers.emplace(header.first.toStdString(), header.second.toStdString());
    return response;
}

} // namespace talestate_desktop
