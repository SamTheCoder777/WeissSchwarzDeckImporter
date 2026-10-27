#include "SeriesRepository.h"

#include <QJsonArray>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <memory>

#include "../network/NetGuard.h"

SeriesRepository::SeriesRepository(QObject *parent) : QObject(parent)
{
    ensureSeriesSchema(Region::JP);
    ensureSeriesSchema(Region::EN);
    ensureCardSchema(Region::JP);
    ensureCardSchema(Region::EN);
    restoreOutdatedState();
    connect(&Config::instance(), &Config::detectLocaleModeChanged, this, [this] {
        const QString interrupted = activeCardId_;
        invalidateNetwork();
        if (!interrupted.isEmpty()) emit cardDownloadFinished(interrupted, false);
        restoreOutdatedState();
        setStatus("Ready.");
        emit busyChanged();
        emit checkStateChanged();
        emit updateAllStateChanged();
        emit seriesListUpdated();
        QTimer::singleShot(0, this, &SeriesRepository::checkAutomaticUpdates);
    });
}

SeriesRepository::~SeriesRepository()
{
    if (!QCoreApplication::closingDown()) invalidateNetwork();
}

QSqlDatabase SeriesRepository::seriesDb(Region region) const
{
    const QString name = "series_catalog_connection_" + regionTag(region);
    const QString path = Config::instance().getSeriesListDatabasePath(region);
    QSqlDatabase db = QSqlDatabase::contains(name)
        ? QSqlDatabase::database(name, false) : QSqlDatabase::addDatabase("QSQLITE", name);
    if (db.databaseName() != path) {
        if (db.isOpen()) db.close();
        db.setDatabaseName(path);
    }
    if (!db.isOpen() && !db.open()) qCritical() << path << db.lastError().text();
    return db;
}

QSqlDatabase SeriesRepository::cardDb(Region region) const
{
    const QString name = "cardlist_catalog_connection_" + regionTag(region);
    const QString path = Config::instance().getCardListDatabasePath(region);
    QSqlDatabase db = QSqlDatabase::contains(name)
        ? QSqlDatabase::database(name, false) : QSqlDatabase::addDatabase("QSQLITE", name);
    if (db.databaseName() != path) {
        if (db.isOpen()) db.close();
        db.setDatabaseName(path);
    }
    if (!db.isOpen() && !db.open()) qCritical() << path << db.lastError().text();
    return db;
}

void SeriesRepository::ensureSeriesSchema(Region region)
{
    QSqlQuery query(seriesDb(region));
    if (!query.exec("CREATE TABLE IF NOT EXISTS series (id TEXT PRIMARY KEY, "
                    "set_code TEXT, name TEXT, hash TEXT)"))
        qCritical() << query.lastError().text();
}

void SeriesRepository::ensureCardSchema(Region region)
{
    QSqlQuery query(cardDb(region));
    const QStringList statements
        = {"CREATE TABLE IF NOT EXISTS cards (series_id TEXT, card_id TEXT, "
           "cardcode TEXT, data TEXT, PRIMARY KEY(series_id, card_id))",
           "CREATE INDEX IF NOT EXISTS idx_cards_cardcode ON cards(cardcode)",
           "CREATE INDEX IF NOT EXISTS idx_cards_series ON cards(series_id)",
           "CREATE INDEX IF NOT EXISTS idx_cards_cardcode_lower ON cards(LOWER(cardcode))",
           "CREATE TABLE IF NOT EXISTS card_update_state "
           "(series_id TEXT PRIMARY KEY, outdated INTEGER NOT NULL DEFAULT 0)"};
    for (const auto &sql : statements)
        if (!query.exec(sql)) qCritical() << query.lastError().text();
}

bool SeriesRepository::hasLocalSeries(Region region) const
{
    QSqlQuery query(seriesDb(region));
    return query.exec("SELECT 1 FROM series LIMIT 1") && query.next();
}

bool SeriesRepository::hasLocalCards(Region region, const QString &id) const
{
    QSqlQuery query(cardDb(region));
    query.prepare("SELECT 1 FROM cards WHERE series_id = ? UNION ALL "
                  "SELECT 1 FROM card_update_state WHERE series_id = ? LIMIT 1");
    query.addBindValue(id);
    query.addBindValue(id);
    return query.exec() && query.next();
}

