#include "GalleryController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <utility>

#include "../core/Config.h"
#include "../database/DatabaseUtil.h"
#include "../database/LocalCardStore.h"
#include "../deckbuilder/DeckBuilderController.h"
#include "../viewmodels/Models.h"

static QString currentDetectionEdition()
{
    return Config::instance().getCurDetectLocaleMode() == Config::DetectLocaleMode::EN
               ? QStringLiteral("en")
               : QStringLiteral("jp");
}

GalleryController::GalleryController(DatabaseUtil *database,
                                     SelectionModel *selection,
                                     DeckBuilderController *deck,
                                     QObject *parent)
    : QObject(parent)
    , database_(database)
    , selection_(selection)
    , deck_(deck)
    , edition_(currentDetectionEdition())
{
    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(35);
    connect(&rebuildTimer_, &QTimer::timeout, this, &GalleryController::rebuild);
    connect(selection_, &QAbstractItemModel::modelReset, this, [this] {
        hidden_.clear();
        dataCache_.clear();
        edition_ = currentDetectionEdition();
        requested_.clear();
        dataCache_.clear();
        schedule();
    });
    connect(selection_, &QAbstractItemModel::dataChanged, this, &GalleryController::schedule);
    connect(selection_, &QAbstractItemModel::rowsInserted, this, &GalleryController::schedule);
    connect(selection_, &QAbstractItemModel::rowsRemoved, this, [this] {
        hidden_.clear();
        schedule();
    });
    connect(selection_, &QAbstractItemModel::rowsMoved, this, [this] {
        hidden_.clear();
        schedule();
    });
    connect(database_, &DatabaseUtil::cardReady, this, [this](const QString &code) {
        dataCache_.remove(code);
        schedule();
    });
    connect(database_, &DatabaseUtil::imageFallbackChanged, this, [this](const QString &code) {
        dataCache_.remove(code);
        schedule();
    });
    connect(database_, &DatabaseUtil::cardDataChanged, this, [this] {
        requested_.clear();
        dataCache_.clear();
        schedule();
    });
    connect(database_, &DatabaseUtil::localeChanged, this, [this] {
        requested_.clear();
        dataCache_.clear();
        schedule();
    });
    connect(database_, &DatabaseUtil::cardDataSourceChanged, this, [this] {
        requested_.clear();
        dataCache_.clear();
        schedule();
    });
    connect(deck_, &DeckBuilderController::stateChanged, this, [this] {
        if (!notice_.isEmpty())
            setNotice(QString());
    });
    schedule();
}

void GalleryController::schedule()
{
    rebuildTimer_.start();
}

void GalleryController::setNotice(const QString &notice)
{
    notice_ = notice;
    emit noticeChanged();
}

void GalleryController::setDetectedEdition(const QString &edition)
{
    if ((edition != "jp" && edition != "en") || edition == edition_)
        return;

    edition_ = edition;
    requested_.clear();
    dataCache_.clear();
    rebuild();
}

QByteArray GalleryController::fingerprint(int row) const
{
    const QJsonArray identity{selection_->dataAt(row, "label").toString(),
                              selection_->dataAt(row, "cardId").toString(),
                              selection_->dataAt(row, "qty").toInt(),
                              selection_->dataAt(row, "confirmed").toBool()};
    return QJsonDocument(identity).toJson(QJsonDocument::Compact);
}

void GalleryController::rebuild()
{
    rebuildTimer_.stop();
    QVariantList next;
    QHash<QString, int> positions;

    for (auto it = hidden_.begin(); it != hidden_.end();) {
        if (it.key() >= selection_->rowCount() || fingerprint(it.key()) != it.value())
            it = hidden_.erase(it);
        else
            ++it;
    }

    for (int i = 0; i < selection_->rowCount(); ++i) {
        const QString code = selection_->dataAt(i, "cardId").toString().trimmed();

        if (code.isEmpty() || hidden_.contains(i))
            continue;

        if (!dataCache_.contains(code))
            dataCache_.insert(code, database_->cardDataFor(code));

        QVariantMap card = dataCache_.value(code);
        card.insert("cardKind", LocalCardStore::kind(card.value("cardKind").toString()));
        const bool needsData = !card.contains("cardName");
        card.insert("cardCode", code);
        card.insert("cardId", code);
        card.insert("setEdition", edition_);
        card.insert("copies", selection_->dataAt(i, "qty"));
        card.insert("_sourceRow", i);

        if (needsData) {
            card.insert("cardName", selection_->dataAt(i, "label"));

            if (!requested_.contains(code)) {
                requested_.insert(code);
                database_->ensureCardData(code);
            }
        }

        if (positions.contains(code)) {
            const int position = positions.value(code);
            QVariantMap combined = next[position].toMap();
            combined.insert("copies",
                            combined.value("copies").toLongLong()
                                + card.value("copies").toLongLong());
            QVariantList sourceRows = combined.value("_sourceRows").toList();
            sourceRows.append(i);
            combined.insert("_sourceRows", sourceRows);
            next[position] = combined;
        } else {
            QVariantList sourceRows;
            sourceRows.append(i);
            card.insert("_sourceRows", sourceRows);
            positions.insert(code, int(next.size()));
            next.append(card);
        }
    }

    rows_ = std::move(next);
    emit rowsChanged();
}

bool GalleryController::transferDetected(bool move)
{
    Q_UNUSED(move);
    setNotice(QString());
    rebuild();
    const QVariantList snapshot = rows_;
    if (!deck_->replaceCards(snapshot, edition_))
        return false;

    setNotice("Copied detected cards; My Deck has been replaced.");
    emit showDeckRequested();
    return true;
}

bool GalleryController::exportDetected(const QString &format)
{
    rebuild();
    return deck_->exportSnapshot(rows_, edition_, "Detected cards", format);
}

void GalleryController::restoreMoved()
{
    hidden_.clear();
    rebuild();
    setNotice("Moved gallery entries restored. My Deck is unchanged.");
}

int GalleryController::cardScalePercent() const
{
    return Config::instance().getGalleryCardScalePercent();
}

void GalleryController::setCardScalePercent(int percent)
{
    auto &config = Config::instance();

    const int previous = config.getGalleryCardScalePercent();

    config.setGalleryCardScalePercent(percent);

    if (config.getGalleryCardScalePercent() != previous)
        emit cardScalePercentChanged();
}
