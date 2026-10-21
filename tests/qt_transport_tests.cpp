#include "qt_http_transport.hpp"
#include "storyforge/agent/connection_check.hpp"

#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Qt transport sends BYOK requests to a real loopback HTTP server") {
    int argc = 1;
    char name[] = "qt_transport_tests";
    char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, server.serverPort());
    INFO("loopback probe: " << probe.errorString().toStdString());
    REQUIRE(probe.waitForConnected(1000));
    probe.disconnectFromHost();
    QByteArray received;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            received += socket->readAll();
            if (!received.contains("\r\n\r\n")) return;
            const QByteArray body = R"({"choices":[{"message":{"content":"ready"}}]})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    talestate_desktop::QtHttpTransport transport;
    storyforge::agent::HttpRequest request;
    request.method = "POST";
    request.url = "http://127.0.0.1:" + std::to_string(server.serverPort()) + "/v1/chat/completions";
    request.headers = {{"Authorization", "Bearer local-test-key"}, {"Content-Type", "application/json"}};
    request.body = R"({"model":"test"})";
    request.timeout = std::chrono::milliseconds{3000};
    const auto result = transport.send(request);
    const auto* failure = std::get_if<storyforge::agent::TransportError>(&result);
    INFO("transport error: " << (failure ? failure->message : "none") <<
         "; received: " << received.toStdString());
    REQUIRE(std::holds_alternative<storyforge::agent::HttpResponse>(result));
    CHECK(std::get<storyforge::agent::HttpResponse>(result).status == 200);
    CHECK(std::get<storyforge::agent::HttpResponse>(result).body.find("ready") != std::string::npos);
    CHECK(received.contains("Authorization: Bearer local-test-key"));
    CHECK(received.contains("POST /v1/chat/completions HTTP/1.1"));
}

TEST_CASE("connection check reports success and authentication failure through Qt loopback") {
    int argc = 1;
    char name[] = "qt_transport_tests";
    char* argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    struct Scenario {
        int status;
        QByteArray body;
        storyforge::agent::ConnectionCheckCode expected;
    };
    const std::vector<Scenario> scenarios{
        {200, R"({"choices":[{"message":{"content":"OK"}}]})",
         storyforge::agent::ConnectionCheckCode::connected},
        {401, R"({"error":{"message":"bad local-test-secret"}})",
         storyforge::agent::ConnectionCheckCode::authentication_failure},
    };
    for (const auto& scenario : scenarios) {
        QTcpServer server;
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        QByteArray received;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                received += socket->readAll();
                if (!received.contains("\r\n\r\n")) return;
                const auto label = scenario.status == 200 ? "OK" : "Unauthorized";
                socket->write("HTTP/1.1 " + QByteArray::number(scenario.status) + " " + label +
                              "\r\nContent-Type: application/json\r\nContent-Length: " +
                              QByteArray::number(scenario.body.size()) +
                              "\r\nConnection: close\r\n\r\n" + scenario.body);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
        storyforge::agent::ProviderConfig config;
        config.base_url = "http://127.0.0.1:" + std::to_string(server.serverPort()) + "/v1";
        config.api_key = "local-test-secret";
        config.model = "loopback-model";
        config.timeout = std::chrono::milliseconds{3000};
        storyforge::agent::OpenAiCompatibleProvider provider{
            config, std::make_shared<talestate_desktop::QtHttpTransport>()};
        const auto result = storyforge::agent::check_connection(provider);
        CHECK(result.code == scenario.expected);
        CHECK(result.message.find(config.api_key) == std::string::npos);
        CHECK(received.contains("Authorization: Bearer local-test-secret"));
        CHECK(received.contains("\"model\":\"loopback-model\""));
        CHECK(received.contains("POST /v1/chat/completions HTTP/1.1"));
    }
}