QVector<SeriesRepository::SeriesRow> SeriesRepository::loadSeries()
{
    const Region region = currentRegion();
    QSet<QString> downloaded;
    QSet<QString> outdated;
    QSqlQuery cards(cardDb(region));
    if (cards.exec("SELECT DISTINCT series_id FROM cards"))
        while (cards.next()) downloaded.insert(cards.value(0).toString());
    if (cards.exec("SELECT series_id, outdated FROM card_update_state")) {
        while (cards.next()) {
            const QString id = cards.value(0).toString();
            downloaded.insert(id);
            if (cards.value(1).toBool()) outdated.insert(id);
        }
    }
    QVector<SeriesRow> rows;
    QSqlQuery series(seriesDb(region));
    if (series.exec("SELECT id,set_code,name,hash FROM series ORDER BY name")) {
        while (series.next()) {
            SeriesRow row;
            row.id = series.value(0).toString();
            row.set = series.value(1).toString();
            row.name = series.value(2).toString();
            row.hash = series.value(3).toString();
            row.hasCardList = downloaded.contains(row.id);
            row.updateAvailable = outdated.contains(row.id);
            rows.append(row);
        }
    }
    return rows;
}

void SeriesRepository::restoreOutdatedState()
{
    outdated_.clear();
    for (const auto &row : loadSeries())
        if (row.hasCardList && row.updateAvailable) outdated_.append(row.id);
}

bool SeriesRepository::saveSeriesToDb(Region region,
    const QVector<SeriesRow> &rows, QString *error)
{
    QSqlDatabase db = seriesDb(region);
    if (!db.transaction()) { *error = db.lastError().text(); return false; }
    QSqlQuery query(db);
    auto fail = [&] { *error = query.lastError().text(); db.rollback(); return false; };
    if (!query.exec("DELETE FROM series")) return fail();
    if (!query.prepare("INSERT INTO series(id,set_code,name,hash) VALUES(?,?,?,?)"))
        return fail();
    for (const auto &row : rows) {
        query.bindValue(0, row.id);
        query.bindValue(1, row.set);
        query.bindValue(2, row.name);
        query.bindValue(3, row.hash);
        if (!query.exec()) return fail();
    }
    if (!db.commit()) { *error = db.lastError().text(); db.rollback(); return false; }
    return true;
}

bool SeriesRepository::storeCardUpdateState(Region region, const QString &id,
    bool outdated, QString *error)
{
    QSqlQuery query(cardDb(region));
    query.prepare("INSERT OR REPLACE INTO card_update_state(series_id,outdated) VALUES(?,?)");
    query.addBindValue(id);
    query.addBindValue(outdated ? 1 : 0);
    if (!query.exec()) { *error = query.lastError().text(); return false; }
    return true;
}

bool SeriesRepository::importCardListJson(Region region, const QString &seriesId,
    const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        *error = "Card list must be a JSON array.";
        return false;
    }

    QSet<QString> ids;
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        const QString id = object.value("_id").toString();
        if (!value.isObject() || id.isEmpty() || ids.contains(id)) {
            *error = "Card list contains a missing or duplicate _id.";
            return false;
        }
        ids.insert(id);
    }
    QSqlDatabase db = cardDb(region);
    if (!db.transaction()) {
        *error = db.lastError().text();
        return false;
    }
    QSqlQuery query(db);
    auto fail = [&] { *error = query.lastError().text(); db.rollback(); return false; };
    query.prepare("DELETE FROM cards WHERE series_id = ?");
    query.addBindValue(seriesId);
    if (!query.exec()) return fail();
    if (!query.prepare("INSERT INTO cards(series_id,card_id,cardcode,data) VALUES(?,?,?,?)"))
        return fail();
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        query.bindValue(0, seriesId);
        query.bindValue(1, object.value("_id").toString());
        query.bindValue(2, object.value("cardcode").toString());
        query.bindValue(3, QString::fromUtf8(
            QJsonDocument(object).toJson(QJsonDocument::Compact)));
        if (!query.exec()) return fail();
    }
    query.prepare("INSERT OR REPLACE INTO card_update_state(series_id,outdated) VALUES(?,0)");
    query.addBindValue(seriesId);
    if (!query.exec()) return fail();
    if (!db.commit()) { *error = db.lastError().text(); db.rollback(); return false; }
    return true;
}

QString SeriesRepository::checkKey(const QString &kind, Region region,
    const QString &identity) const
{
    return kind + ":" + regionTag(region) + ":" + identity;
}

