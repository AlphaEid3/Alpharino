#include "providers/twitch/TwitchNameHistory.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/twitch/TwitchNameHistoryCache.hpp"
#include "singletons/Paths.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

namespace chatterino {
namespace detail {

TwitchNameHistoryMemoryCache::TwitchNameHistoryMemoryCache(
    qsizetype maxHistories)
    : maxHistories_(std::max<qsizetype>(1, maxHistories))
{
}

void TwitchNameHistoryMemoryCache::rebuildIndexes()
{
    this->userIds_.clear();
    this->currentLogins_.clear();
    this->historicalLogins_.clear();

    for (const auto &history : this->order_)
    {
        const auto userId = history->userId.trimmed();
        if (!userId.isEmpty())
        {
            this->userIds_.insert(userId, history);
        }

        const auto currentLogin =
            normalizeTwitchNameHistoryLogin(history->currentLogin);
        if (!currentLogin.isEmpty())
        {
            this->currentLogins_.insert(currentLogin, history);
        }

        for (const auto &entry : history->entries)
        {
            const auto historicalLogin =
                normalizeTwitchNameHistoryLogin(entry.login);
            if (!historicalLogin.isEmpty())
            {
                this->historicalLogins_.insert(historicalLogin, history);
            }
        }
    }
}

void TwitchNameHistoryMemoryCache::insert(const TwitchNameHistory &history)
{
    TwitchNameHistoryPtr replaced;
    const auto userId = history.userId.trimmed();
    const auto currentLogin =
        normalizeTwitchNameHistoryLogin(history.currentLogin);
    if (!userId.isEmpty())
    {
        replaced = this->findByUserId(userId);
    }
    else if (!currentLogin.isEmpty())
    {
        const auto existing = std::find_if(
            this->order_.rbegin(), this->order_.rend(),
            [&currentLogin](const auto &candidate) {
                return candidate->userId.trimmed().isEmpty() &&
                       normalizeTwitchNameHistoryLogin(
                           candidate->currentLogin) == currentLogin;
            });
        if (existing != this->order_.rend())
        {
            replaced = *existing;
        }
    }
    if (replaced)
    {
        std::erase(this->order_, replaced);
    }

    auto shared = std::make_shared<TwitchNameHistory>(history);
    this->order_.push_back(shared);
    while (static_cast<qsizetype>(this->order_.size()) > this->maxHistories_)
    {
        this->order_.pop_front();
    }
    this->rebuildIndexes();
}

TwitchNameHistoryPtr TwitchNameHistoryMemoryCache::findByUserId(
    const QString &userId) const
{
    return this->userIds_.value(userId.trimmed());
}

TwitchNameHistoryPtr TwitchNameHistoryMemoryCache::findByLogin(
    const QString &login) const
{
    const auto normalized = normalizeTwitchNameHistoryLogin(login);
    if (const auto current = this->currentLogins_.value(normalized))
    {
        return current;
    }
    return this->historicalLogins_.value(normalized);
}

const std::deque<TwitchNameHistoryPtr> &
    TwitchNameHistoryMemoryCache::histories() const
{
    return this->order_;
}

qsizetype TwitchNameHistoryMemoryCache::size() const
{
    return static_cast<qsizetype>(this->order_.size());
}

}  // namespace detail

namespace {

constexpr QStringView NAME_HISTORY_API =
    u"https://logs.zonian.dev/namehistory/%1";
constexpr int NAME_HISTORY_TIMEOUT_MS = 30000;
constexpr int NAME_HISTORY_CACHE_VERSION = 1;
constexpr int NAME_HISTORY_CACHE_TTL_DAYS = 21;
constexpr qsizetype NAME_HISTORY_MAX_CACHED_USERS = 256;
constexpr qint64 NAME_HISTORY_MAX_CACHE_FILE_BYTES = 4 * 1024 * 1024;
constexpr QStringView NAME_HISTORY_CACHE_FILE = u"twitch-name-history.json";

detail::TwitchNameHistoryMemoryCache &nameHistoryCache()
{
    static detail::TwitchNameHistoryMemoryCache cache(
        NAME_HISTORY_MAX_CACHED_USERS);
    return cache;
}

bool &nameHistoryCacheLoaded()
{
    static bool loaded = false;
    return loaded;
}

QDateTime parseNameHistoryTimestamp(const QString &isoTimestamp)
{
    auto timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODateWithMs);
    if (!timestamp.isValid())
    {
        timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    }
    if (!timestamp.isValid() && isoTimestamp.contains('.'))
    {
        auto trimmed = isoTimestamp;
        const auto dotIndex = trimmed.indexOf('.');
        const auto zoneIndex = trimmed.indexOf('Z', dotIndex);
        if (zoneIndex > dotIndex + 4)
        {
            trimmed = trimmed.left(dotIndex + 4) + trimmed.mid(zoneIndex);
            timestamp = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
        }
    }

