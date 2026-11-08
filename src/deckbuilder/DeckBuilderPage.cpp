#include "DeckBuilderPage.h"

#include <utility>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPainter>
#include <QTransform>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <limits>

#include "../core/Config.h"
#include "../database/LocalCardStore.h"
#include "../network/NetGuard.h"
#include "DeckBuilderImages.h"

// Reuse only saved catalogues. Opening the builder never starts a manifest GET.
static QJsonObject deckManifestForEdition(const QString &edition)
{
    auto read = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object();
    };
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + "/manifests";
    auto &config = Config::instance();
    const QString activeEdition = config.getCurDetectLocaleMode() == Config::DetectLocaleMode::EN
                                  ? "en" : "jp";
    if (activeEdition == edition) {
        const QUrl url(config.getIndexManifestUrl());
        const QByteArray identity = (edition == "en" ? QByteArray("EN:") : QByteArray("JP:"))
                                    + url.toEncoded();
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
        const QJsonObject cached = read(directory + "/" + hash + ".json");
        if (cached.value("indexes").isArray()) return cached;
    }
    // IndexCatalog writes this edition-qualified snapshot whenever it applies a manifest.
    const QJsonObject snapshot = read(directory + "/deck-groups-" + edition + ".json");
    const QJsonObject manifest = snapshot.value("manifest").toObject();
    if (snapshot.value("edition").toString() == edition && manifest.value("indexes").isArray())
        return manifest;
    return {};
}

int DeckBuilderDeckModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : int(entries_.size()); }
QVariant DeckBuilderDeckModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
        return {};
    const Entry &entry = entries_[index.row()];
    if (role == CardRole) return entry.card;
    if (role == QuantityRole) return entry.quantity;
    return {};
}
QHash<int, QByteArray> DeckBuilderDeckModel::roleNames() const
{ return {{CardRole, "cardData"}, {QuantityRole, "copies"}}; }
int DeckBuilderDeckModel::quantity(const QString &code) const
{
    for (const Entry &entry : entries_)
        if (entry.card.value("cardCode").toString().compare(code, Qt::CaseInsensitive) == 0)
            return entry.quantity;
    return 0;
}
void DeckBuilderDeckModel::setQuantity(const QVariantMap &card, int quantity)
{
    const QString code = card.value("cardCode").toString();
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].card.value("cardCode").toString().compare(code, Qt::CaseInsensitive) != 0)
            continue;
        if (quantity <= 0) {
            beginRemoveRows({}, i, i);
            entries_.removeAt(i);
            endRemoveRows();
        } else {
            entries_[i].quantity = quantity;
            emit dataChanged(index(i), index(i), {QuantityRole});
        }
        return;
    }
    if (quantity > 0) {
        const int row = int(entries_.size());
        beginInsertRows({}, row, row);
        entries_.append(Entry{card, quantity});
        endInsertRows();
    }
}
void DeckBuilderDeckModel::replace(QVector<Entry> entries)
{ beginResetModel(); entries_ = std::move(entries); endResetModel(); }

// Count copies and printed Soul trigger symbols, not distinct rows or base soul damage.
static QVariantMap deckStatistics(const QVector<DeckBuilderDeckModel::Entry> &entries)
{
    QVariantMap stats{{"character", 0}, {"event", 0}, {"climax", 0}, {"soulTriggers", 0},
                      {"level0", 0}, {"level1", 0}, {"level2", 0}, {"level3", 0}};
    for (const auto &entry : entries) {
        const QString kind = entry.card.value("cardKind").toString();
        const QString kindKey = kind == "CH" ? "character" : kind == "EV" ? "event"
                              : kind == "CX" ? "climax" : QString();
        if (!kindKey.isEmpty()) stats[kindKey] = stats.value(kindKey).toInt() + entry.quantity;
        if (kind != "CX") {
            bool ok = false;
            const int level = entry.card.value("level").toString().trimmed().toInt(&ok);
            if (ok && level >= 0 && level <= 3) {
                const QString key = "level" + QString::number(level);
                stats[key] = stats.value(key).toInt() + entry.quantity;
            }
        }
        QString raw = entry.card.value("cardTrigger").toString().trimmed().toUpper();
        QStringList triggers = LocalCardStore::icons(raw);
        if (triggers.isEmpty())
            triggers = raw.split(QRegularExpression(QStringLiteral("[,\\s/]+")), Qt::SkipEmptyParts);
        stats["soulTriggers"] = stats.value("soulTriggers").toInt()
                             + int(triggers.count("SOUL")) * entry.quantity;
    }
    return stats;
}

