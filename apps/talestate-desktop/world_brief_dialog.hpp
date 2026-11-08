#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>

class QTableWidget;

class WorldBriefDialog final : public QDialog {
public:
    explicit WorldBriefDialog(const QByteArray& draft, QWidget* parent = nullptr);
    [[nodiscard]] QByteArray brief() const { return brief_; }

private:
    void add_row(const QString& name = {}, const QString& content = {});
    bool load_json(const QByteArray& data);
    [[nodiscard]] QByteArray serialize(QString& error) const;
    void import_draft();
    void export_draft();

    QTableWidget* table_{};
    QByteArray brief_;
};