    return timestamp;
}

QString formatNameHistoryDate(const QString &isoTimestamp)
{
    const auto timestamp = parseNameHistoryTimestamp(isoTimestamp);
    if (!timestamp.isValid())
    {
        return {};
    }

    return timestamp.toLocalTime().date().toString("MMM d, yyyy");
}

QString formatNameHistoryDateOrUnknown(const QString &isoTimestamp)
{
    const auto date = formatNameHistoryDate(isoTimestamp);
    return date.isEmpty() ? QStringLiteral("Unknown") : date;
}

bool historyMatchesExpectedLogin(const TwitchNameHistory &history,
                                 const QString &expectedCurrentLogin)
{
    const auto expected = normalizeTwitchNameHistoryLogin(expectedCurrentLogin);
    return expected.isEmpty() ||
           (!history.currentLogin.isEmpty() &&
            history.currentLogin.compare(expected, Qt::CaseInsensitive) == 0);
}

bool nameHistoryIsFresh(const TwitchNameHistory &history)
{
    if (!history.fetchedAt.isValid())
    {
        return false;
    }

    const auto now = QDateTime::currentDateTimeUtc();
    if (history.fetchedAt > now.addSecs(5 * 60))
    {
        return false;
    }

    return history.fetchedAt.secsTo(now) <=
           NAME_HISTORY_CACHE_TTL_DAYS * 24 * 60 * 60;
}

TwitchNameHistory parseNameHistory(const QJsonArray &root,
                                   const QString &userId,
                                   const QString &requestedLogin)
{
    TwitchNameHistory result;
    result.userId = userId;
    result.fetchedAt = QDateTime::currentDateTimeUtc();

    QDateTime newestSeenAt;
    for (const auto &value : root)
    {
        const auto object = value.toObject();
        const auto login = normalizeTwitchNameHistoryLogin(
            object.value("user_login").toString());
        if (login.isEmpty())
        {
            continue;
        }

        const auto lastSeenAt = parseNameHistoryTimestamp(
            object.value("last_timestamp").toString());
        if (!newestSeenAt.isValid() ||
            (lastSeenAt.isValid() && lastSeenAt > newestSeenAt))
        {
            newestSeenAt = lastSeenAt;
            result.currentLogin = login;
        }
    }
    if (result.currentLogin.isEmpty())
    {
        result.currentLogin = normalizeTwitchNameHistoryLogin(requestedLogin);
    }

    const auto reserveCount =
        std::min<int>(root.size(), TWITCH_NAME_HISTORY_LIMIT);
    result.entries.reserve(static_cast<size_t>(reserveCount));

    bool addedCurrentEntry = false;
    for (auto i = root.size() - 1; i >= 0; --i)
    {
        const auto object = root.at(i).toObject();
        auto login = normalizeTwitchNameHistoryLogin(
            object.value("user_login").toString());
        if (login.isEmpty())
        {
            continue;
        }

        auto firstSeen = formatNameHistoryDateOrUnknown(
            object.value("first_timestamp").toString());
        const auto lastSeen = formatNameHistoryDateOrUnknown(
            object.value("last_timestamp").toString());

        const bool isCurrentLogin =
            login.compare(result.currentLogin, Qt::CaseInsensitive) == 0;
        if (isCurrentLogin && !addedCurrentEntry)
        {
            addedCurrentEntry = true;
            result.entries.push_back(
                {std::move(login), firstSeen, QStringLiteral("Present")});
        }
        else
        {
            if (i == 0)
            {
                firstSeen = QStringLiteral("Unknown");
            }
            result.entries.push_back({std::move(login), firstSeen, lastSeen});
        }

        if (static_cast<int>(result.entries.size()) >=
            TWITCH_NAME_HISTORY_LIMIT)
        {
            break;
        }
    }

    return result;
}

void insertNameHistory(const TwitchNameHistory &history)
{
    nameHistoryCache().insert(history);
}

QString nameHistoryCachePath()
{
    return getApp()->getPaths().cacheFilePath(
        NAME_HISTORY_CACHE_FILE.toString());
}

TwitchNameHistory historyFromJson(const QJsonObject &object)
{
    TwitchNameHistory history;
    history.userId = object.value("userId").toString().trimmed();
    history.currentLogin = normalizeTwitchNameHistoryLogin(
        object.value("currentLogin").toString());
    history.fetchedAt =
        parseNameHistoryTimestamp(object.value("fetchedAt").toString());

    const auto entries = object.value("entries").toArray();
    history.entries.reserve(static_cast<size_t>(
        std::min<int>(entries.size(), TWITCH_NAME_HISTORY_LIMIT)));

    for (const auto &value : entries)
    {
        const auto entryObject = value.toObject();
        TwitchNameHistoryEntry entry{
            normalizeTwitchNameHistoryLogin(
                entryObject.value("login").toString()),
            entryObject.value("left").toString(),
            entryObject.value("right").toString(),
        };
        if (entry.login.isEmpty() || entry.leftText.isEmpty() ||
            entry.rightText.isEmpty())
        {
            continue;
        }

        history.entries.push_back(std::move(entry));
        if (static_cast<int>(history.entries.size()) >=
            TWITCH_NAME_HISTORY_LIMIT)
        {
            break;
        }
    }

    return history;
}

QJsonObject historyToJson(const TwitchNameHistory &history)
{
    QJsonArray entries;
    for (const auto &entry : history.entries)
    {
        QJsonObject entryObject;
        entryObject.insert("login", entry.login);
        entryObject.insert("left", entry.leftText);
        entryObject.insert("right", entry.rightText);
        entries.push_back(entryObject);
    }

    QJsonObject object;
    object.insert("userId", history.userId);
    object.insert("currentLogin", history.currentLogin);
    object.insert("fetchedAt",
                  history.fetchedAt.toUTC().toString(Qt::ISODateWithMs));
    object.insert("entries", entries);
    return object;
}

void loadNameHistoryCache()
{
    auto &loaded = nameHistoryCacheLoaded();
    if (loaded)
    {
        return;
    }
    loaded = true;

    QFile file(nameHistoryCachePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    if (file.size() > NAME_HISTORY_MAX_CACHE_FILE_BYTES)
    {
        return;
    }

    QJsonParseError parseError;
    const auto data = file.read(NAME_HISTORY_MAX_CACHE_FILE_BYTES + 1);
    if (data.size() > NAME_HISTORY_MAX_CACHE_FILE_BYTES)
    {
        return;
    }
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return;
    }

    const auto root = document.object();
    if (root.value("version").toInt() != NAME_HISTORY_CACHE_VERSION)
    {
        return;
    }

    for (const auto &value : root.value("histories").toArray())
    {
        auto history = historyFromJson(value.toObject());
        if ((history.userId.isEmpty() && history.currentLogin.isEmpty()) ||
            !nameHistoryIsFresh(history))
        {
            continue;
        }

        insertNameHistory(history);
    }
}

void saveNameHistoryCache()
{
    QJsonArray histories;

    for (const auto &historyPtr : nameHistoryCache().histories())
    {
        if (historyPtr == nullptr)
        {
            continue;
        }

        const auto &history = *historyPtr;
        if (!nameHistoryIsFresh(history))
        {
            continue;
        }

        histories.push_back(historyToJson(history));
    }

    QJsonObject root;
    root.insert("version", NAME_HISTORY_CACHE_VERSION);
    root.insert("histories", histories);

    QSaveFile file(nameHistoryCachePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        return;
    }
    const auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() > NAME_HISTORY_MAX_CACHE_FILE_BYTES ||
        file.write(data) != data.size())
    {
        file.cancelWriting();
        return;
    }
    file.commit();
}

