// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/chat/EmoteUsageStats.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Paths.hpp"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSaveFile>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

using namespace Qt::Literals::StringLiterals;

namespace chatterino {
namespace {

constexpr int kPersonalLimit = 400;

struct Key {
    QString provider;
    QString name;

    bool operator==(const Key &other) const
    {
        return this->provider == other.provider && this->name == other.name;
    }
};

struct KeyHash {
    size_t operator()(const Key &key) const noexcept
    {
        return qHash(key.provider) ^ (qHash(key.name) << 1);
    }
};

QString providerOf(const Emote &emote)
{
    const auto page = emote.homePage.string.toLower();
    if (page.contains(u"betterttv"_s))
    {
        return QStringLiteral("BTTV");
    }
    if (page.contains(u"frankerfacez"_s))
    {
        return QStringLiteral("FFZ");
    }
    if (page.contains(u"7tv"_s))
    {
        return QStringLiteral("7TV");
    }
    return QStringLiteral("Twitch");
}

void collect(const MessageElement *element, std::vector<Key> &out)
{
    if (element == nullptr)
    {
        return;
    }

    if (const auto *emote = dynamic_cast<const EmoteElement *>(element))
    {
        if (emote->getFlags().has(MessageElementFlag::EmojiAll) ||
            !emote->getFlags().has(MessageElementFlag::Emote) ||
            emote->getEmote() == nullptr ||
            emote->getEmote()->name.string.isEmpty())
        {
            return;
        }
        out.push_back(
            {providerOf(*emote->getEmote()), emote->getEmote()->name.string});
        return;
    }

    if (const auto *layered =
            dynamic_cast<const LayeredEmoteElement *>(element))
    {
        for (const auto &layer : layered->getEmotes())
        {
            if (layer.ptr == nullptr || layer.ptr->name.string.isEmpty())
            {
                continue;
            }
            out.push_back({providerOf(*layer.ptr), layer.ptr->name.string});
        }
    }
}

QString statsPath()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return {};
    }
    return app->getPaths().settingsDirectory +
           QStringLiteral("/emote-usage.json");
}

class Store
{
public:
    void load()
    {
        if (this->loaded_)
        {
            return;
        }
        this->loaded_ = true;

        QFile file(statsPath());
        if (!file.open(QIODevice::ReadOnly))
        {
            return;
        }
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
        {
            return;
        }
        for (const auto &value : document.object().value("sent").toArray())
        {
            const auto object = value.toObject();
            const auto name = object.value("name").toString();
            const auto provider = object.value("provider").toString();
            const auto count = object.value("count").toInt();
            if (name.isEmpty() || provider.isEmpty() || count <= 0)
            {
                continue;
            }
            this->personal_.insert({provider, name}, count);
        }
    }

    void observe(const Channel &channel, const Message &message)
    {
        if (message.flags.has(MessageFlag::System))
        {
            return;
        }

        std::vector<Key> keys;
        for (const auto &element : message.elements)
        {
            collect(element.get(), keys);
        }
        if (keys.empty())
        {
            return;
        }

        bool mine = false;
        if (auto *app = tryGetApp())
        {
            const auto &userId =
                app->getAccounts()->twitch.getCurrent()->getUserId();
            mine = !userId.isEmpty() && message.userID == userId;
        }

        std::lock_guard lock(this->mutex_);
        this->load();
        const auto channelName = channel.getName().toLower();
        auto &channelCounts = this->channels_[channelName];
        for (const auto &key : keys)
        {
            channelCounts[key] += 1;
            if (mine)
            {
                this->personal_[key] += 1;
                this->personalDirty_ = true;
            }
        }
        if (this->personal_.size() > kPersonalLimit)
        {
            this->trimPersonal();
        }
        if (this->personalDirty_)
        {
            this->savePersonal();
        }
    }

    std::vector<EmoteUsageRow> personalRows()
    {
        std::lock_guard lock(this->mutex_);
        this->load();
        return rowsFrom(this->personal_, 80);
    }

    std::vector<EmoteUsageRow> channelRows(const QString &channelName,
                                           int limit)
    {
        std::lock_guard lock(this->mutex_);
        const auto it = this->channels_.constFind(channelName.toLower());
        if (it == this->channels_.cend())
        {
            return {};
        }
        return rowsFrom(*it, limit);
    }

