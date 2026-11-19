// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/chat/BlockedTerms.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <mutex>

using namespace Qt::Literals::StringLiterals;

namespace chatterino {
namespace {

struct CacheEntry {
    QStringList phrases;
    QDateTime fetchedAt;
    bool inFlight = false;
};

std::mutex &cacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

QHash<QString, CacheEntry> &cache()
{
    static QHash<QString, CacheEntry> entries;
    return entries;
}

QStringList localPhrases()
{
    const auto raw = getSettings()->localBlockedTerms.getValue();
    QStringList phrases;
    for (const auto &part :
         raw.split(QRegularExpression(u"[,\\n]"_s), Qt::SkipEmptyParts))
    {
        const auto phrase = part.trimmed();
        if (!phrase.isEmpty())
        {
            phrases.push_back(phrase);
        }
    }
    return phrases;
}

QString matchIn(const QString &haystack, const QStringList &phrases)
{
    const auto lower = haystack.toLower();
    for (const auto &phrase : phrases)
    {
        if (lower.contains(phrase.toLower()))
        {
            return phrase;
        }
    }
    return {};
}

}  // namespace

void prepareChannelBlockedTerms(TwitchChannel *channel)
{
    if (channel == nullptr || (!channel->isMod() && !channel->isBroadcaster()))
    {
        return;
    }

    const auto channelId = channel->roomId();
    if (channelId.isEmpty())
    {
        return;
    }

    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return;
    }
    const auto user = app->getAccounts()->twitch.getCurrent();
    if (user->isAnon())
    {
        return;
    }

    {
        std::lock_guard lock(cacheMutex());
        auto &entry = cache()[channelId];
        if (entry.inFlight)
        {
            return;
        }
        if (entry.fetchedAt.isValid() &&
            entry.fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < 120)
        {
            return;
        }
        entry.inFlight = true;
    }

    const auto token = user->getOAuthToken();
    TwitchGql::getChannelBlockedTerms(
        channelId, token,
        [channelId](QVector<GqlBlockedTerm> terms) {
            const auto now = QDateTime::currentDateTimeUtc();
            QStringList phrases;
            phrases.reserve(terms.size());
            for (const auto &term : terms)
            {
                if (term.phrase.trimmed().isEmpty())
                {
                    continue;
                }
                if (!term.expiresAt.isEmpty())
                {
                    auto expires = QDateTime::fromString(term.expiresAt,
                                                         Qt::ISODateWithMs);
                    if (!expires.isValid())
                    {
                        expires =
                            QDateTime::fromString(term.expiresAt, Qt::ISODate);
                    }
                    if (expires.isValid() && expires.toUTC() <= now)
                    {
                        continue;
                    }
                }
                phrases.push_back(term.phrase.trimmed());
            }

            std::lock_guard lock(cacheMutex());
            auto &entry = cache()[channelId];
            entry.phrases = std::move(phrases);
            entry.fetchedAt = now;
            entry.inFlight = false;
        },
        [channelId](const QString &) {
            std::lock_guard lock(cacheMutex());
            auto &entry = cache()[channelId];
            entry.inFlight = false;
            if (!entry.fetchedAt.isValid())
            {
                entry.fetchedAt = QDateTime::currentDateTimeUtc();
            }
        });
}

QString matchingBlockedTerm(const QString &message, const Channel *channel)
{
    if (auto local = matchIn(message, localPhrases()); !local.isEmpty())
    {
        return local;
    }

    const auto *twitch = dynamic_cast<const TwitchChannel *>(channel);
    if (twitch == nullptr || (!twitch->isMod() && !twitch->isBroadcaster()))
    {
        return {};
    }

    const auto channelId = twitch->roomId();
    if (channelId.isEmpty())
    {
        return {};
    }

    QStringList phrases;
    {
        std::lock_guard lock(cacheMutex());
        phrases = cache().value(channelId).phrases;
    }
    return matchIn(message, phrases);
}

}  // namespace chatterino