void cacheNameHistory(const TwitchNameHistory &history)
{
    loadNameHistoryCache();
    insertNameHistory(history);
    saveNameHistoryCache();
}

}  // namespace

QString normalizeTwitchNameHistoryLogin(const QString &login)
{
    return login.trimmed().toLower();
}

std::optional<TwitchNameHistory> getCachedTwitchNameHistory(
    const QString &userId, const QString &expectedCurrentLogin)
{
    loadNameHistoryCache();

    const auto trimmedUserId = userId.trimmed();
    const auto &cache = nameHistoryCache();
    if (!trimmedUserId.isEmpty())
    {
        if (const auto history = cache.findByUserId(trimmedUserId);
            history != nullptr && nameHistoryIsFresh(*history) &&
            historyMatchesExpectedLogin(*history, expectedCurrentLogin))
        {
            return *history;
        }

        return std::nullopt;
    }
    if (!expectedCurrentLogin.trimmed().isEmpty())
    {
        if (const auto history = cache.findByLogin(expectedCurrentLogin);
            history != nullptr && nameHistoryIsFresh(*history) &&
            historyMatchesExpectedLogin(*history, expectedCurrentLogin))
        {
            return *history;
        }
    }

    return std::nullopt;
}

void fetchTwitchNameHistoryByUserId(
    const QString &userId, const QString &requestedLogin,
    std::function<void(TwitchNameHistory)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    const auto trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
    {
        failureCallback("Missing user id");
        return;
    }

    const auto encodedUserId =
        QString::fromUtf8(QUrl::toPercentEncoding(trimmedUserId));
    const auto url = QUrl(NAME_HISTORY_API.toString().arg(encodedUserId));

    NetworkRequest(url)
        .timeout(NAME_HISTORY_TIMEOUT_MS)
        .maximumResponseSize(NAME_HISTORY_MAX_CACHE_FILE_BYTES)
        .maximumRedirectsAllowed(3)
        .followRedirects(true)
        .header("Accept", "application/json")
        .header("User-Agent", "Moltorino")
        .onSuccess([trimmedUserId, requestedLogin,
                    successCallback = std::move(successCallback),
                    failureCallback](const NetworkResult &result) mutable {
            const auto rootValue = result.parseJsonValue();
            if (!rootValue.isArray())
            {
                failureCallback("Unexpected name history response");
                return;
            }

            const auto root = rootValue.toArray();
            if (root.isEmpty())
            {
                TwitchNameHistory empty;
                empty.userId = trimmedUserId;
                empty.currentLogin =
                    normalizeTwitchNameHistoryLogin(requestedLogin);
                empty.fetchedAt = QDateTime::currentDateTimeUtc();
                cacheNameHistory(empty);
                successCallback(std::move(empty));
                return;
            }

            auto history =
                parseNameHistory(root, trimmedUserId, requestedLogin);
            cacheNameHistory(history);
            successCallback(std::move(history));
        })
        .onError([failureCallback =
                      std::move(failureCallback)](const NetworkResult &result) {
            failureCallback(result.formatError());
        })
        .execute();
}