DeckBuilderController::DeckBuilderController(QString path, QWidget *dialogParent)
    : QObject(dialogParent), path_(std::move(path)), deck_(this), dialogParent_(dialogParent)
{
    deckStats_ = deckStatistics(deck_.entries());
    QTimer::singleShot(0, this, &DeckBuilderController::reload);
}
DeckBuilderController::~DeckBuilderController()
{
    if (exportRequest_)
        NetGuard::instance().cancel(exportRequest_);
}
void DeckBuilderController::setStatus(const QString &status)
{ status_ = status; emit stateChanged(); }
int DeckBuilderController::totalCards() const
{
    int total = 0;
    for (const auto &entry : deck_.entries()) total += entry.quantity;
    return total;
}
int DeckBuilderController::climaxCards() const
{
    int total = 0;
    for (const auto &entry : deck_.entries())
        if (entry.card.value("cardKind") == "CX") total += entry.quantity;
    return total;
}
void DeckBuilderController::changedDeck(bool dirty)
{
    dirty_ = dirty;
    ++deckRevision_;
    deckStats_ = deckStatistics(deck_.entries());
    emit deckChanged();
    if (referenceDeckOnly_ && referenceOpen_) {
        QStringList codes;
        for (const auto &entry : deck_.entries()) codes << entry.card.value("cardCode").toString();
        // Quantity changes within a row do not restart or scroll the popup.
        if (codes != referenceDeckCodes_) openReference(referenceKind_, referenceValue_);
    }
}
void DeckBuilderController::setDeckName(const QString &name)
{
    if (name_ == name) return;
    name_ = name;
    changedDeck();
    emit stateChanged();
}
int DeckBuilderController::quantity(const QString &code) const { return deck_.quantity(code); }
QString DeckBuilderController::imageSource(const QString &picture) const
{ return DeckBuilderImages::source(picture); }
void DeckBuilderController::selectCard(const QVariantMap &card)
{
    if (card.value("setEdition") != edition_) return;
    selected_ = LocalCardStore::card(path_, edition_, card.value("cardCode").toString(), language_);
    emit selectedChanged();
}
void DeckBuilderController::addCard(const QVariantMap &card) { changeQuantity(card, 1); }
void DeckBuilderController::changeQuantity(const QVariantMap &card, int delta)
{
    if (card.value("cardCode").toString().isEmpty() || card.value("setEdition") != edition_)
        return;
    const int old = deck_.quantity(card.value("cardCode").toString());
    const int next = qBound(0, old + delta, 999);
    if (old == next) return;
    deck_.setQuantity(card, next);
    changedDeck();
}
bool DeckBuilderController::confirmDiscard()
{
    if (exporting_) {
        QMessageBox::information(dialogParent_, "Deck export", "Finish or cancel the image export first.");
        return false;
    }
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(dialogParent_, "Save deck?",
        "Save your deck before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveText();
    return answer == QMessageBox::Discard;
}
bool DeckBuilderController::setEdition(const QString &edition)
{
    if (edition == edition_) return true;
    if (edition != "jp" && edition != "en") return false;
    if (!deck_.entries().isEmpty() || dirty_) {
        if (!confirmDiscard()) return false;
        if (!deck_.entries().isEmpty() && !dirty_) {
            if (QMessageBox::question(dialogParent_, "Change edition?",
                "Start an empty deck for the other edition? Your saved TXT file is kept.")
                != QMessageBox::Yes)
                return false;
        }
    }
    edition_ = edition;
    deck_.replace({});
    selected_.clear();
    name_ = "My Deck";
    changedDeck(false);
    emit selectedChanged();
    reload();
    return true;
}
void DeckBuilderController::setLanguage(const QString &language)
{
    if ((language != "EN" && language != "JP") || language == language_) return;
    closeReference();
    language_ = language;
    selected_.clear();
    emit selectedChanged();
    search(title_, filters_, 0);
    emit stateChanged();
}
void DeckBuilderController::reload()
{
    closeReference();
    const quint64 serial = ++titlesGeneration_;
    ++generation_;
    title_.clear();
    results_.clear();
    loadError_.clear();
    searchPending_ = false;
    pendingAppend_ = false;
    titles_.clear();
    catalogueNotice_.clear();
    totalMatches_ = 0;
    page_ = 0;
    titlesRunning_ = true;
    emit resultsReset();
    emit resultsChanged();
    emit titlesChanged();
    setStatus("Loading local sets…");
    const QString path = path_, edition = edition_;
    const QJsonObject manifest = deckManifestForEdition(edition);
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, serial] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        if (serial != titlesGeneration_) return;
        titlesRunning_ = false;
        titles_ = result.value("titles").toList();
        catalogueNotice_ = result.value("notice").toString();
        const QString error = result.value("error").toString();
        setStatus(!error.isEmpty() ? error : titles_.isEmpty()
                      ? "No local cards are available in this edition." : "Choose a set.");
        emit titlesChanged();
    });
    watcher->setFuture(QtConcurrent::run([path, edition, manifest] {
        const QVariantMap result = LocalCardStore::deckGroups(path, edition, manifest);
        LocalCardStore::releaseThreadConnection(path);
        return result;
    }));
}
void DeckBuilderController::search(const QString &titleNumber, const QVariantMap &filters, int)
{
    QStringList codes;
    for (const QVariant &entry : titles_) {
        const QVariantMap group = entry.toMap();
        if (group.value("titleNumber").toString() == titleNumber) {
            codes = group.value("setCodes").toStringList();
            break;
        }
    }
    title_ = codes.isEmpty() ? QString() : titleNumber;
    filters_ = filters;
    filters_["_deckSetCodes"] = codes;
    page_ = 0; // Kept for source compatibility; the UI now scrolls continuously.
    ++generation_;
    results_.clear();
    totalMatches_ = 0;
    loadError_.clear();
    searchPending_ = true;
    pendingAppend_ = false;
    emit resultsReset();
    emit resultsChanged();
    if (!searchRunning_) dispatchSearch();
}
void DeckBuilderController::loadMore()
{
    if (busy() || title_.isEmpty() || !hasMore()) return;
    loadError_.clear();
    searchPending_ = true;
    pendingAppend_ = true;
    dispatchSearch();
}
void DeckBuilderController::dispatchSearch()
{
    searchPending_ = false;
    if (title_.isEmpty()) {
        results_.clear(); totalMatches_ = 0;
        emit resultsChanged(); emit stateChanged();
        return;
    }
    searchRunning_ = true;
    loadingMore_ = pendingAppend_;
    const bool append = loadingMore_;
    const quint64 serial = generation_;
    const QString path = path_, edition = edition_, title = title_, language = language_;
    const QVariantMap filters = filters_;
    const int offset = append ? int(results_.size()) : 0;
    setStatus(append ? "Loading more cards…" : "Searching local cards…");
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
        [this, watcher, serial, append, offset] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        searchRunning_ = false;
        loadingMore_ = false;
        if (serial == generation_) {
            const QVariantList cards = result.value("cards").toList();
            loadError_ = result.value("error").toString();
            if (loadError_.isEmpty() && cards.isEmpty() && result.value("total").toInt() > offset)
                loadError_ = "The card database changed while loading. Reload the set.";
            if (loadError_.isEmpty()) {
                if (append) results_.append(cards); else results_ = cards;
                totalMatches_ = result.value("total").toInt();
                setStatus(QString("%1 of %2 cards loaded").arg(results_.size()).arg(totalMatches_));
                emit resultsChanged();
            } else {
                setStatus(loadError_);
            }
        }
        if (searchPending_) dispatchSearch();
        else emit stateChanged();
    });
    watcher->setFuture(QtConcurrent::run([path, edition, title, filters, language, offset] {
        const QVariantMap result = LocalCardStore::deckPage(path, edition, title, filters,
                                                          language, offset, 60);
        LocalCardStore::releaseThreadConnection(path);
        return result;
    }));
}
void DeckBuilderController::setReferenceSort(const QString &group, const QString &sort, bool descending)
{
    if (group != "none" && group != "color" && group != "level") return;
    if (sort != "code" && sort != "name" && sort != "level" && sort != "power"
        && sort != "colorLevelCostPower") return;
    if (referenceGroup_ == group && referenceSort_ == sort && referenceDescending_ == descending) return;
    referenceGroup_ = group;
    referenceSort_ = sort;
    referenceDescending_ = descending;
    if (referenceOpen_) openReference(referenceKind_, referenceValue_);
}
void DeckBuilderController::setReferenceDeckOnly(bool enabled)
{
    if (referenceDeckOnly_ == enabled) return;
    referenceDeckOnly_ = enabled; // Session only; deliberately not written to Config.
    if (referenceOpen_) openReference(referenceKind_, referenceValue_);
    else emit referenceChanged();
}
void DeckBuilderController::openReference(const QString &kind, const QString &value)
{
    if ((!referenceDeckOnly_ && title_.isEmpty()) || value.trimmed().isEmpty()
        || (kind != "trait" && kind != "name" && kind != "ability")) return;
    ++referenceGeneration_;
    referenceOpen_ = true;
    referenceDeckCodes_.clear();
    for (const auto &entry : deck_.entries())
        referenceDeckCodes_ << entry.card.value("cardCode").toString();
    referenceKind_ = kind;
    referenceValue_ = value.trimmed();
    referenceTitle_ = title_;
    referenceSetCodes_ = filters_.value("_deckSetCodes").toStringList();
    referenceResults_.clear();
    referenceTotal_ = 0;
    referenceError_.clear();
    referencePending_ = true;
    referenceAppend_ = false;
    emit referenceReset();
    emit referenceChanged();
    if (!referenceRunning_) dispatchReference();
}
void DeckBuilderController::closeReference()
{
    referenceOpen_ = false;
    ++referenceGeneration_;
    referencePending_ = false;
    referenceResults_.clear();
    referenceTotal_ = 0;
    referenceError_.clear();
    emit referenceReset();
    emit referenceChanged();
}
void DeckBuilderController::loadMoreReferences()
{
    if (referenceRunning_ || !referenceHasMore()) return;
    referenceError_.clear();
    referenceAppend_ = true;
    dispatchReference();
}
void DeckBuilderController::dispatchReference()
{
    referencePending_ = false;
    referenceRunning_ = true;
    const quint64 serial = referenceGeneration_;
    const bool append = referenceAppend_;
    const int offset = append ? int(referenceResults_.size()) : 0;
    const QString path = path_, edition = edition_, title = referenceTitle_, language = language_;
    const QVariantMap filters{{"_deckReferenceKind", referenceKind_},
                              {"_deckReferenceTerm", referenceValue_},
                              {"_deckSetCodes", referenceSetCodes_},
                              {"_deckReferenceDeckOnly", referenceDeckOnly_},
                              {"_deckReferenceCodes", referenceDeckCodes_},
                              {"_deckGroup", referenceGroup_},
                              {"_deckSort", referenceSort_},
                              {"_deckDescending", referenceDescending_}};
    emit referenceChanged();
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
        [this, watcher, serial, append, offset] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        referenceRunning_ = false;
        if (serial == referenceGeneration_) {
            const QVariantList cards = result.value("cards").toList();
            referenceError_ = result.value("error").toString();
            if (referenceError_.isEmpty() && cards.isEmpty() && result.value("total").toInt() > offset)
                referenceError_ = "The card database changed. Close and reopen this reference.";
            if (referenceError_.isEmpty()) {
                if (append) referenceResults_.append(cards); else referenceResults_ = cards;
                referenceTotal_ = result.value("total").toInt();
            }
            emit referenceChanged();
        }
        if (referencePending_) dispatchReference();
        else emit referenceChanged();
    });
    watcher->setFuture(QtConcurrent::run([path, edition, title, filters, language, offset] {
        const QVariantMap result = LocalCardStore::deckPage(path, edition, title, filters,
                                                          language, offset, 60);
        LocalCardStore::releaseThreadConnection(path);
        return result;
    }));
}
bool DeckBuilderController::saveText()
{
    QString path = QFileDialog::getSaveFileName(dialogParent_, "Save deck", "deck.txt", "Deck text (*.txt)");
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += ".txt";
    QString text = "# WSDeckImporter deck v1\n# edition=" + edition_
                   + "\n# name=" + QString::fromLatin1(QUrl::toPercentEncoding(name_)) + "\n";
    for (const auto &entry : deck_.entries()) {
        QString name = entry.card.value("cardName").toString();
        name.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
        text += QString::number(entry.quantity) + " " + entry.card.value("cardCode").toString()
                + " " + name + "\n";
    }
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus("Could not save deck: " + file.errorString());
        return false;
    }
    changedDeck(false);
    setStatus("Deck saved: " + path);
    return true;
}
void DeckBuilderController::newDeck()
{
    if (!confirmDiscard()) return;
    deck_.replace({}); name_ = "My Deck";
    changedDeck(false); emit stateChanged();
}
void DeckBuilderController::openText()
{
    if (exporting_) return;
    const QString filePath = QFileDialog::getOpenFileName(dialogParent_, "Open deck", {}, "Deck text (*.txt)");
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
        setStatus("Unable to open this deck file (maximum 2 MiB)."); return;
    }
    QString text = QString::fromUtf8(file.readAll());
    text.remove(QChar(0xfeff));
    QString edition = edition_, name = QFileInfo(filePath).completeBaseName();
    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith("# edition=")) edition = line.mid(10).trimmed();
        if (line.startsWith("# name=")) name = QUrl::fromPercentEncoding(line.mid(7).toUtf8());
    }
    if (edition != "jp" && edition != "en") { setStatus("Invalid deck edition."); return; }
    QVector<DeckBuilderDeckModel::Entry> imported;
    QHash<QString, int> positions;
    static const QRegularExpression row("^([1-9][0-9]{0,2})\\s+(\\S+)(?:\\s+.*)?$");
    int lineNumber = 0;
    for (QString line : lines) {
        ++lineNumber; line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const auto match = row.match(line);
        if (!match.hasMatch()) { setStatus(QString("Invalid deck entry on line %1.").arg(lineNumber)); return; }
        const QString code = match.captured(2).toUpper();
        const int copies = match.captured(1).toInt();
        if (positions.contains(code)) {
            auto &entry = imported[positions.value(code)];
            if (entry.quantity + copies > 999) { setStatus("A card quantity exceeds 999."); return; }
            entry.quantity += copies;
        } else {
            if (imported.size() >= 1000) { setStatus("Deck files support up to 1000 distinct cards."); return; }
            const QVariantMap card = LocalCardStore::card(path_, edition, code, language_);
            if (card.isEmpty()) { setStatus("Card not found in this edition: " + code); return; }
            positions.insert(code, int(imported.size()));
            imported.append(DeckBuilderDeckModel::Entry{card, copies});
        }
    }
    if (!confirmDiscard()) return;
    const bool changedEdition = edition_ != edition;
    edition_ = edition; name_ = name;
    deck_.replace(std::move(imported));
    selected_.clear(); emit selectedChanged();
    changedDeck(false);
    if (changedEdition) reload();
    setStatus("Opened deck: " + filePath);
}
// Same color buckets and numeric priority as the default database/browser order.
static int deckExportColorRank(const QVariantMap &card)
{
    const QString raw = card.value("color").toString().trimmed().toUpper();
    QStringList colors = LocalCardStore::icons(raw);
    if (colors.isEmpty())
        colors = raw.split(QRegularExpression(QStringLiteral("[,\\s/]+")), Qt::SkipEmptyParts);
    const QStringList order{"RED", "YELLOW", "GREEN", "BLUE"};
    int rank = 5, found = 0;
    for (int i = 0; i < order.size(); ++i) {
        if (colors.contains(order[i])) { rank = i; ++found; }
    }
    return found > 1 ? 4 : rank;
}

