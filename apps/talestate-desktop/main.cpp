#include "qt_http_transport.hpp"

#include "storyforge/agent/connection_check.hpp"
#include "storyforge/agent/story_agent.hpp"
#include "storyforge/io/story_loader.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <QApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString safe(const std::string& value) {
    return QString::fromStdString(value).toHtmlEscaped();
}

QString state_summary(const storyforge::StoryPackage& package,
                      const storyforge::StoryState& state) {
    QString result;
    if (const auto* scene = storyforge::current_scene(package, state))
        result += "📍 " + QString::fromStdString(scene->title) + "\n";
    result += "回合 " + QString::number(state.turn) + "　金币 " + QString::number(state.gold) + "\n\n";
    result += "背包\n";
    if (state.inventory.empty()) result += "  空\n";
    for (const auto& [item, quantity] : state.inventory)
        result += "  " + QString::fromStdString(item.value()) + " ×" + QString::number(quantity) + "\n";
    result += "\n世界变量\n";
    for (const auto& [id, value] : state.variables) {
        const QString text = std::visit([](const auto& value) -> QString {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return QString::fromStdString(value);
            else return QString::number(value);
        }, value);
        result += "  " + QString::fromStdString(id.value()) + ": " + text + "\n";
    }
    result += "\n人物关系\n";
    if (state.npc_relationships.empty()) result += "  暂无\n";
    for (const auto& [npc, score] : state.npc_relationships)
        result += "  " + QString::fromStdString(npc.value()) + ": " + QString::number(score) + "\n";
    result += "\n人物记忆\n";
    if (state.npc_memory.empty()) result += "  暂无\n";
    for (const auto& [npc, memory] : state.npc_memory)
        for (const auto& [key, value] : memory) {
            const QString text = std::visit([](const auto& value) -> QString {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::string>) return QString::fromStdString(value);
                else return QString::number(value);
            }, value);
            result += "  " + QString::fromStdString(npc.value() + "." + key) + ": " + text + "\n";
        }
    result += "\n已发生事件\n";
    if (state.completed_events.empty()) result += "  暂无\n";
    for (const auto& event : state.completed_events)
        result += "  " + QString::fromStdString(event.value()) + "\n";
    return result;
}