bool SeriesRepository::serverAllowed(const QUrl &url)
{
    const qint64 wait = NetGuard::instance().blockedForMs(url);
    if (wait <= 0) return true;
    setStatus("Server cooldown · try again in " + NetGuard::minsText(wait));
    return false;
}

bool SeriesRepository::checkAllowed(const QString &key, const QUrl &url, bool manual)
{
    const qint64 wait = Config::instance().updateCheckWaitSeconds(key, manual);
    if (wait > 0) {
        setStatus(QString("Check skipped · available in %1 min.").arg((wait + 59) / 60));
        return false;
    }
    return serverAllowed(url);
}

void SeriesRepository::checkAutomaticUpdates()
{
    refreshSeriesList(currentRegion(), false);
    checkCardListUpdates(false);
}

void SeriesRepository::checkForUpdates(Region region, bool manual)
{
    const int regionId = int(region);
    if (seriesHeadIds_.contains(regionId)) return;
    const QUrl url(Config::instance().getSeriesListUrl(region));

    const QString key = checkKey("series-head", region, url.toString(QUrl::FullyEncoded));
    if (!checkAllowed(key, url, manual)) {
        emit updateAvailable(region, UpdateStatus::Error);
        return;
    }
    const quint64 epoch = epoch_;
    NetRequest request;
    request.url = url;
    request.key = key;
    request.onStarted = [this, epoch, key] {
        if (epoch == epoch_) Config::instance().recordUpdateCheck(key);
    };
    setStatus(regionTag(region) + ": checking for series updates…");
    seriesHeadIds_.insert(regionId, NetGuard::instance().head(request, this,
        [this, epoch, region, regionId](const NetResult &result) {
            if (epoch != epoch_) return;
            seriesHeadIds_.remove(regionId);
            if (!result.ok()) {
                setStatus(result.message + (hasLocalSeries(region)
                    ? " · using local series list." : " · no local series list."));
                emit updateAvailable(region, UpdateStatus::Error);
                return;
            }
            const QString remote = QString::fromLatin1(result.header("ETag"));
            const QString local = Config::instance().getSeriesListEtag(region);
            const bool upToDate = hasLocalSeries(region) && !remote.isEmpty() && remote == local;
            setStatus(regionTag(region) + (upToDate
                ? ": series list is up to date." : ": series list download available."));
            emit updateAvailable(region, upToDate
                ? UpdateStatus::UpToDate : UpdateStatus::UpdateAvailable);
        }));
}

void SeriesRepository::refreshSeriesList(Region region, bool manual)
{
    if (isBusy() || updatingAll_) return;
    const QUrl url(Config::instance().getSeriesListUrl(region));
    const QString key = checkKey("series-fetch", region, url.toString(QUrl::FullyEncoded));
    if (!checkAllowed(key, url, manual)) return;
    const quint64 epoch = epoch_;
    NetRequest request;
    request.url = url;
    request.key = key;
    const QString etag = Config::instance().getSeriesListEtag(region);
    if (hasLocalSeries(region) && !etag.isEmpty()) {
        request.headers.insert("If-None-Match", etag.toLatin1());
        request.allowNotModified = true;
    }
    request.validate = [](const QByteArray &body) {
        return QJsonDocument::fromJson(body).isArray();
    };
    request.onStarted = [this, epoch, key] {
        if (epoch == epoch_) Config::instance().recordUpdateCheck(key);
    };
    request.progress = [this, epoch, region](qint64 received, qint64 total) {
        if (epoch == epoch_) emit seriesProgress(region, received, total);
    };
    setStatus(regionTag(region) + ": checking series list…");
    requestId_ = NetGuard::instance().get(request, this,
        [this, epoch, region](const NetResult &result) {
            if (epoch != epoch_) return;
            QString error;
            bool success = result.ok();
            if (!success) error = result.message;
            if (success && result.status != NetResult::NotModified) {
                QVector<SeriesRow> parsed;
                QSet<QString> ids;
                for (const auto &value : QJsonDocument::fromJson(result.body).array()) {
                    if (!value.isObject()) { error = "Series entry is not an object."; break; }
                    const auto object = value.toObject();
                    if (!object.value("enabled").toBool(true)) continue;
                    SeriesRow row;
                    row.id = object.value("_id").toString();
                    row.set = object.value("set").toString();
                    row.name = object.value("name").toString();
                    row.hash = object.value("hash").toString();
                    if (row.id.isEmpty() || ids.contains(row.id)) {
                        error = "Series list contains a missing or duplicate _id.";
                        break;
                    }
                    ids.insert(row.id);
                    parsed.append(row);
                }
                success = error.isEmpty() && saveSeriesToDb(region, parsed, &error);
                if (success) Config::instance().setSeriesListEtag(
                    region, QString::fromLatin1(result.header("ETag")));
            }
            requestId_ = 0;
            emit busyChanged();
            if (!success) {
                setStatus("Series list update failed: " + error);
                emit updateAvailable(region, UpdateStatus::Error);
                emit errorOccurred(error);
                return;
            }
            restoreOutdatedState();
            setStatus(regionTag(region) + ": series list is up to date.");
            emit seriesListUpdated();
            emit updateAvailable(region, UpdateStatus::UpToDate);

            QTimer::singleShot(0, this, [this, epoch] {
                if (epoch == epoch_) checkCardListUpdates(false);
            });
        });
    emit busyChanged();
}

