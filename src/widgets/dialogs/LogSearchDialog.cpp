// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/LogSearchDialog.hpp"

#include "Application.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QDate>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTableWidget>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <utility>

namespace chatterino {
namespace {

constexpr int kResultLimit = 2000;

struct SearchRequest {
    QString root;
    QString text;
    QString username;
    QString channel;
    bool useDates = false;
    QDate from;
    QDate to;
};

class SearchJob : public QObject
{
public:
    std::shared_ptr<std::atomic<bool>> cancel;
    SearchRequest request;
    QPointer<LogSearchDialog> dialog;
    int generation = 0;

    void run()
    {
        QVector<LogSearchHit> batch;
        int matches = 0;
        int files = 0;
        bool truncated = false;
        const auto text = this->request.text.toLower();
        const auto username = this->request.username.toLower();
        auto channel = this->request.channel.toLower();
        if (channel.startsWith(u'#'))
        {
            channel.remove(0, 1);
        }

        QDirIterator iterator(this->request.root, {QStringLiteral("*.log")},
                              QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            if (this->cancel && this->cancel->load())
            {
                break;
            }
            const auto path = iterator.next();
            const auto info = iterator.fileInfo();
            ++files;

            const auto parent = info.dir().dirName();
            if (!channel.isEmpty() && !parent.toLower().contains(channel))
            {
                continue;
            }

            const auto fileDate = dateInFileName(info.fileName());
            if (this->request.useDates)
            {
                const auto date =
                    fileDate.isValid() ? fileDate : info.lastModified().date();
                if (date < this->request.from || date > this->request.to)
                {
                    continue;
                }
            }

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                continue;
            }
            QTextStream stream(&file);
            stream.setEncoding(QStringConverter::Utf8);
            int lineNumber = 0;
            while (!stream.atEnd())
            {
                if (this->cancel && this->cancel->load())
                {
                    break;
                }
                const auto line = stream.readLine();
                ++lineNumber;
                if (line.startsWith(u"# Start logging"_s) ||
                    line.startsWith(u"# Stop logging"_s))
                {
                    continue;
                }

                const auto hit = parseLine(line, parent, path, lineNumber);
                if (!username.isEmpty() &&
                    !hit.user.toLower().contains(username))
                {
                    continue;
                }
                if (!text.isEmpty() && !hit.message.toLower().contains(text) &&
                    !line.toLower().contains(text))
                {
                    continue;
                }

                batch.push_back(hit);
                ++matches;
                if (batch.size() >= 40)
                {
                    this->deliver(batch);
                    batch.clear();
                }
                if (matches >= kResultLimit)
                {
                    truncated = true;
                    break;
                }
            }

            if (truncated)
            {
                break;
            }
            if (files % 25 == 0)
            {
                this->report(files, matches);
            }
        }

        if (!batch.isEmpty())
        {
            this->deliver(batch);
        }

        const auto scanned = files;
        const auto stoppedEarly = truncated;
        const auto generation = this->generation;
        QMetaObject::invokeMethod(
            this->dialog,
            [dialog = this->dialog, generation, scanned, stoppedEarly] {
                if (dialog == nullptr)
                {
                    return;
                }
                dialog->finishSearch(generation, scanned, stoppedEarly);
            },
            Qt::QueuedConnection);
        if (auto *thread = QThread::currentThread())
        {
            thread->quit();
        }
    }

private:
    static QDate dateInFileName(const QString &fileName)
    {
        const auto match =
            QRegularExpression(u"(\\d{4}-\\d{2}-\\d{2})\\.log$"_s)
                .match(fileName);
        if (!match.hasMatch())
        {
            return {};
        }
        return QDate::fromString(match.captured(1), Qt::ISODate);
    }

    static LogSearchHit parseLine(const QString &line, const QString &channel,
                                  const QString &path, int lineNumber)
    {
        LogSearchHit hit;
        hit.channel = channel;
        hit.line = line;
        hit.filePath = path;
        hit.lineNumber = lineNumber;

        auto rest = QStringView(line).trimmed();
        if (rest.startsWith(u'#'))
        {
            const auto space = rest.indexOf(u' ');
            if (space > 1)
            {
                hit.channel = rest.mid(1, space - 1).toString();
                rest = rest.mid(space + 1).trimmed();
            }
        }
        if (rest.startsWith(u'[') && rest.contains(u']'))
        {
            const auto end = rest.indexOf(u']');
            hit.timestamp = rest.mid(1, end - 1).toString();
            rest = rest.mid(end + 1).trimmed();
        }

        const auto colon = rest.indexOf(u':');
        if (colon > 0)
        {
            const auto head = rest.left(colon).trimmed();
            hit.message = rest.mid(colon + 1).trimmed().toString();
            const auto parts = head.toString().split(u' ', Qt::SkipEmptyParts);
            hit.user = parts.isEmpty() ? head.toString() : parts.constLast();
        }
        else
        {
            hit.message = rest.toString();
        }
        return hit;
    }