class MainWindow final : public QWidget {
public:
    MainWindow() {
        setWindowTitle("TaleState C++ · 文字 RPG Agent");
        resize(1180, 760);
        setStyleSheet(R"(
            QWidget { background: #f5f1e9; color: #2f2d2a; font-size: 14px; }
            QGroupBox { background: #fffdf8; border: 1px solid #d8d0c2; border-radius: 12px;
                        margin-top: 12px; padding: 14px; font-weight: 600; }
            QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; }
            QLineEdit, QTextEdit, QTextBrowser { background: white; border: 1px solid #d8d0c2;
                        border-radius: 8px; padding: 8px; selection-background-color: #996e48; }
            QPushButton { background: #554436; color: white; border: 0; border-radius: 8px;
                          padding: 10px 15px; font-weight: 600; }
            QPushButton:hover { background: #75563d; }
            QPushButton:disabled { background: #bcb5ac; }
        )");

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 20);
        root->setSpacing(14);
        auto* heading = new QLabel("TaleState C++  ·  把你的世界交给玩家");
        heading->setStyleSheet("font-size: 23px; font-weight: 700; color: #493723;");
        root->addWidget(heading);

        auto* splitter = new QSplitter(Qt::Horizontal);
        root->addWidget(splitter, 1);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 8, 0);
        splitter->addWidget(left);

        auto* world_group = new QGroupBox("世界包");
        auto* world_layout = new QVBoxLayout(world_group);
        world_label_ = new QLabel("尚未导入世界包");
        world_label_->setWordWrap(true);
        import_button_ = new QPushButton("导入世界包 story.json");
        world_layout->addWidget(world_label_);
        world_layout->addWidget(import_button_);
        left_layout->addWidget(world_group);
        connect(import_button_, &QPushButton::clicked, this, [this] { import_world(); });

        auto* provider_group = new QGroupBox("你的模型 · BYOK");
        auto* form = new QFormLayout(provider_group);
        base_url_ = new QLineEdit;
        base_url_->setPlaceholderText("https://你的服务地址/v1");
        api_key_ = new QLineEdit;
        api_key_->setEchoMode(QLineEdit::Password);
        api_key_->setPlaceholderText("仅在本次运行的内存中使用");
        model_ = new QLineEdit;
        model_->setPlaceholderText("模型名称");
        form->addRow("API 地址", base_url_);
        form->addRow("API Key", api_key_);
        form->addRow("模型", model_);
        test_connection_button_ = new QPushButton("测试连接");
        test_connection_button_->setObjectName("testConnectionButton");
        test_connection_status_ = new QLabel("尚未测试连接。测试会发送一次简短请求，可能消耗少量 API 额度。");
        test_connection_status_->setWordWrap(true);
        test_connection_status_->setStyleSheet("font-size: 12px; color: #756a5d;");
        form->addRow(test_connection_button_);
        form->addRow(test_connection_status_);
        connect(test_connection_button_, &QPushButton::clicked, this,
                [this] { test_connection(); });
        const auto mark_untested = [this] {
            if (!busy_) {
                test_connection_status_->setText("配置已更改，请重新测试连接。");
                test_connection_status_->setStyleSheet("font-size: 12px; color: #756a5d;");
            }
        };
        connect(base_url_, &QLineEdit::textChanged, this, mark_untested);
        connect(api_key_, &QLineEdit::textChanged, this, mark_untested);
        connect(model_, &QLineEdit::textChanged, this, mark_untested);
        auto* privacy = new QLabel("Key 不写入世界包、存档或日志；请求直达你填写的模型服务。");
        privacy->setWordWrap(true);
        privacy->setStyleSheet("font-size: 12px; color: #756a5d;");
        form->addRow(privacy);
        left_layout->addWidget(provider_group);

        auto* state_group = new QGroupBox("世界事实 · 引擎裁定");
        auto* state_layout = new QVBoxLayout(state_group);
        state_view_ = new QTextEdit;
        state_view_->setReadOnly(true);
        state_view_->setPlainText("导入世界包后，这里会显示地点、背包、人物记忆与事件。");
        state_layout->addWidget(state_view_);
        left_layout->addWidget(state_group, 1);

        auto* right = new QWidget;
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(8, 0, 0, 0);
        splitter->addWidget(right);
        auto* story_group = new QGroupBox("故事");
        auto* story_layout = new QVBoxLayout(story_group);
        story_view_ = new QTextBrowser;
        story_view_->setOpenExternalLinks(false);
        story_view_->setHtml("<p style='color:#756a5d'>导入世界包，填写模型信息，然后开始自由行动。</p>");
        story_layout->addWidget(story_view_);
        right_layout->addWidget(story_group, 1);
        auto* input_group = new QGroupBox("你想做什么？");
        auto* input_layout = new QVBoxLayout(input_group);
        action_input_ = new QTextEdit;
        action_input_->setPlaceholderText("例如：我悄悄靠近钟楼，观察齿轮的刻痕……");
        action_input_->setFixedHeight(90);
        send_button_ = new QPushButton("推进故事 →");
        input_layout->addWidget(action_input_);
        input_layout->addWidget(send_button_, 0, Qt::AlignRight);
        right_layout->addWidget(input_group);
        connect(send_button_, &QPushButton::clicked, this, [this] { send_turn(); });
        splitter->setSizes({400, 720});
    }

    ~MainWindow() override {
        if (worker_ && worker_->isRunning()) worker_->wait();
    }

private:
    [[nodiscard]] storyforge::agent::ProviderConfig provider_config() const {
        storyforge::agent::ProviderConfig config;
        config.base_url = base_url_->text().trimmed().toStdString();
        config.api_key = api_key_->text().toStdString();
        config.model = model_->text().trimmed().toStdString();
        return config;
    }

    void set_busy(bool busy, bool checking_connection = false) {
        busy_ = busy;
        send_button_->setEnabled(!busy);
        import_button_->setEnabled(!busy);
        test_connection_button_->setEnabled(!busy);
        base_url_->setEnabled(!busy);
        api_key_->setEnabled(!busy);
        model_->setEnabled(!busy);
        send_button_->setText(busy && !checking_connection ? "Agent 正在推进…" : "推进故事 →");
        test_connection_button_->setText(busy && checking_connection ? "连接测试中…" : "测试连接");
    }

    void test_connection() {
        if (busy_) return;
        const auto config = provider_config();
        const auto issues = storyforge::agent::validate(config);
        if (!issues.empty()) {
            const auto& field = issues.front().field;
            test_connection_status_->setText(field == "api_key" ? "请先填写 API Key。" :
                field == "model" ? "请先填写模型名称。" :
                "请填写有效的 API 地址：远程服务须使用 HTTPS，本机服务可使用 HTTP。");
            test_connection_status_->setStyleSheet("font-size: 12px; color: #9b3d31;");
            return;
        }
        set_busy(true, true);
        test_connection_status_->setText("正在向模型服务发送简短请求，请稍候…");
        test_connection_status_->setStyleSheet("font-size: 12px; color: #756a5d;");
        worker_ = QThread::create([this, config] {
            auto transport = std::make_shared<talestate_desktop::QtHttpTransport>();
            storyforge::agent::OpenAiCompatibleProvider provider(config, transport);
            auto result = storyforge::agent::check_connection(provider);
            QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
                set_busy(false);
                worker_ = nullptr;
                test_connection_status_->setText(QString::fromStdString(result.message) +
                    (result.connected() ? " 这不代表完整剧情流程已验证。" : ""));
                test_connection_status_->setStyleSheet(result.connected() ?
                    "font-size: 12px; color: #276144;" : "font-size: 12px; color: #9b3d31;");
            }, Qt::QueuedConnection);
        });
        connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
        worker_->start();
    }

    void import_world() {
        const auto path = QFileDialog::getOpenFileName(this, "选择 Story Package 的 story.json", {},
                                                        "Story Package (story.json *.json)");
        if (path.isEmpty()) return;
        const auto loaded = storyforge::load_story_package(path.toStdWString());
        if (!loaded.package || !loaded.report.valid()) {
            QString problems;
            for (const auto& issue : loaded.report.issues)
                problems += QString::fromStdString(issue.path + ": " + issue.message) + "\n";
            QMessageBox::warning(this, "世界包未通过校验", problems.left(3000));
            return;
        }
        if (loaded.package->agent_actions.empty() && loaded.package->agent_routes.empty() &&
            loaded.package->agent_npcs.empty()) {
            QMessageBox::warning(this, "缺少 Agent 规则",
                                 "这个世界包只有固定选项，还没有 Agent 行动规则，无法自由输入。请导入支持 Agent 的世界包。");
            return;
        }
        package_ = std::move(*loaded.package);
        state_ = storyforge::make_initial_state(*package_);
        agent_ = std::make_shared<storyforge::agent::StoryAgent>();
        world_label_->setText(QString::fromStdString(package_->title) + "\n" + path);
        story_view_->clear();
        append_story("世界", storyforge::current_node(*package_, state_)->text);
        state_view_->setPlainText(state_summary(*package_, state_));
    }

    void append_story(const QString& speaker, const std::string& text) {
        story_view_->append("<p><strong>" + speaker.toHtmlEscaped() + "</strong><br>" +
                            safe(text).replace("\n", "<br>") + "</p>");
    }

    void send_turn() {
        if (busy_ || !package_) {
            if (!package_) QMessageBox::information(this, "先导入世界", "请选择一个含 Agent 行动规则的世界包。");
            return;
        }
        const auto input = action_input_->toPlainText().trimmed().toStdString();
        const auto config = provider_config();
        const auto issues = storyforge::agent::validate(config);
        if (!issues.empty()) {
            QMessageBox::warning(this, "模型配置不完整", QString::fromStdString(issues.front().message));
            return;
        }
        if (input.empty()) {
            QMessageBox::information(this, "写下行动", "先写下你想做什么。\n例如：我询问守门人今晚是否有人经过。");
            return;
        }
        set_busy(true);
        append_story("你", input);
        action_input_->clear();
        const auto package = *package_;
        auto state = state_;
        const auto agent = agent_;
        worker_ = QThread::create([this, package, state, agent, config, input]() mutable {
            auto transport = std::make_shared<talestate_desktop::QtHttpTransport>();
            storyforge::agent::OpenAiCompatibleProvider provider(config, transport);
            auto result = agent->take_turn(package, state, input, provider);
            QMetaObject::invokeMethod(this, [this, result = std::move(result), state = std::move(state)]() mutable {
                set_busy(false);
                worker_ = nullptr;
                if (auto* failure = std::get_if<storyforge::agent::AgentFailure>(&result)) {
                    append_story("系统", failure->message);
                    return;
                }
                auto& turn = std::get<storyforge::agent::AgentTurn>(result);
                state_ = std::move(state);
                append_story("故事", turn.narration);
                if (turn.used_fallback_narration)
                    append_story("系统", "叙述请求失败，已显示引擎确认的事件；世界状态仍然有效。");
                state_view_->setPlainText(state_summary(*package_, state_));
                if (const auto* node = storyforge::current_node(*package_, state_); node && node->terminal)
                    append_story("系统", "这个故事已抵达结局。");
            }, Qt::QueuedConnection);
        });
        connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
        worker_->start();
    }

    std::optional<storyforge::StoryPackage> package_;
    storyforge::StoryState state_;
    std::shared_ptr<storyforge::agent::StoryAgent> agent_;
    QThread* worker_{nullptr};
    bool busy_{false};
    QLabel* world_label_{};
    QPushButton* import_button_{};
    QPushButton* send_button_{};
    QPushButton* test_connection_button_{};
    QLabel* test_connection_status_{};
    QLineEdit* base_url_{};
    QLineEdit* api_key_{};
    QLineEdit* model_{};
    QTextEdit* state_view_{};
    QTextBrowser* story_view_{};
    QTextEdit* action_input_{};
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow window;
    if (app.arguments().contains("--smoke-test") &&
        !window.findChild<QPushButton*>("testConnectionButton")) return 1;
    window.show();
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