void SeriesRepository::downloadCardList(const QString &seriesId)
{
    if (isBusy())
        return;

    if (checkingCards_) {
        setStatus("Wait for the current card update check to finish.");
        emit cardDownloadFinished(seriesId, false);
        return;
    }

    const Region region = currentRegion();

    const QString cooldownKey = checkKey(QStringLiteral("item-download:cards"), region, seriesId);

    const qint64 waitSeconds = Config::instance().updateCheckWaitSeconds(cooldownKey, true);

    if (waitSeconds > 0) {
        setStatus(QString("Download cooldown for %1 · try again in %2")
                      .arg(seriesId, NetGuard::minsText(waitSeconds * 1000)));

        emit cardDownloadFinished(seriesId, false);

        if (updatingAll_) {
            QTimer::singleShot(0, this, &SeriesRepository::pumpUpdateQueue);
        }

        return;
    }

    QString mutableId = seriesId;

    const QUrl url(Config::instance().getCardListUrl(mutableId));

    if (!serverAllowed(url)) {
        stopUpdateBatch();
        emit cardDownloadFinished(seriesId, false);
        return;
    }

    const quint64 epoch = epoch_;

    NetRequest request;
    request.url = url;

    request.key = checkKey(QStringLiteral("card-fetch"), region, url.toString(QUrl::FullyEncoded));

    const QString etag = Config::instance().getCardListEtag(mutableId);

    if (hasLocalCards(region, seriesId) && !etag.isEmpty()) {
        request.headers.insert("If-None-Match", etag.toLatin1());

        request.allowNotModified = true;
    }

    request.validate = [](const QByteArray &body) {
        return QJsonDocument::fromJson(body).isArray();
    };

    request.onStarted = [this, epoch, cooldownKey] {
        if (epoch == epoch_) {
            Config::instance().recordUpdateCheck(cooldownKey);
        }
    };

    request.progress = [this, epoch, seriesId](qint64 received, qint64 total) {
        if (epoch != epoch_)
            return;

        const double progress = total > 0 ? qMin(0.99, double(received) / double(total)) : 0.0;

        emit cardProgress(seriesId, progress);
    };

    activeCardId_ = seriesId;
    setStatus("Downloading cards…");

    requestId_ = NetGuard::instance()
                     .get(request, this, [this, epoch, region, seriesId](const NetResult &result) {
                         if (epoch != epoch_)
                             return;

                         QString error;
                         bool success = result.ok();

                         if (!success)
                             error = result.message;

                         if (success && result.status == NetResult::NotModified) {
                             success = storeCardUpdateState(region, seriesId, false, &error);
                         } else if (success) {
                             success = importCardListJson(region, seriesId, result.body, &error);

                             if (success) {
                                 Config::instance().setCardListEtag(seriesId,
                                                                    QString::fromLatin1(
                                                                        result.header("ETag")));
                             }
                         }

                         requestId_ = 0;
                         activeCardId_.clear();

                         emit busyChanged();
                         emit cardDownloadFinished(seriesId, success);

                         if (success) {
                             outdated_.removeAll(seriesId);

                             emit cardListUpdateState(seriesId, false);
                             emit cardProgress(seriesId, 1.0);

                             setStatus("Cards downloaded and imported.");
                             emit cardListDownloaded(seriesId);
                         } else {
                             if (result.retryInMs > 0 || result.status == NetResult::Cancelled) {
                                 stopUpdateBatch();
                             }

                             setStatus("Card download failed: " + error);

                             if (result.status != NetResult::Cancelled)
                                 emit errorOccurred(error);
                         }

                         if (updatingAll_) {
                             QTimer::singleShot(0, this, &SeriesRepository::pumpUpdateQueue);
                         }
                     });

    emit busyChanged();
    emit cardProgress(seriesId, 0.0);
}