static bool deckExportBefore(const DeckBuilderDeckModel::Entry &left,
                             const DeckBuilderDeckModel::Entry &right)
{
    static const QRegularExpression integer(QStringLiteral("^[0-9]+$"));
    for (const QString &field : {QString("level"), QString("cost"), QString("power")}) {
        const QString a = left.card.value(field).toString().trimmed();
        const QString b = right.card.value(field).toString().trimmed();
        const bool aValid = integer.match(a).hasMatch();
        const bool bValid = integer.match(b).hasMatch();
        if (aValid != bValid)
            return aValid; // Missing/invalid values last.
        if (!aValid) {
            if (field == "level") {
                const int ac = deckExportColorRank(left.card), bc = deckExportColorRank(right.card);
                if (ac != bc)
                    return ac < bc;
            }
            continue;
        }
        bool aOk = false, bOk = false;
        qlonglong aNumber = a.toLongLong(&aOk), bNumber = b.toLongLong(&bOk);
        // SQLite CAST clamps overflowing positive integers to its maximum integer.
        if (!aOk)
            aNumber = std::numeric_limits<qlonglong>::max();
        if (!bOk)
            bNumber = std::numeric_limits<qlonglong>::max();
        if (aNumber != bNumber)
            return aNumber > bNumber;
        if (field == "level") {
            const int ac = deckExportColorRank(left.card), bc = deckExportColorRank(right.card);
            if (ac != bc)
                return ac < bc;
        }
    }
    return QString::compare(left.card.value("cardCode").toString(),
                            right.card.value("cardCode").toString(),
                            Qt::CaseInsensitive)
           < 0;
}