    void deliver(const QVector<LogSearchHit> &hits)
    {
        const auto generation = this->generation;
        QMetaObject::invokeMethod(
            this->dialog,
            [dialog = this->dialog, generation, hits] {
                if (dialog == nullptr)
                {
                    return;
                }
                dialog->appendHits(generation, hits);
            },
            Qt::QueuedConnection);
    }

    void report(int files, int matches)
    {
        const auto generation = this->generation;
        QMetaObject::invokeMethod(
            this->dialog,
            [dialog = this->dialog, generation, files, matches] {
                if (dialog == nullptr)
                {
                    return;
                }
                dialog->reportProgress(generation, files, matches);
            },
            Qt::QueuedConnection);
    }
};

}  // namespace

void LogSearchDialog::open(QWidget *parent, const QString &channel)
{
    auto *dialog = new LogSearchDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (!channel.isEmpty())
    {
        dialog->channel_->setText(channel);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

LogSearchDialog::LogSearchDialog(QWidget *parent)
    : QDialog(parent)
{
    this->setWindowTitle(QStringLiteral("Search saved logs"));
    this->resize(760, 520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    this->text_ = new QLineEdit(this);
    this->username_ = new QLineEdit(this);
    this->channel_ = new QLineEdit(this);
    this->text_->setPlaceholderText(QStringLiteral("Message text"));
    this->username_->setPlaceholderText(
        QStringLiteral("Login or display name"));
    this->channel_->setPlaceholderText(QStringLiteral("Any channel"));
    this->text_->setClearButtonEnabled(true);
    this->username_->setClearButtonEnabled(true);
    this->channel_->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("Text"), this->text_);
    form->addRow(QStringLiteral("Username"), this->username_);
    form->addRow(QStringLiteral("Channel"), this->channel_);

    this->useDates_ = new QCheckBox(QStringLiteral("Limit to dates"), this);
    this->from_ = new QDateEdit(QDate::currentDate().addDays(-30), this);
    this->to_ = new QDateEdit(QDate::currentDate(), this);
    this->from_->setCalendarPopup(true);
    this->to_->setCalendarPopup(true);
    this->from_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->to_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->from_->setEnabled(false);
    this->to_->setEnabled(false);
    QObject::connect(this->useDates_, &QCheckBox::toggled, this,
                     [this](bool enabled) {
                         this->from_->setEnabled(enabled);
                         this->to_->setEnabled(enabled);
                     });
    auto *dates = new QHBoxLayout();
    dates->addWidget(this->useDates_);
    dates->addWidget(this->from_);
    dates->addWidget(new QLabel(QStringLiteral("to"), this));
    dates->addWidget(this->to_);
    dates->addStretch(1);
    form->addRow(QStringLiteral("Dates"), dates);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout();
    this->searchButton_ = new QPushButton(QStringLiteral("Search"), this);
    auto *copy = new QPushButton(QStringLiteral("Copy"), this);
    auto *openFile = new QPushButton(QStringLiteral("Open log file"), this);
    buttons->addWidget(this->searchButton_);
    buttons->addWidget(copy);
    buttons->addWidget(openFile);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    this->status_ = new QLabel(
        QStringLiteral(
            "Search runs in the background and does not freeze chat."),
        this);
    this->status_->setWordWrap(true);
    layout->addWidget(this->status_);

    this->results_ = new QTableWidget(this);
    this->results_->setColumnCount(4);
    this->results_->setHorizontalHeaderLabels(
        {QStringLiteral("Time"), QStringLiteral("Channel"),
         QStringLiteral("User"), QStringLiteral("Message")});
    this->results_->horizontalHeader()->setStretchLastSection(true);
    this->results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->results_->setSelectionMode(QAbstractItemView::SingleSelection);
    this->results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->results_->verticalHeader()->setVisible(false);
    layout->addWidget(this->results_, 1);

    QObject::connect(this->searchButton_, &QPushButton::clicked, this,
                     &LogSearchDialog::startSearch);
    QObject::connect(this->text_, &QLineEdit::returnPressed, this,
                     &LogSearchDialog::startSearch);
    QObject::connect(this->username_, &QLineEdit::returnPressed, this,
                     &LogSearchDialog::startSearch);
    QObject::connect(copy, &QPushButton::clicked, this,
                     &LogSearchDialog::copySelected);
    QObject::connect(openFile, &QPushButton::clicked, this,
                     &LogSearchDialog::openSelected);
    QObject::connect(this->results_, &QTableWidget::cellDoubleClicked, this,
                     [this](int, int) {
                         this->copySelected();
                     });
}

LogSearchDialog::~LogSearchDialog()
{
    if (this->searchCancel_)
    {
        this->searchCancel_->store(true);
    }
    if (this->thread_ != nullptr)
    {
        this->thread_->quit();
        this->thread_->wait(3000);
    }
}

void LogSearchDialog::startSearch()
{
    const auto text = this->text_->text().trimmed();
    const auto username = this->username_->text().trimmed();
    if (text.isEmpty() && username.isEmpty())
    {
        this->status_->setText(
            QStringLiteral("Enter message text or a username."));
        return;
    }

    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return;
    }
    const auto custom = getSettings()->logPath.getValue();
    const auto root =
        custom.isEmpty() ? app->getPaths().messageLogDirectory : custom;
    if (!QDir(root).exists())
    {
        this->status_->setText(QStringLiteral(
            "No log folder yet. Turn on logging and chat first."));
        return;
    }

    if (this->searchCancel_)
    {
        this->searchCancel_->store(true);
    }
    if (this->thread_ != nullptr)
    {
        this->thread_->quit();
        this->thread_->wait(10000);
        this->thread_->deleteLater();
        this->thread_ = nullptr;
    }

    this->results_->setRowCount(0);
    this->searching_ = true;
    const auto generation = ++this->generation_;
    this->status_->setText(QStringLiteral("Searching..."));
    this->searchButton_->setText(QStringLiteral("Searching..."));

    auto *thread = new QThread(this);
    auto *job = new SearchJob();
    this->searchCancel_ = std::make_shared<std::atomic<bool>>(false);
    job->cancel = this->searchCancel_;
    job->generation = generation;
    job->dialog = this;
    job->request = SearchRequest{
        root,
        text,
        username,
        this->channel_->text().trimmed(),
        this->useDates_->isChecked(),
        this->from_->date(),
        this->to_->date(),
    };
    job->moveToThread(thread);
    QObject::connect(thread, &QThread::started, job, [job] {
        job->run();
    });
    QObject::connect(thread, &QThread::finished, job, &QObject::deleteLater);
    this->thread_ = thread;
    thread->start();
}

void LogSearchDialog::appendHits(int generation,
                                 const QVector<LogSearchHit> &hits)
{
    if (generation != this->generation_)
    {
        return;
    }
    for (const auto &hit : hits)
    {
        const auto row = this->results_->rowCount();
        this->results_->insertRow(row);
        auto *time = new QTableWidgetItem(hit.timestamp);
        time->setData(Qt::UserRole, hit.filePath);
        time->setData(Qt::UserRole + 1, hit.lineNumber);
        time->setData(Qt::UserRole + 2, hit.line);
        this->results_->setItem(row, 0, time);
        this->results_->setItem(row, 1, new QTableWidgetItem(hit.channel));
        this->results_->setItem(row, 2, new QTableWidgetItem(hit.user));
        this->results_->setItem(row, 3, new QTableWidgetItem(hit.message));
    }
    this->status_->setText(QStringLiteral("Searching... %1 matches")
                               .arg(this->results_->rowCount()));
}

void LogSearchDialog::reportProgress(int generation, int files, int matches)
{
    if (generation != this->generation_ || !this->searching_)
    {
        return;
    }
    this->status_->setText(QStringLiteral("Searching... %1 files, %2 matches")
                               .arg(files)
                               .arg(matches));
}

void LogSearchDialog::finishSearch(int generation, int scannedFiles,
                                   bool truncated)
{
    if (generation != this->generation_)
    {
        return;
    }
    this->searching_ = false;
    this->searchButton_->setText(QStringLiteral("Search"));
    auto text = QStringLiteral("%1 matches in %2 files")
                    .arg(this->results_->rowCount())
                    .arg(scannedFiles);
    if (truncated)
    {
        text += QStringLiteral(". Stopped after %1 matches.").arg(kResultLimit);
    }
    this->status_->setText(text);
}

void LogSearchDialog::copySelected()
{
    const auto row = this->results_->currentRow();
    if (row < 0)
    {
        return;
    }
    const auto *item = this->results_->item(row, 0);
    if (item == nullptr)
    {
        return;
    }
    const auto line = item->data(Qt::UserRole + 2).toString();
    const auto channel = this->results_->item(row, 1) == nullptr
                             ? QString()
                             : this->results_->item(row, 1)->text();
    QGuiApplication::clipboard()->setText(
        channel.isEmpty() ? line : channel + QStringLiteral(" ") + line);
    this->status_->setText(QStringLiteral("Copied the selected line."));
}

void LogSearchDialog::openSelected()
{
    const auto row = this->results_->currentRow();
    if (row < 0)
    {
        return;
    }
    const auto *item = this->results_->item(row, 0);
    if (item == nullptr)
    {
        return;
    }
    const auto path = item->data(Qt::UserRole).toString();
    const auto lineNumber = item->data(Qt::UserRole + 1).toInt();
    if (path.isEmpty())
    {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    this->status_->setText(
        QStringLiteral("Opened the log at line %1.").arg(lineNumber));
}

}  // namespace chatterino
