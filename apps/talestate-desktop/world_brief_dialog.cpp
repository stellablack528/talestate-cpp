#include "world_brief_dialog.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

WorldBriefDialog::WorldBriefDialog(const QByteArray& draft, QWidget* parent) : QDialog(parent) {
    setWindowTitle("整理世界设定");
    resize(810, 650);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel("按你想写的填，不用填满；左列可改名，也可新增条目。内容支持多行。生成时只提交已填写的条目。", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    table_ = new QTableWidget(0, 2, this);
    table_->setObjectName("worldBriefTable");
    table_->setHorizontalHeaderLabels({"设定项", "你的内容"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 170);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    if (!load_json(draft)) {
        for (const auto& name : {"题材与氛围", "世界背景", "玩家身份与目标", "重要地点",
                                 "关键人物", "世界规则与禁忌", "开局冲突", "自由补充"})
            add_row(QString::fromUtf8(name));
    }
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("＋ 增加条目", this);
    add->setObjectName("addWorldBriefRowButton");
    auto* remove = new QPushButton("删除选中条目", this);
    auto* import = new QPushButton("导入草稿", this);
    auto* export_button = new QPushButton("保存草稿", this);
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(import);
    row->addWidget(export_button);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this] { if (table_->rowCount() < 24) add_row(); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() >= 0) table_->removeRow(table_->currentRow());
    });
    connect(import, &QPushButton::clicked, this, [this] { import_draft(); });
    connect(export_button, &QPushButton::clicked, this, [this] { export_draft(); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("使用这份设定");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        const auto data = serialize(error);
        if (!error.isEmpty()) { QMessageBox::warning(this, "设定需要调整", error); return; }
        brief_ = data;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void WorldBriefDialog::add_row(const QString& name, const QString& content) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setRowHeight(row, 78);
    table_->setItem(row, 0, new QTableWidgetItem(name));
    auto* edit = new QPlainTextEdit(table_);
    edit->setPlainText(content);
    edit->setPlaceholderText("写一句或几句都可以……");
    table_->setCellWidget(row, 1, edit);
}

bool WorldBriefDialog::load_json(const QByteArray& data) {
    if (data.isEmpty() || data.size() > 16384) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto root = document.object();
    if (root.value("format").toString() != "talestate.world-brief/v1" ||
        !root.value("fields").isArray()) return false;
    const auto fields = root.value("fields").toArray();
    if (fields.isEmpty() || fields.size() > 24) return false;
    for (const auto& value : fields) {
        if (!value.isObject() || !value.toObject().value("name").isString() ||
            !value.toObject().value("content").isString()) return false;
    }
    table_->setRowCount(0);
    for (const auto& value : fields)
        add_row(value.toObject().value("name").toString(),
                value.toObject().value("content").toString());
    return true;
}

QByteArray WorldBriefDialog::serialize(QString& error) const {
    QJsonArray fields;
    std::set<QString> names;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* name_item = table_->item(row, 0);
        const auto* editor = qobject_cast<QPlainTextEdit*>(table_->cellWidget(row, 1));
        const auto name = name_item ? name_item->text().trimmed() : QString{};
        const auto content = editor ? editor->toPlainText().trimmed() : QString{};
        if (content.isEmpty()) continue;
        if (name.isEmpty() || name.size() > 40 || !names.insert(name).second) {
            error = "已填写内容的设定项需要不同的名称，名称不能超过 40 字。";
            return {};
        }
        fields.push_back(QJsonObject{{"name", name}, {"content", content}});
    }
    if (fields.isEmpty()) { error = "至少填写一项世界设定。"; return {}; }
    const auto data = QJsonDocument(QJsonObject{{"format", "talestate.world-brief/v1"},
        {"fields", fields}}).toJson(QJsonDocument::Compact);
    if (data.size() > 4000) {
        error = "设定超过 4000 字节，请精简部分内容；详细设定可以留在自己的草稿里。";
        return {};
    }
    return data;
}

void WorldBriefDialog::import_draft() {
    const auto path = QFileDialog::getOpenFileName(this, "导入世界设定草稿", {}, "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "导入失败", "无法读取草稿文件。"); return; }
    const auto data = file.read(16385);
    if (!load_json(data)) QMessageBox::warning(this, "导入失败", "草稿格式不正确，原来的设定仍保留。 ");
}

void WorldBriefDialog::export_draft() {
    QString error;
    const auto data = serialize(error);
    if (!error.isEmpty()) { QMessageBox::warning(this, "无法保存草稿", error); return; }
    const auto path = QFileDialog::getSaveFileName(this, "保存世界设定草稿", "world-brief.json", "JSON (*.json)");
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::warning(this, "保存失败", "草稿没有成功写入，请检查文件位置与权限。");
}