namespace {

constexpr int kLocalNameLimit = 20;
constexpr QStringView kLocalNameFile = u"twitch-seen-names.json";

struct LocalPreviousName {
    QString login;
    QString display;
    QDateTime from;
    QDateTime until;
};

struct LocalNameRecord {
    QString login;
    QString display;
    QDateTime since;
    std::vector<LocalPreviousName> previous;
};

std::mutex &localNameMutex()
{
    static std::mutex mutex;
    return mutex;
}

QHash<QString, LocalNameRecord> &localNames()
{
    static QHash<QString, LocalNameRecord> records;
    return records;
}

bool &localNamesLoaded()
{
    static bool loaded = false;
    return loaded;
}

QString localNamePath()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return {};
    }
    return app->getPaths().settingsDirectory + QLatin1Char('/') +
           kLocalNameFile.toString();
}

QString formatLocalDate(const QDateTime &time)
{
    if (!time.isValid())
    {
        return QStringLiteral("Unknown");
    }
    return time.toLocalTime().date().toString(QStringLiteral("MMM d, yyyy"));
}

void saveLocalNamesLocked()
{
    const auto path = localNamePath();
    if (path.isEmpty())
    {
        return;
    }

    QJsonArray users;
    for (auto it = localNames().cbegin(); it != localNames().cend(); ++it)
    {
        QJsonArray previous;
        for (const auto &entry : it->previous)
        {
            QJsonObject object;
            object.insert(QStringLiteral("login"), entry.login);
            object.insert(QStringLiteral("display"), entry.display);
            object.insert(QStringLiteral("from"),
                          entry.from.toUTC().toString(Qt::ISODateWithMs));
            object.insert(QStringLiteral("until"),
                          entry.until.toUTC().toString(Qt::ISODateWithMs));
            previous.push_back(object);
        }

        QJsonObject user;
        user.insert(QStringLiteral("id"), it.key());
        user.insert(QStringLiteral("login"), it->login);
        user.insert(QStringLiteral("display"), it->display);
        user.insert(QStringLiteral("since"),
                    it->since.toUTC().toString(Qt::ISODateWithMs));
        user.insert(QStringLiteral("previous"), previous);
        users.push_back(user);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("users"), users);
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

void loadLocalNamesLocked()
{
    if (localNamesLoaded())
    {
        return;
    }
    localNamesLoaded() = true;

    QFile file(localNamePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        return;
    }
    for (const auto &value : document.object().value(u"users"_s).toArray())
    {
        const auto object = value.toObject();
        const auto id = object.value(u"id"_s).toString().trimmed();
        if (id.isEmpty())
        {
            continue;
        }
        LocalNameRecord record;
        record.login = normalizeTwitchNameHistoryLogin(
            object.value(u"login"_s).toString());
        record.display = object.value(u"display"_s).toString().trimmed();
        record.since =
            parseNameHistoryTimestamp(object.value(u"since"_s).toString());
        for (const auto &previousValue : object.value(u"previous"_s).toArray())
        {
            const auto previousObject = previousValue.toObject();
            LocalPreviousName previous;
            previous.login = normalizeTwitchNameHistoryLogin(
                previousObject.value(u"login"_s).toString());
            previous.display =
                previousObject.value(u"display"_s).toString().trimmed();
            previous.from = parseNameHistoryTimestamp(
                previousObject.value(u"from"_s).toString());
            previous.until = parseNameHistoryTimestamp(
                previousObject.value(u"until"_s).toString());
            if (previous.login.isEmpty() && previous.display.isEmpty())
            {
                continue;
            }
            record.previous.push_back(std::move(previous));
        }
        localNames().insert(id, std::move(record));
    }
}

QString sightingLabel(const QString &login, const QString &display)
{
    if (display.isEmpty() || display.compare(login, Qt::CaseInsensitive) == 0)
    {
        return login;
    }
    return display + QStringLiteral(" (") + login + u')';
}

}  // namespace

