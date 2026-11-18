// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDate>
#include <QDialog>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

class QLabel;
class QLineEdit;
class QDateEdit;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QThread;

namespace chatterino {

struct LogSearchHit {
    QString timestamp;
    QString channel;
    QString user;
    QString message;
    QString line;
    QString filePath;
    int lineNumber = 0;
};

class LogSearchDialog : public QDialog
{
    Q_OBJECT

public:
    static void open(QWidget *parent, const QString &channel = {});

    explicit LogSearchDialog(QWidget *parent = nullptr);
    ~LogSearchDialog() override;

    void appendHits(int generation, const QVector<LogSearchHit> &hits);
    void reportProgress(int generation, int files, int matches);
    void finishSearch(int generation, int scannedFiles, bool truncated);

private:
    void startSearch();
    void copySelected();
    void openSelected();

    QLineEdit *text_{};
    QLineEdit *username_{};
    QLineEdit *channel_{};
    QCheckBox *useDates_{};
    QDateEdit *from_{};
    QDateEdit *to_{};
    QPushButton *searchButton_{};
    QLabel *status_{};
    QTableWidget *results_{};

    QThread *thread_{};
    std::shared_ptr<std::atomic<bool>> searchCancel_;
    bool searching_ = false;
    int generation_ = 0;
};

}  // namespace chatterino