    void resetPersonal()
    {
        std::lock_guard lock(this->mutex_);
        this->loaded_ = true;
        this->personal_.clear();
        this->personalDirty_ = true;
        this->savePersonal();
    }

    void resetChannel(const QString &channelName)
    {
        std::lock_guard lock(this->mutex_);
        this->channels_.remove(channelName.toLower());
    }

private:
    using Counts = std::unordered_map<Key, int, KeyHash>;

    static std::vector<EmoteUsageRow> rowsFrom(const Counts &counts, int limit)
    {
        std::vector<EmoteUsageRow> rows;
        rows.reserve(counts.size());
        for (const auto &[key, count] : counts)
        {
            rows.push_back({key.name, key.provider, count});
        }
        std::sort(rows.begin(), rows.end(),
                  [](const EmoteUsageRow &left, const EmoteUsageRow &right) {
                      if (left.count != right.count)
                      {
                          return left.count > right.count;
                      }
                      return left.name.compare(right.name,
                                               Qt::CaseInsensitive) < 0;
                  });
        if (static_cast<int>(rows.size()) > limit)
        {
            rows.resize(static_cast<size_t>(limit));
        }
        return rows;
    }

    void trimPersonal()
    {
        auto rows = rowsFrom(this->personal_, kPersonalLimit);
        this->personal_.clear();
        for (const auto &row : rows)
        {
            this->personal_.insert({row.provider, row.name}, row.count);
        }
    }

    void savePersonal()
    {
        this->personalDirty_ = false;
        const auto path = statsPath();
        if (path.isEmpty())
        {
            return;
        }

        QJsonArray sent;
        for (const auto &[key, count] : this->personal_)
        {
            QJsonObject object;
            object.insert("name", key.name);
            object.insert("provider", key.provider);
            object.insert("count", count);
            sent.push_back(object);
        }
        QJsonObject root;
        root.insert("version", 1);
        root.insert("sent", sent);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
        {
            return;
        }
        const auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (file.write(data) != data.size() || !file.commit())
        {
            file.cancelWriting();
        }
    }

    std::mutex mutex_;
    bool loaded_ = false;
    bool personalDirty_ = false;
    Counts personal_;
    QHash<QString, Counts> channels_;
};

Store &store()
{
    static Store value;
    return value;
}

}  // namespace

EmoteUsageStats &EmoteUsageStats::instance()
{
    static EmoteUsageStats value;
    return value;
}

void EmoteUsageStats::observeMessage(const Channel &channel,
                                     const Message &message)
{
    store().observe(channel, message);
}

std::vector<EmoteUsageRow> EmoteUsageStats::personalRows() const
{
    return store().personalRows();
}

std::vector<EmoteUsageRow> EmoteUsageStats::channelRows(
    const QString &channelName, int limit) const
{
    return store().channelRows(channelName, limit);
}

void EmoteUsageStats::resetPersonal()
{
    store().resetPersonal();
}

void EmoteUsageStats::resetChannel(const QString &channelName)
{
    store().resetChannel(channelName);
}

void fillEmoteStatsMenu(QMenu &menu, const QString &channelName)
{
    const auto channel = channelName.trimmed();
    menu.clear();
    if (channel.isEmpty())
    {
        auto *empty =
            menu.addAction(QStringLiteral("Open a Twitch channel first"));
        empty->setEnabled(false);
        return;
    }
    const auto rows = EmoteUsageStats::instance().channelRows(channel, 12);
    if (rows.empty())
    {
        auto *empty = menu.addAction(
            QStringLiteral("No emotes seen in this channel yet"));
        empty->setEnabled(false);
    }
    else
    {
        for (const auto &row : rows)
        {
            auto *action = menu.addAction(QStringLiteral("%1  %2 (%3)")
                                              .arg(row.count)
                                              .arg(row.name, row.provider));
            action->setEnabled(false);
        }
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("Reset channel stats"), [channel] {
        EmoteUsageStats::instance().resetChannel(channel);
    });
}

void addEmoteStatsMenu(QMenu &menu, const QString &channelName)
{
    const auto channel = channelName.trimmed();
    auto *stats = menu.addMenu(QStringLiteral("Show emote stats"));
    QObject::connect(stats, &QMenu::aboutToShow, stats, [stats, channel] {
        fillEmoteStatsMenu(*stats, channel);
    });
}

}  // namespace chatterino