void noteTwitchIdentity(const QString &userId, const QString &login,
                        const QString &displayName)
{
    const auto id = userId.trimmed();
    const auto normalized = normalizeTwitchNameHistoryLogin(login);
    if (id.isEmpty() || normalized.isEmpty())
    {
        return;
    }
    const auto display = displayName.trimmed();

    if (auto *application = QCoreApplication::instance())
    {
        static bool flushOnQuit = false;
        if (!flushOnQuit)
        {
            flushOnQuit = true;
            QObject::connect(application, &QCoreApplication::aboutToQuit,
                             application, [] {
                                 std::lock_guard lock(localNameMutex());
                                 if (localNamesLoaded())
                                 {
                                     saveLocalNamesLocked();
                                 }
                             });
        }
    }

    std::lock_guard lock(localNameMutex());
    loadLocalNamesLocked();
    auto &record = localNames()[id];
    const auto now = QDateTime::currentDateTimeUtc();
    if (record.since.isNull() && record.login.isEmpty())
    {
        record.login = normalized;
        record.display = display;
        record.since = now;
        static int pendingNewUsers = 0;
        if (++pendingNewUsers >= 30)
        {
            pendingNewUsers = 0;
            saveLocalNamesLocked();
        }
        return;
    }

    const bool loginChanged =
        record.login.compare(normalized, Qt::CaseInsensitive) != 0;
    const bool displayChanged = !display.isEmpty() &&
                                !record.display.isEmpty() &&
                                record.display != display;
    if (!loginChanged && !displayChanged)
    {
        if (record.display.isEmpty() && !display.isEmpty())
        {
            record.display = display;
            saveLocalNamesLocked();
        }
        return;
    }

    LocalPreviousName previous;
    previous.login = record.login;
    previous.display = record.display;
    previous.from = record.since;
    previous.until = now;
    record.previous.push_back(std::move(previous));
    if (static_cast<int>(record.previous.size()) > kLocalNameLimit)
    {
        record.previous.erase(record.previous.begin());
    }
    record.login = normalized;
    if (!display.isEmpty())
    {
        record.display = display;
    }
    record.since = now;
    saveLocalNamesLocked();
}

std::vector<LocalTwitchNameSighting> localTwitchNameSightings(
    const QString &userId)
{
    const auto id = userId.trimmed();
    if (id.isEmpty())
    {
        return {};
    }

    std::lock_guard lock(localNameMutex());
    loadLocalNamesLocked();
    const auto it = localNames().constFind(id);
    if (it == localNames().cend())
    {
        return {};
    }

    std::vector<LocalTwitchNameSighting> sightings;
    sightings.reserve(it->previous.size());
    for (auto index = static_cast<int>(it->previous.size()) - 1; index >= 0;
         --index)
    {
        const auto &previous = it->previous[static_cast<size_t>(index)];
        LocalTwitchNameSighting sighting;
        sighting.login = previous.login;
        sighting.name = sightingLabel(previous.login, previous.display);
        sighting.fromText = formatLocalDate(previous.from);
        sighting.untilText = formatLocalDate(previous.until);
        sightings.push_back(std::move(sighting));
    }
    return sightings;
}

}  // namespace chatterino