void SeriesRepository::checkCardListUpdates(bool manual)
{
    if (checkingCards_ || updatingAll_ || !activeCardId_.isEmpty()) return;
    QStringList candidates;
    for (const auto &row : loadSeries())
        if (row.hasCardList) candidates.append(row.id);
    if (candidates.isEmpty()) {
        setStatus("No downloaded card lists to check.");
        emit cardListCheckFinished(outdated_.size());
        return;
    }
    QString first = candidates.first();
    const QUrl url(Config::instance().getCardListUrl(first));
    const QString key = checkKey("card-checks", currentRegion(),
        Config::instance().getCardListDatabasePath(currentRegion()));
    if (!checkAllowed(key, url, manual))
        return;

    cardCheckQueue_ = candidates;
    cardBatchKey_ = key;
    cardBatchRecorded_ = false;
    cardCheckHadErrors_ = false;
    checkingCards_ = true;
    setStatus(QString("Checking %1 card list(s)…").arg(candidates.size()));
    emit checkStateChanged();
    pumpCardCheckQueue();
}

void SeriesRepository::pumpCardCheckQueue()
{
    if (!checkingCards_) return;
    while (cardCheckActive_ < 4 && !cardCheckQueue_.isEmpty()) {
        QString id = cardCheckQueue_.takeFirst();
        const Region region = currentRegion();
        const QUrl url(Config::instance().getCardListUrl(id));
        if (!serverAllowed(url)) {
            cardCheckHadErrors_ = true;
            cardCheckQueue_.clear();
            break;
        }
        const quint64 epoch = epoch_;
        NetRequest request;
        request.url = url;
        request.key = checkKey("card-head", region, url.toString(QUrl::FullyEncoded));
        const QString stored = Config::instance().getCardListEtag(id);
        if (!stored.isEmpty()) request.headers.insert("If-None-Match", stored.toLatin1());
        request.onStarted = [this, epoch] {
            if (epoch == epoch_ && !cardBatchRecorded_) {
                cardBatchRecorded_ = true;
                Config::instance().recordUpdateCheck(cardBatchKey_);
            }
        };

        const auto token = std::make_shared<quint64>(0);
        ++cardCheckActive_;
        *token = NetGuard::instance().head(request, this,
            [this, epoch, region, id, stored, token](const NetResult &result) {
                if (epoch != epoch_) return;
                cardCheckIds_.remove(*token);
                --cardCheckActive_;
                QString error;
                bool known = result.ok();
                bool outdated = false;
                if (known && result.status != NetResult::NotModified) {
                    const QString remote = QString::fromLatin1(result.header("ETag"));
                    if (remote.isEmpty())
                        known = false;
                    else outdated = stored.isEmpty() || remote != stored;
                }
                if (known) known = storeCardUpdateState(region, id, outdated, &error);
                if (known) {
                    if (outdated && !outdated_.contains(id)) outdated_.append(id);
                    if (!outdated) outdated_.removeAll(id);
                    emit cardListUpdateState(id, outdated);
                } else {
                    cardCheckHadErrors_ = true;
                    if (result.retryInMs > 0) cardCheckQueue_.clear();
                    qWarning() << "Card update check incomplete:" << id
                               << result.message << error;
                }
                pumpCardCheckQueue();
            });
        cardCheckIds_.insert(*token);
    }
    if (cardCheckQueue_.isEmpty() && cardCheckActive_ == 0) finishCardChecks();
}

void SeriesRepository::finishCardChecks()
{
    checkingCards_ = false;
    if (cardCheckHadErrors_)
        setStatus(QString("Update check incomplete · %1 known update(s).")
                      .arg(outdated_.size()));
    else
        setStatus(outdated_.isEmpty() ? "All card lists are up to date."
            : QString("%1 card list update(s) available.").arg(outdated_.size()));
    emit checkStateChanged();
    emit cardListCheckFinished(outdated_.size());
}

