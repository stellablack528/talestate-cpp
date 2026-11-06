#include "qt_http_transport.hpp"

#include "storyforge/agent/connection_check.hpp"
#include "storyforge/agent/story_agent.hpp"
#include "storyforge/agent/world_map_generator.hpp"
#include "storyforge/agent/world_seed_generator.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"

#include <memory>
#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QComboBox>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
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
    for (const auto& [item, quantity] : state.inventory) {
        const auto found = std::find_if(package.agent_objects.begin(), package.agent_objects.end(),
            [&](const storyforge::AgentObject& object) { return object.item == item; });
        const auto& label = found == package.agent_objects.end() ? item.value() : found->name;
        result += "  " + QString::fromStdString(label) + " ×" + QString::number(quantity) + "\n";
    }
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
        heading->setWordWrap(true);
        heading->setStyleSheet("font-size: 23px; font-weight: 700; color: #493723;");
        root->addWidget(heading);

        auto* splitter = new QSplitter(Qt::Horizontal);
        root->addWidget(splitter, 1);
        auto* left_scroll = new QScrollArea;
        left_scroll->setObjectName("leftPaneScroll");
        left_scroll->setWidgetResizable(true);
        left_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        left_scroll->setFrameShape(QFrame::NoFrame);
        splitter->addWidget(left_scroll);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 8, 0);
        left_scroll->setWidget(left);

        auto* world_group = new QGroupBox("世界包");
        auto* world_layout = new QVBoxLayout(world_group);
        world_label_ = new QLabel("尚未导入世界包");
        world_label_->setWordWrap(true);
        import_button_ = new QPushButton("导入世界包 story.json");
        world_layout->addWidget(world_label_);
        world_layout->addWidget(import_button_);
        map_button_ = new QPushButton("根据世界观生成地图草案");
        map_button_->setObjectName("generateMapButton");
        world_layout->addWidget(map_button_);
        seed_button_ = new QPushButton("根据世界观生成探索世界包");
        seed_button_->setObjectName("generateSeedButton");
        world_layout->addWidget(seed_button_);
        auto* save_row = new QHBoxLayout;
        save_button_ = new QPushButton("保存进度");
        save_button_->setObjectName("saveProgressButton");
        continue_button_ = new QPushButton("继续游戏");
        continue_button_->setObjectName("continueGameButton");
        save_row->addWidget(save_button_);
        save_row->addWidget(continue_button_);
        world_layout->addLayout(save_row);
        left_layout->addWidget(world_group);
        connect(import_button_, &QPushButton::clicked, this, [this] { import_world(); });
        connect(map_button_, &QPushButton::clicked, this, [this] { generate_map(); });
        connect(seed_button_, &QPushButton::clicked, this, [this] { generate_seed(); });
        connect(save_button_, &QPushButton::clicked, this, [this] { save_game(); });
        connect(continue_button_, &QPushButton::clicked, this, [this] { continue_game(); });

        auto* provider_group = new QGroupBox("你的模型 · BYOK");
        auto* provider_layout = new QVBoxLayout(provider_group);
        provider_layout->setSpacing(8);
        auto* form = new QFormLayout;
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        provider_layout->addLayout(form);
        provider_type_ = new QComboBox;
        provider_type_->setObjectName("providerTypeSelector");
        provider_type_->addItem("自定义 · OpenAI 兼容", "");
        provider_type_->addItem("OpenAI", "https://api.openai.com/v1");
        provider_type_->addItem("DeepSeek", "https://api.deepseek.com");
        provider_type_->addItem("OpenRouter", "https://openrouter.ai/api/v1");
        provider_type_->addItem("硅基流动 SiliconFlow", "https://api.siliconflow.cn/v1");
        base_url_ = new QLineEdit;
        base_url_->setObjectName("providerBaseUrl");
        base_url_->setPlaceholderText("https://你的服务地址/v1");
        api_key_ = new QLineEdit;
        api_key_->setObjectName("providerApiKey");
        api_key_->setEchoMode(QLineEdit::Password);
        api_key_->setPlaceholderText("仅在本次运行的内存中使用");
        model_ = new QLineEdit;
        model_->setPlaceholderText("模型名称");
        form->addRow("服务商", provider_type_);
        form->addRow("API 地址", base_url_);
        form->addRow("API Key", api_key_);
        form->addRow("模型", model_);
        auto* provider_hint = new QLabel("预设会填写官方默认地址；模型 ID 请从服务商控制台复制。也可修改地址，使用其他兼容服务。");
        provider_hint->setObjectName("providerHint");
        provider_hint->setWordWrap(true);
        provider_hint->setStyleSheet("font-size: 12px; color: #756a5d;");
        provider_layout->addWidget(provider_hint);
        connect(provider_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int index) {
                    base_url_->setText(provider_type_->itemData(index).toString());
                    api_key_->clear();
                    model_->clear();
                    model_->setPlaceholderText(index == 2 ? "例如 deepseek-flash" :
                        index == 3 ? "例如 供应商/模型名称" : "从服务商控制台复制模型 ID");
                });
        test_connection_button_ = new QPushButton("测试连接");
        test_connection_button_->setObjectName("testConnectionButton");
        test_connection_status_ = new QLabel("尚未测试连接。测试会发送一次简短请求，可能消耗少量 API 额度。");
        test_connection_status_->setObjectName("testConnectionStatus");
        test_connection_status_->setWordWrap(true);
        test_connection_status_->setStyleSheet("font-size: 12px; color: #756a5d;");
        provider_layout->addWidget(test_connection_button_);
        provider_layout->addWidget(test_connection_status_);
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
        auto* privacy = new QLabel("模型配置中的 Key 不写入世界包、存档或日志；请求直达你填写的模型服务。");
        privacy->setObjectName("providerPrivacy");
        privacy->setWordWrap(true);
        privacy->setStyleSheet("font-size: 12px; color: #756a5d;");
        provider_layout->addWidget(privacy);
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
        map_button_->setEnabled(!busy);
        seed_button_->setEnabled(!busy);
        save_button_->setEnabled(!busy);
        continue_button_->setEnabled(!busy);
        test_connection_button_->setEnabled(!busy);
        provider_type_->setEnabled(!busy);
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

    void show_map_draft(const storyforge::agent::WorldMapDraft& draft) {
        QDialog dialog(this);
        dialog.setWindowTitle("地图草案 · " + QString::fromStdString(draft.title));
        dialog.resize(850, 680);
        auto* layout = new QVBoxLayout(&dialog);
        auto* note = new QLabel("这是模型提出、C++ 校验过的地点与路线草案。保存后仍需补充人物、规则和剧情，才能成为可玩的世界包。");
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* scene = new QGraphicsScene(&dialog);
        for (const auto& route : draft.routes) {
            const auto find = [&draft](const std::string& id) {
                return std::find_if(draft.locations.begin(), draft.locations.end(),
                    [&id](const auto& location) { return location.id == id; });
            };
            const auto a = find(route.from);
            const auto b = find(route.to);
            scene->addLine(95 + a->x * 150, 65 + a->y * 110,
                           95 + b->x * 150, 65 + b->y * 110,
                           QPen(QColor("#92795e"), 3));
        }
        for (const auto& location : draft.locations) {
            const int x = 95 + location.x * 150;
            const int y = 65 + location.y * 110;
            const bool start = location.id == draft.start;
            scene->addEllipse(x - 13, y - 13, 26, 26,
                              QPen(QColor("#554436"), 2),
                              QBrush(QColor(start ? "#d5a35a" : "#fffdf8")));
            auto* label = scene->addText(QString::fromStdString(location.title));
            label->setDefaultTextColor(QColor("#2f2d2a"));
            label->setPos(x - label->boundingRect().width() / 2, y + 13);
        }
        scene->setSceneRect(0, 0, 790, 570);
        auto* view = new QGraphicsView(scene);
        view->setRenderHint(QPainter::Antialiasing);
        layout->addWidget(view, 1);
        QString details;
        for (const auto& location : draft.locations)
            details += QString::fromStdString(location.title + "：" + location.description) + "\n";
        auto* description = new QLabel(details.trimmed());
        description->setWordWrap(true);
        layout->addWidget(description);
        auto* buttons = new QHBoxLayout;
        auto* save = new QPushButton("保存地图 JSON 草案");
        auto* close = new QPushButton("关闭");
        buttons->addWidget(save);
        buttons->addWidget(close);
        layout->addLayout(buttons);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(save, &QPushButton::clicked, &dialog, [this, &draft] {
            const auto path = QFileDialog::getSaveFileName(this, "保存地图草案", "world-map-draft.json", "JSON (*.json)");
            if (path.isEmpty()) return;
            QSaveFile file(path);
            const auto bytes = QByteArray::fromStdString(storyforge::agent::serialize_world_map_draft(draft));
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                QMessageBox::warning(this, "保存失败", "无法写入地图草案，请检查文件位置和权限。");
        });
        dialog.exec();
    }

    void generate_map() {
        if (busy_) return;
        const auto config = provider_config();
        if (const auto issues = storyforge::agent::validate(config); !issues.empty()) {
            QMessageBox::warning(this, "模型配置不完整", QString::fromStdString(issues.front().message));
            return;
        }
        bool accepted = false;
        const auto brief = QInputDialog::getMultiLineText(this, "生成地图草案", "描述世界观、重要地点和探索方向：", {}, &accepted).trimmed();
        if (!accepted) return;
        if (brief.isEmpty() || brief.toUtf8().size() > 4000) {
            QMessageBox::warning(this, "世界观长度不合适", "请填写不超过 4000 字节的世界观描述。");
            return;
        }
        set_busy(true);
        map_button_->setText("地图生成中…");
        worker_ = QThread::create([this, config, brief = brief.toStdString()] {
            auto transport = std::make_shared<talestate_desktop::QtHttpTransport>();
            storyforge::agent::OpenAiCompatibleProvider provider(config, transport);
            auto result = storyforge::agent::generate_world_map_draft(brief, provider);
            QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
                set_busy(false);
                map_button_->setText("根据世界观生成地图草案");
                worker_ = nullptr;
                if (const auto* error = std::get_if<storyforge::agent::WorldMapError>(&result))
                    QMessageBox::warning(this, "地图生成失败", QString::fromStdString(error->message));
                else show_map_draft(std::get<storyforge::agent::WorldMapDraft>(result));
            }, Qt::QueuedConnection);
        });
        connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
        worker_->start();
    }

    void generate_seed() {
        if (busy_) return;
        const auto config = provider_config();
        if (const auto issues = storyforge::agent::validate(config); !issues.empty()) {
            QMessageBox::warning(this, "模型配置不完整", QString::fromStdString(issues.front().message));
            return;
        }
        bool accepted = false;
        const auto brief = QInputDialog::getMultiLineText(this, "生成探索世界包",
            "描述世界观、重要地点和想探索的冲突：", {}, &accepted).trimmed();
        if (!accepted) return;
        if (brief.isEmpty() || brief.toUtf8().size() > 4000) {
            QMessageBox::warning(this, "世界观长度不合适", "请填写不超过 4000 字节的世界观描述。");
            return;
        }
        set_busy(true);
        seed_button_->setText("世界包生成中…");
        worker_ = QThread::create([this, config, input = brief.toStdString()] {
            auto transport = std::make_shared<talestate_desktop::QtHttpTransport>();
            storyforge::agent::OpenAiCompatibleProvider provider(config, transport);
            auto map = storyforge::agent::generate_world_map_draft(input, provider, 3);
            storyforge::agent::WorldSeedResult result;
            if (const auto* failure = std::get_if<storyforge::agent::WorldMapError>(&map))
                result = storyforge::agent::WorldSeedError{failure->message};
            else result = storyforge::agent::generate_world_seed_from_map(
                std::get<storyforge::agent::WorldMapDraft>(map), input, provider);
            QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
                set_busy(false);
                seed_button_->setText("根据世界观生成探索世界包");
                worker_ = nullptr;
                if (const auto* failure = std::get_if<storyforge::agent::WorldSeedError>(&result)) {
                    QMessageBox::warning(this, "生成失败", QString::fromStdString(failure->message));
                    return;
                }
                const auto& seed = std::get<storyforge::agent::WorldSeedDraft>(result);
                const auto checked = storyforge::load_story_package_text(seed.story_json);
                if (!checked.package || !checked.report.valid()) {
                    QString problems;
                    for (const auto& issue : checked.report.issues)
                        problems += QString::fromStdString(issue.path + ": " + issue.message) + "\n";
                    QMessageBox::warning(this, "生成的世界包未通过校验", problems.left(3000));
                    return;
                }
                const auto path = QFileDialog::getSaveFileName(this, "保存探索世界包", "story.json", "Story Package (*.json)");
                if (path.isEmpty()) return;
                const auto bytes = QByteArray::fromStdString(seed.story_json);
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                    QMessageBox::warning(this, "保存失败", "无法写入世界包，请检查文件位置和权限。");
                    return;
                }
                if (!load_world_from_path(path)) return;
                QMessageBox::information(this, "探索世界已加载",
                    "已生成三个地点和初始人物，可以开始探索与交谈。\n当前阶段尚未生成结局与复杂物品规则。\n目标：" +
                    QString::fromStdString(seed.opening_goal));
            }, Qt::QueuedConnection);
        });
        connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
        worker_->start();
    }

    void import_world() {
        const auto path = QFileDialog::getOpenFileName(this, "选择 Story Package 的 story.json", {},
                                                        "Story Package (story.json *.json)");
        if (path.isEmpty()) return;
        load_world_from_path(path);
    }

    bool load_world_from_path(const QString& path) {
        const auto loaded = storyforge::load_story_package(path.toStdWString());
        if (!loaded.package || !loaded.report.valid()) {
            QString problems;
            for (const auto& issue : loaded.report.issues)
                problems += QString::fromStdString(issue.path + ": " + issue.message) + "\n";
            QMessageBox::warning(this, "世界包未通过校验", problems.left(3000));
            return false;
        }
        if (loaded.package->agent_actions.empty() && loaded.package->agent_routes.empty() &&
            loaded.package->agent_npcs.empty() && loaded.package->agent_objects.empty() &&
            loaded.package->agent_item_rules.empty()) {
            QMessageBox::warning(this, "缺少 Agent 规则",
                                 "这个世界包只有固定选项，还没有 Agent 行动规则，无法自由输入。请导入支持 Agent 的世界包。");
            return false;
        }
        package_ = std::move(*loaded.package);
        state_ = storyforge::make_initial_state(*package_);
        agent_ = std::make_shared<storyforge::agent::StoryAgent>();
        world_label_->setText(QString::fromStdString(package_->title) + "\n" + path);
        story_view_->clear();
        append_story("世界", storyforge::current_node(*package_, state_)->text);
        state_view_->setPlainText(state_summary(*package_, state_));
        return true;
    }

    void save_game() {
        if (busy_) return;
        if (!package_) {
            QMessageBox::information(this, "先导入世界", "请先导入要游玩的世界包，再保存进度。");
            return;
        }
        const auto encoded = storyforge::serialize_story_save(*package_, state_);
        if (const auto* failure = std::get_if<storyforge::SaveError>(&encoded)) {
            QMessageBox::warning(this, "无法保存", QString::fromStdString(failure->message));
            return;
        }
        const auto path = QFileDialog::getSaveFileName(this, "保存游戏进度",
            QString::fromStdString(package_->id.value()) + "-save.json", "TaleState 存档 (*.json)");
        if (path.isEmpty()) return;
        const auto bytes = QByteArray::fromStdString(std::get<std::string>(encoded));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::warning(this, "保存失败", "无法写入存档文件；已有存档未被替换。请检查文件位置和权限。");
            return;
        }
        append_story("系统", "进度已保存。存档不包含模型配置中的 API Key；下次请先导入同一 ID 和版本的世界包，再点击“继续游戏”。");
    }

    void continue_game() {
        if (busy_) return;
        if (!package_) {
            QMessageBox::information(this, "先导入世界", "请先导入创建存档时使用的世界包，再点击“继续游戏”。");
            return;
        }
        const auto path = QFileDialog::getOpenFileName(this, "选择 TaleState 存档", {},
                                                        "TaleState 存档 (*.json)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "读取失败", "无法读取这个存档文件，当前进度未改变。");
            return;
        }
        const auto bytes = file.read(static_cast<qint64>(storyforge::max_story_save_bytes) + 1);
        if (bytes.size() > static_cast<qint64>(storyforge::max_story_save_bytes) || file.error() != QFile::NoError) {
            QMessageBox::warning(this, "读取失败", "存档过大或读取失败，当前进度未改变。");
            return;
        }
        const auto decoded = storyforge::deserialize_story_save(*package_, bytes.toStdString());
        if (const auto* failure = std::get_if<storyforge::SaveError>(&decoded)) {
            QMessageBox::warning(this, "无法继续游戏", QString::fromStdString(failure->message));
            return;
        }
        state_ = std::get<storyforge::StoryState>(decoded);
        agent_ = std::make_shared<storyforge::agent::StoryAgent>();
        action_input_->clear();
        story_view_->clear();
        append_story("系统", "已恢复世界进度。之前的聊天文字不会恢复，但地点、物品和剧情事实已恢复。模型配置不会从存档恢复。");
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
    QPushButton* map_button_{};
    QPushButton* seed_button_{};
    QPushButton* save_button_{};
    QPushButton* continue_button_{};
    QPushButton* send_button_{};
    QPushButton* test_connection_button_{};
    QComboBox* provider_type_{};
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
    if (app.arguments().contains("--smoke-test")) {
        auto* selector = window.findChild<QComboBox*>("providerTypeSelector");
        auto* base_url = window.findChild<QLineEdit*>("providerBaseUrl");
        auto* api_key = window.findChild<QLineEdit*>("providerApiKey");
        if (!window.findChild<QPushButton*>("testConnectionButton") ||
            !window.findChild<QPushButton*>("saveProgressButton") ||
            !window.findChild<QPushButton*>("continueGameButton") ||
            !window.findChild<QPushButton*>("generateMapButton") ||
            !window.findChild<QPushButton*>("generateSeedButton") ||
            !selector || !base_url || !api_key) return 1;
        const QStringList expected{"", "https://api.openai.com/v1", "https://api.deepseek.com",
                                   "https://openrouter.ai/api/v1", "https://api.siliconflow.cn/v1"};
        for (int index = 1; index < expected.size(); ++index) {
            api_key->setText("previous-provider-secret");
            selector->setCurrentIndex(index);
            if (base_url->text() != expected[index] || !api_key->text().isEmpty()) return 1;
        }
    }
    if (app.arguments().contains("--layout-test")) {
        window.resize(860, 600);
        auto* scroll = window.findChild<QScrollArea*>("leftPaneScroll");
        auto* hint = window.findChild<QLabel*>("providerHint");
        auto* button = window.findChild<QPushButton*>("testConnectionButton");
        auto* status = window.findChild<QLabel*>("testConnectionStatus");
        auto* privacy = window.findChild<QLabel*>("providerPrivacy");
        if (!scroll || !hint || !button || !status || !privacy) return 1;
        QTimer::singleShot(0, &app, [&] {
            status->setText("模型配置中的地址或模型名称无效。请检查服务商控制台中的设置，"
                            "然后重新测试连接；这段说明会随着窗口宽度自动换行。");
            app.processEvents();
            const bool separated = hint->geometry().bottom() < button->geometry().top() &&
                button->geometry().bottom() < status->geometry().top() &&
                status->geometry().bottom() < privacy->geometry().top();
            const bool scrollable = scroll->verticalScrollBar()->maximum() > 0;
            const auto screenshot = qEnvironmentVariable("TALESTATE_LAYOUT_SNAPSHOT");
            if (!screenshot.isEmpty()) window.grab().save(screenshot);
            app.exit(separated && scrollable ? 0 : 1);
        });
    }
    window.show();
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