void DeckBuilderController::exportImage()
{
    if (exporting_ || deck_.entries().isEmpty()) return;
    // Bound output allocation for large deck images.
    if (deck_.rowCount() > 1000) { setStatus("Image export supports up to 1000 distinct cards."); return; }
    QString path = QFileDialog::getSaveFileName(dialogParent_, "Export deck image", "deck.png", "PNG image (*.png)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".png";
    exportPath_ = path; exportName_ = name_; exportDeck_ = deck_.entries();
    std::stable_sort(exportDeck_.begin(), exportDeck_.end(), deckExportBefore);
    exportImages_.clear(); exportImages_.reserve(exportDeck_.size());
    exportIndex_ = 0; exporting_ = true; writingExport_ = false; ++exportGeneration_;
    emit stateChanged();
    nextExportImage();
}
void DeckBuilderController::cancelExport()
{
    if (writingExport_) return;
    ++exportGeneration_;
    exporting_ = false;
    const quint64 id = exportRequest_; exportRequest_ = 0;
    if (id) NetGuard::instance().cancel(id);
    exportImages_.clear(); exportDeck_.clear();
    setStatus("Image export cancelled.");
}
void DeckBuilderController::nextExportImage()
{
    if (!exporting_) return;
    if (exportIndex_ >= exportDeck_.size()) { renderExport(); return; }
    setStatus(QString("Preparing deck image: %1 / %2").arg(exportIndex_ + 1).arg(exportDeck_.size()));
    const auto &entry = exportDeck_[exportIndex_];
    const quint64 serial = exportGeneration_;
    exportRequest_ = DeckBuilderImages::load(entry.card.value("picture").toString(), this,
        [this, serial](const QImage &image, const QString &error) {
            if (serial != exportGeneration_) return;
            exportRequest_ = 0;
            if (!exporting_) return;
            if (image.isNull()) {
                const QString code = exportDeck_[exportIndex_].card.value("cardCode").toString();
                exporting_ = false; exportImages_.clear(); exportDeck_.clear();
                setStatus("Image export stopped at " + code + ": " + error);
                return;
            }
            QImage prepared = image;
            const bool climax = exportDeck_[exportIndex_].card.value("cardKind").toString() == "CX";
            if (climax && prepared.height() > prepared.width())
                prepared = prepared.transformed(QTransform().rotate(90), Qt::SmoothTransformation);
            exportImages_.append(prepared.scaled(climax ? 224 : 160, climax ? 160 : 224,
                                                 Qt::KeepAspectRatio, Qt::SmoothTransformation));
            ++exportIndex_;
            nextExportImage();
        });
}
void DeckBuilderController::renderExport()
{
    const QString path = exportPath_, name = exportName_;
    const auto cards = exportDeck_;
    const auto images = exportImages_;
    exportDeck_.clear(); exportImages_.clear();
    writingExport_ = true;
    setStatus("Writing deck image…");
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        const QString error = watcher->result(); watcher->deleteLater();
        exporting_ = false; writingExport_ = false;
        setStatus(error.isEmpty() ? "Deck image saved: " + path : error);
    });
    watcher->setFuture(QtConcurrent::run([path, name, cards, images]() -> QString {
        if (cards.isEmpty() || cards.size() != images.size()) return "No complete deck image to export.";
        QVector<int> mainCards, climaxCards;
        for (int i = 0; i < cards.size(); ++i) {
            if (cards[i].card.value("cardKind").toString() == "CX") climaxCards << i;
            else mainCards << i;
        }
        const int margin = 20, header = 182, sectionHeading = 34;
        const int mainColumns = qMin(8, int(mainCards.size()));
        const int climaxColumns = qMin(6, int(climaxCards.size()));
        const int mainWidth = 180, mainHeight = 292;
        const int climaxWidth = 244, climaxHeight = 228;
        const int mainRows = mainColumns ? (int(mainCards.size()) + mainColumns - 1) / mainColumns : 0;
        const int climaxRows = climaxColumns ? (int(climaxCards.size()) + climaxColumns - 1) / climaxColumns : 0;
        const int contentWidth = qMax(560, qMax(mainColumns * mainWidth, climaxColumns * climaxWidth));
        const int climaxTop = header + mainRows * mainHeight + (mainRows ? 12 : 0);
        const int canvasHeight = climaxCards.isEmpty() ? header + mainRows * mainHeight + margin
            : climaxTop + sectionHeading + climaxRows * climaxHeight + margin;
        QImage canvas(contentWidth + margin * 2, canvasHeight, QImage::Format_RGB32);
        if (canvas.isNull()) return "Not enough memory to render this deck image.";
        canvas.fill(QColor("#20242b"));
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Segoe UI", 20, QFont::Bold));
        painter.drawText(QRect(margin, 10, contentWidth, 36), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(name, Qt::ElideRight, contentWidth));
        const QVariantMap stats = deckStatistics(cards);
        const QStringList statKeys{"character", "event", "climax", "soulTriggers", "level0", "level1", "level2", "level3"};
        const QStringList statLabels{"Character", "Event", "Climax", "Trigger icons", "Lv 0", "Lv 1", "Lv 2", "Lv 3"};
        const int statWidth = contentWidth / 2;
        painter.setFont(QFont("Segoe UI", 11));
        for (int i = 0; i < statKeys.size(); ++i) {
            const int x = margin + (i % 2) * statWidth, y = 54 + (i / 2) * 28;
            painter.fillRect(QRect(x, y + 5, 3, 16), QColor("#edbd58"));
            painter.setPen(QColor("#dce5ef"));
            const QString label = statLabels[i] + ": " + QString::number(stats.value(statKeys[i]).toInt());
            painter.drawText(QRect(x + 12, y, statWidth - 20, 26), Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(label, Qt::ElideRight, statWidth - 20));
        }
        auto drawCards =
            [&](const QVector<int> &indices, int columns, int top, int cellWidth, int cellHeight) {
                if (indices.isEmpty())
                    return;
                for (int n = 0; n < indices.size(); ++n) {
                    const int i = indices[n];
                    const int x = margin + (n % columns) * cellWidth;
                    const int y = top + (n / columns) * cellHeight;
                    const QImage &image = images[i];
                    const int imageX = x + (cellWidth - image.width()) / 2;
                    const int imageY = y + 12;

                    painter.drawImage(imageX, imageY, image);
                    painter.setFont(QFont("Segoe UI", 9, QFont::Bold));

                    const QString quantityText = QString::number(cards[i].quantity);
                    const int diameter = qMax(22,
                                              painter.fontMetrics().horizontalAdvance(quantityText)
                                                  + 8);

                    const QRect badge(imageX + image.width() - diameter / 2,
                                      imageY - diameter / 2,
                                      diameter,
                                      diameter);

                    painter.setBrush(QColor("#4d9dff"));
                    painter.setPen(Qt::NoPen);
                    painter.drawEllipse(badge);

                    painter.setPen(Qt::white);
                    painter.drawText(badge, Qt::AlignCenter, quantityText);

                    painter.setFont(QFont("Segoe UI", 9));

                    const QRect codeRect(x + 4, imageY + image.height() + 8, cellWidth - 8, 20);

                    painter.drawText(codeRect,
                                     Qt::AlignHCenter | Qt::AlignVCenter,
                                     painter.fontMetrics()
                                         .elidedText(cards[i].card.value("cardCode").toString(),
                                                     Qt::ElideRight,
                                                     codeRect.width()));
                }
            };
        drawCards(mainCards, mainColumns, header, mainWidth, mainHeight);
        if (!climaxCards.isEmpty()) {
            painter.setPen(QColor("#b7c9e2"));
            painter.setFont(QFont("Segoe UI", 12, QFont::Bold));
            painter.drawText(QRect(margin, climaxTop, contentWidth, sectionHeading),
                             Qt::AlignVCenter,
                             "Climax");
            drawCards(climaxCards,
                      climaxColumns,
                      climaxTop + sectionHeading,
                      climaxWidth,
                      climaxHeight);
        }
        painter.end();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return "Could not open PNG: " + file.errorString();
        if (!canvas.save(&file, "PNG")) { file.cancelWriting(); return "Could not encode the PNG image."; }
        if (!file.commit()) return "Could not save PNG: " + file.errorString();
        return {};
    }));
}

DeckBuilderPage::DeckBuilderPage(QWidget *parent) : QWidget(parent)
{
    const QString path = QCoreApplication::applicationDirPath() + "/weiss-db.sqlite";
    controller_ = new DeckBuilderController(path, this);
    view_ = new QQuickWidget(this);
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view_->engine()->addImageProvider("deckcards", new DeckBuilderImageProvider);
    view_->rootContext()->setContextProperty("deckBuilder", controller_);
    // The shared CardDetailPanel is configured with an explicit local image source
    // and requestCardData=false in this page. No global DatabaseUtil is changed.
    view_->setSource(QUrl("qrc:/qml/deckbuilder/DeckBuilderPage.qml"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
}
bool DeckBuilderPage::confirmClose() { return controller_->confirmDiscard(); }