void SeriesRepository::stopUpdateBatch()
{
    updateQueue_.clear();
    const bool wasUpdating = updatingAll_;
    updatingAll_ = false;
    if (wasUpdating) emit updateAllStateChanged();
}

void SeriesRepository::updateOutdatedCardLists()
{
    if (isBusy() || checkingCards_ || updatingAll_ || outdated_.isEmpty()) return;
    updateQueue_ = outdated_;
    updatingAll_ = true;
    emit updateAllStateChanged();
    pumpUpdateQueue();
}

void SeriesRepository::pumpUpdateQueue()
{
    if (!updatingAll_ || isBusy()) return;
    if (updateQueue_.isEmpty()) {
        stopUpdateBatch();
        setStatus(outdated_.isEmpty() ? "All card lists updated."
            : QString("%1 card list(s) could not be updated.").arg(outdated_.size()));
        return;
    }
    const QString id = updateQueue_.takeFirst();
    downloadCardList(id);
}

void SeriesRepository::invalidateNetwork()
{
    ++epoch_;
    const quint64 foreground = requestId_;
    const auto heads = seriesHeadIds_;
    const auto cardChecks = cardCheckIds_;
    requestId_ = 0;
    activeCardId_.clear();
    seriesHeadIds_.clear();
    cardCheckIds_.clear();
    cardCheckQueue_.clear();
    cardCheckActive_ = 0;
    checkingCards_ = false;
    updateQueue_.clear();
    updatingAll_ = false;
    auto &guard = NetGuard::instance();
    if (foreground) guard.cancel(foreground);
    for (const auto id : heads) guard.cancel(id);
    for (const auto id : cardChecks) guard.cancel(id);
}

void SeriesRepository::cancel()
{
    const QString interrupted = activeCardId_;
    invalidateNetwork();
    if (!interrupted.isEmpty()) emit cardDownloadFinished(interrupted, false);
    setStatus("Cancelled.");
    emit busyChanged();
    emit checkStateChanged();
    emit updateAllStateChanged();
}

void SeriesRepository::resetSeries(Region region)
{
    if (region == currentRegion()) cancel();
    QSqlQuery query(seriesDb(region));
    if (!query.exec("DELETE FROM series")) { emit errorOccurred(query.lastError().text()); return; }
    Config::instance().setSeriesListEtag(region, "");
    const QUrl url(Config::instance().getSeriesListUrl(region));
    Config::instance().clearUpdateCheck(checkKey("series-fetch", region,
        url.toString(QUrl::FullyEncoded)));
    Config::instance().clearUpdateCheck(checkKey("series-head", region,
        url.toString(QUrl::FullyEncoded)));
    if (region == currentRegion()) {
        restoreOutdatedState();
        emit seriesListUpdated();
    }
    setStatus(regionTag(region) + ": series list reset.");
    emit updateAvailable(region, UpdateStatus::UpdateAvailable);
}

void SeriesRepository::resetCards(Region region)
{
    if (region == currentRegion()) cancel();
    QSqlDatabase db = cardDb(region);
    if (!db.transaction()) { emit errorOccurred(db.lastError().text()); return; }
    QSqlQuery query(db);
    if (!query.exec("DELETE FROM cards") || !query.exec("DELETE FROM card_update_state")) {
        const auto error = query.lastError().text();
        db.rollback(); emit errorOccurred(error); return;
    }
    if (!db.commit()) { const auto error = db.lastError().text(); db.rollback(); emit errorOccurred(error); return; }
    Config::instance().clearCardListEtags(region);
    Config::instance().clearUpdateCheck(checkKey("card-checks", region,
        Config::instance().getCardListDatabasePath(region)));
    if (region == currentRegion()) { restoreOutdatedState(); emit seriesListUpdated(); }
    setStatus(regionTag(region) + ": card lists reset.");
}

void SeriesRepository::purgeFallbackCards(Region region)
{
    QSqlQuery query(cardDb(region));
    if (!query.exec("DELETE FROM cards WHERE series_id = '__official__'")) {
        emit errorOccurred(query.lastError().text()); return;
    }
    const int count = query.numRowsAffected();
    setStatus(QString("%1: purged %2 fallback card(s).").arg(regionTag(region)).arg(count));
    emit fallbackCardsPurged(region, count);
    if (region == currentRegion()) emit seriesListUpdated();
}
