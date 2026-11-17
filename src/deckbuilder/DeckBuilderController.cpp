#include "DeckBuilderController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <utility>

#include "../core/Config.h"
#include "../database/LocalCardStore.h"
#include "../index/IndexCatalog.h"
#include "../network/NetGuard.h"
#include "DeckBuilderImages.h"
#include "DeckCode.h"

struct DeckPngSection
{
    QString label;
    QVector<int> indices;
    bool climax = false;
    int columns = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    int top = 0;
};

struct DeckPngPage
{
    QVector<DeckPngSection> sections;
    int cardWidth = 0;
    int copies = 0;
};

static QVector<DeckPngSection> deckPngSections(const QVector<DeckBuilderDeckModel::Entry> &cards,
                                               const QVector<int> &indices)
{
    QMap<qlonglong, QVector<int>> levels;
    QVector<int> unknown, climaxes;
    static const QRegularExpression integer(QStringLiteral("^[0-9]+$"));
    for (int index : indices) {
        const QVariantMap &card = cards[index].card;
        if (LocalCardStore::kind(card.value("cardKind").toString()) == "CX") {
            climaxes.append(index);
            continue;
        }
        const QString raw = card.value("level").toString().trimmed();
        bool ok = false;
        const qlonglong level = raw.toLongLong(&ok);
        if (ok && integer.match(raw).hasMatch())
            levels[level].append(index);
        else
            unknown.append(index);
    }
    QVector<DeckPngSection> sections;
    for (auto it = levels.cend(); it != levels.cbegin();) {
        --it;
        sections.append(DeckPngSection{QString("Level %1").arg(it.key()), it.value()});
    }
    if (!unknown.isEmpty())
        sections.append(DeckPngSection{QStringLiteral("Other"), unknown});
    if (!climaxes.isEmpty())
        sections.append(DeckPngSection{QStringLiteral("Climax"), climaxes, true});
    return sections;
}

static bool fitDeckPngPage(DeckPngPage &page)
{
    const int contentWidth = 1944;
    for (int width = 220; width >= 96; width -= 2) {
        int cursor = 146;
        for (auto &section : page.sections) {
            const int imageWidth = section.climax ? qRound(width * 1.4) : width;
            const int imageHeight = section.climax ? width : qRound(width * 1.4);
            section.cellWidth = imageWidth + 12;
            section.cellHeight = imageHeight + 28;
            section.columns = qMax(1, contentWidth / section.cellWidth);
            section.top = cursor;
            const int rows = (int(section.indices.size()) + section.columns - 1) / section.columns;
            cursor += 30 + rows * section.cellHeight + 12;
        }
        if (cursor <= 1720) {
            page.cardWidth = width;
            return true;
        }
    }
    return false;
}

static QVector<DeckPngPage> deckPngPages(const QVector<DeckBuilderDeckModel::Entry> &cards)
{
    QVector<int> copies;

    for (int i = 0; i < cards.size(); ++i) {
        if (cards[i].quantity < 1 || cards[i].quantity > 50 - int(copies.size())) {
            return {};
        }

        for (int n = 0; n < cards[i].quantity; ++n)
            copies.append(i);
    }

    if (copies.isEmpty())
        return {};

    DeckPngPage page;
    page.sections = deckPngSections(cards, copies);
    page.copies = int(copies.size());

    if (!fitDeckPngPage(page))
        return {};

    return {page};
}

static QStringList deckPngPaths(const QString &path, int pages)
{
    if (pages == 1)
        return {path};
    const QFileInfo info(path);
    QStringList paths;
    for (int i = 1; i <= pages; ++i) {
        const QString fileName = info.completeBaseName()
                                 + QString("-%1.png").arg(i, 2, 10, QChar('0'));
        paths.append(info.absoluteDir().filePath(fileName));
    }
    return paths;
}

// Reuse only saved catalogues. Opening the builder never starts a manifest GET.
static QJsonObject deckManifestForEdition(const QString &edition)
{
    if (edition != QStringLiteral("jp") && edition != QStringLiteral("en")) {
        return {};
    }

    const auto mode = edition == QStringLiteral("en") ? Config::DetectLocaleMode::EN
                                                      : Config::DetectLocaleMode::JP;

    const QUrl url(Config::instance().getIndexManifestUrl(mode));

    const QString path = IndexCatalog::manifestCachePathForEdition(url, edition);

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError parseError;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value("indexes").isArray()) {
        return {};
    }

    return document.object();
}

int DeckBuilderDeckModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(entries_.size());
}

QVariant DeckBuilderDeckModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
        return {};
    const Entry &entry = entries_[index.row()];
    if (role == CardRole)
        return entry.card;
    if (role == QuantityRole)
        return entry.quantity;
    return {};
}

QHash<int, QByteArray> DeckBuilderDeckModel::roleNames() const
{
    return {{CardRole, "cardData"}, {QuantityRole, "copies"}};
}

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
{
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}

static QVariantMap deckStatistics(const QVector<DeckBuilderDeckModel::Entry> &entries)
{
    QVariantMap stats{{"character", 0},
                      {"event", 0},
                      {"climax", 0},
                      {"soulTriggers", 0},
                      {"level0", 0},
                      {"level1", 0},
                      {"level2", 0},
                      {"level3", 0}};

    for (const auto &entry : entries) {
        const QString kind = entry.card.value("cardKind").toString();
        const QString kindKey = kind == "CH"   ? "character"
                                : kind == "EV" ? "event"
                                : kind == "CX" ? "climax"
                                               : QString();

        if (!kindKey.isEmpty())
            stats[kindKey] = stats.value(kindKey).toInt() + entry.quantity;

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
        if (colors.contains(order[i])) {
            rank = i;
            ++found;
        }
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
    exportEntries(deck_.entries(), name_);
}

bool DeckBuilderController::exportEntries(const QVector<DeckBuilderDeckModel::Entry> &entries,
                                          const QString &deckName)
{
    if (exporting_ || editionChangePending_ || entries.isEmpty())
        return false;

    qint64 total = 0;
    for (const auto &entry : entries) {
        total += entry.quantity;

        if (total > 50) {
            QMessageBox::warning(dialogParent_,
                                 "PNG export",
                                 "PNG export supports a maximum of 50 cards, including copies.");
            return false;
        }
    }

    QVector<DeckBuilderDeckModel::Entry> snapshot = entries;
    const QString exportName = deckName;
    std::stable_sort(snapshot.begin(), snapshot.end(), [](const auto &a, const auto &b) {
        const bool aClimax = LocalCardStore::kind(a.card.value("cardKind").toString()) == "CX";
        const bool bClimax = LocalCardStore::kind(b.card.value("cardKind").toString()) == "CX";
        if (aClimax != bClimax)
            return !aClimax;
        return deckExportBefore(a, b);
    });
    const QVector<DeckPngPage> pages = deckPngPages(snapshot);
    if (pages.isEmpty()) {
        QMessageBox::warning(dialogParent_,
                             "PNG export",
                             "This collection cannot fit on one readable PNG. "
                             "Keep the number of cards to 50 and try again.");
        return false;
    }

    QString path = QFileDialog::getSaveFileName(dialogParent_,
                                                "Export deck image",
                                                "deck.png",
                                                "PNG image (*.png)",
                                                nullptr,
                                                QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";

    const QStringList paths = deckPngPaths(path, int(pages.size()));
    QStringList existing;
    for (const QString &output : paths) {
        if (QFileInfo::exists(output))
            existing.append(output);
    }
    if (!existing.isEmpty()
        && QMessageBox::question(dialogParent_,
                                 "Replace existing PNG files?",
                                 "These files already exist:\n\n" + existing.join('\n')
                                     + "\n\nReplace them?",
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel)
               != QMessageBox::Yes) {
        return false;
    }

    exportPath_ = path;
    exportPngPaths_ = paths;
    exportName_ = exportName;
    exportDeck_ = std::move(snapshot);
    exportImages_.clear();
    exportImages_.reserve(exportDeck_.size());
    exportIndex_ = 0;
    exporting_ = true;
    writingExport_ = false;
    ++exportGeneration_;
    emit stateChanged();
    nextExportImage();
    return true;
}

void DeckBuilderController::cancelExport()
{
    if (writingExport_)
        return;
    ++exportGeneration_;
    exporting_ = false;
    const quint64 id = exportRequest_;
    exportRequest_ = 0;
    if (id)
        NetGuard::instance().cancel(id);
    exportImages_.clear();
    exportDeck_.clear();
    exportPngPaths_.clear();
    setStatus("Image export cancelled.");
}

void DeckBuilderController::nextExportImage()
{
    if (!exporting_)
        return;
    if (exportIndex_ >= exportDeck_.size()) {
        renderExport();
        return;
    }
    setStatus(
        QString("Preparing deck image: %1 / %2").arg(exportIndex_ + 1).arg(exportDeck_.size()));
    const auto &entry = exportDeck_[exportIndex_];
    const quint64 serial = exportGeneration_;
    exportRequest_ = DeckBuilderImages::load(
        entry.card.value("picture").toString(),
        this,
        [this, serial](const QImage &image, const QString &error) {
            if (serial != exportGeneration_)
                return;
            exportRequest_ = 0;
            if (!exporting_)
                return;
            if (image.isNull()) {
                const QString code = exportDeck_[exportIndex_].card.value("cardCode").toString();
                exporting_ = false;
                exportImages_.clear();
                exportDeck_.clear();
                exportPngPaths_.clear();
                setStatus("Image export stopped at " + code + ": " + error);
                return;
            }
            QImage prepared = image;
            const bool climax = LocalCardStore::kind(
                                    exportDeck_[exportIndex_].card.value("cardKind").toString())
                                == "CX";
            if (climax && prepared.height() > prepared.width())
                prepared = prepared.transformed(QTransform().rotate(90), Qt::SmoothTransformation);

            exportImages_.append(prepared.scaled(climax ? 336 : 240,
                                                 climax ? 240 : 336,
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));

            ++exportIndex_;
            nextExportImage();
        });
}

void DeckBuilderController::renderExport()
{
    const QString name = exportName_;
    const QStringList paths = exportPngPaths_;
    const auto cards = exportDeck_;
    const auto images = exportImages_;
    exportDeck_.clear();
    exportImages_.clear();
    exportPngPaths_.clear();
    writingExport_ = true;
    setStatus("Writing deck image…");

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, paths] {
        const QString error = watcher->result();
        watcher->deleteLater();
        exporting_ = false;
        writingExport_ = false;
        if (!error.isEmpty()) {
            setStatus(error);
            QMessageBox::warning(dialogParent_, "PNG export failed", error);
            return;
        }
        const QString message = paths.size() == 1
                                    ? "Deck image saved: " + paths.first()
                                    : QString("Saved %1 PNG pages in:\n%2")
                                          .arg(paths.size())
                                          .arg(QFileInfo(paths.first()).absolutePath());
        setStatus(message);
        QMessageBox::information(dialogParent_, "PNG export complete", message);
    });

    watcher->setFuture(QtConcurrent::run([name, paths, cards, images]() -> QString {
        if (cards.isEmpty() || cards.size() != images.size())
            return "No complete deck image to export.";
        const QVector<DeckPngPage> pages = deckPngPages(cards);
        if (pages.isEmpty() || paths.size() != pages.size())
            return "The PNG page layout is not valid.";

        auto font = [](int pixels, bool bold = false) {
            QFont value("Segoe UI");
            value.setPixelSize(pixels);
            value.setBold(bold);
            return value;
        };

        const QVariantMap stats = deckStatistics(cards);
        const QStringList statKeys{"character",
                                   "event",
                                   "climax",
                                   "soulTriggers",
                                   "level0",
                                   "level1",
                                   "level2",
                                   "level3"};
        const QStringList statLabels{"Character",
                                     "Event",
                                     "Climax",
                                     "Trigger icons",
                                     "Lv 0",
                                     "Lv 1",
                                     "Lv 2",
                                     "Lv 3"};
        QStringList saved;
        auto failure = [&saved](const QString &message) {
            return saved.isEmpty() ? message
                                   : message + "\n\nPages already saved:\n" + saved.join('\n');
        };

        // Render and release one fixed-size canvas at a time.
        for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            const DeckPngPage &page = pages[pageIndex];
            QImage canvas(2000, 1800, QImage::Format_RGB32);
            if (canvas.isNull())
                return failure("Not enough memory to render this PNG.");
            canvas.fill(QColor("#20242b"));
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setPen(Qt::white);
            painter.setFont(font(28, true));
            painter.drawText(QRect(28, 16, 1944, 38),
                             Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(name, Qt::ElideRight, 1944));

            painter.setFont(font(17));
            for (int i = 0; i < statKeys.size(); ++i) {
                const int x = 28 + (i % 4) * 486;
                const int y = 64 + (i / 4) * 30;
                painter.fillRect(QRect(x, y + 5, 3, 17), QColor("#edbd58"));
                painter.setPen(QColor("#dce5ef"));
                painter.drawText(QRect(x + 12, y, 468, 27),
                                 Qt::AlignVCenter,
                                 statLabels[i] + ": "
                                     + QString::number(stats.value(statKeys[i]).toInt()));
            }

            for (const auto &section : page.sections) {
                painter.setPen(QColor("#b7c9e2"));
                painter.setFont(font(20, true));
                painter.drawText(QRect(28, section.top, 1944, 30), Qt::AlignVCenter, section.label);
                const int imageWidth = section.climax ? qRound(page.cardWidth * 1.4)
                                                      : page.cardWidth;
                const int imageHeight = section.climax ? page.cardWidth
                                                       : qRound(page.cardWidth * 1.4);
                for (int n = 0; n < section.indices.size(); ++n) {
                    const int index = section.indices[n];
                    const int x = 28 + (n % section.columns) * section.cellWidth;
                    const int y = section.top + 30 + (n / section.columns) * section.cellHeight;
                    const QImage &image = images[index];
                    if (image.isNull())
                        return failure("A card image is missing.");
                    const QSize fit = image.size().scaled(imageWidth,
                                                          imageHeight,
                                                          Qt::KeepAspectRatio);
                    const QRect target(x + (section.cellWidth - fit.width()) / 2,
                                       y + (imageHeight - fit.height()) / 2,
                                       fit.width(),
                                       fit.height());
                    painter.drawImage(target, image);

                    const QString code = cards[index].card.value("cardCode").toString();
                    int textSize = 14;
                    painter.setFont(font(textSize));
                    while (textSize > 9
                           && painter.fontMetrics().horizontalAdvance(code)
                                  > section.cellWidth - 8) {
                        painter.setFont(font(--textSize));
                    }
                    painter.setPen(QColor("#e4e9f0"));
                    painter.drawText(QRect(x + 4, y + imageHeight + 3, section.cellWidth - 8, 20),
                                     Qt::AlignHCenter | Qt::AlignVCenter,
                                     painter.fontMetrics().elidedText(code,
                                                                      Qt::ElideRight,
                                                                      section.cellWidth - 8));
                }
            }
            // Small branding footer, outside the card layout.
            {
                painter.save();

                const QString brandText = QStringLiteral("WSDeckImporter");

                const int rightMargin = 28;
                const int footerHeight = 36;
                const int footerY = canvas.height() - 52;
                const int iconSize = 30;
                const int iconGap = 10;

                QFont brandFont = painter.font();
                brandFont.setPixelSize(20);
                brandFont.setBold(true);
                painter.setFont(brandFont);

                const QImage brandIcon(QStringLiteral(":/icon/logo.svg"));

                const int textWidth = painter.fontMetrics().horizontalAdvance(brandText);

                const int iconSpace = brandIcon.isNull() ? 0 : iconSize + iconGap;

                const int totalWidth = iconSpace + textWidth;

                const int startX = canvas.width() - rightMargin - totalWidth;

                if (!brandIcon.isNull()) {
                    const QSize fittedSize = brandIcon.size().scaled(iconSize,
                                                                     iconSize,
                                                                     Qt::KeepAspectRatio);

                    const QRect iconRect(startX + (iconSize - fittedSize.width()) / 2,
                                         footerY + (footerHeight - fittedSize.height()) / 2,
                                         fittedSize.width(),
                                         fittedSize.height());

                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter.drawImage(iconRect, brandIcon);
                }

                painter.setRenderHint(QPainter::TextAntialiasing, true);
                painter.setPen(QColor("#aebdce"));

                painter.drawText(QRect(startX + iconSpace, footerY, textWidth, footerHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                 brandText);

                painter.restore();
            }
            painter.end();
            QSaveFile file(paths[pageIndex]);
            if (!file.open(QIODevice::WriteOnly))
                return failure("Could not open PNG: " + file.errorString());
            if (!canvas.save(&file, "PNG")) {
                file.cancelWriting();
                return failure("Could not encode PNG: " + paths[pageIndex]);
            }
            if (!file.commit())
                return failure("Could not save PNG: " + file.errorString());
            saved.append(paths[pageIndex]);
        }
        return {};
    }));
}

void DeckBuilderController::copyDeckCode()
{
    copyCodeEntries(deck_.entries());
}

bool DeckBuilderController::copyCodeEntries(const QVector<DeckBuilderDeckModel::Entry> &entries)
{
    if (exporting_)
        return false;

    auto reject = [this](const QString &message) {
        setStatus(message);
        QMessageBox::warning(dialogParent_, "Deck code", message);
    };

    if (entries.isEmpty()) {
        reject("Cannot share an empty deck.");
        return false;
    }

    qint64 total = 0;

    for (const auto &entry : entries)
        total += entry.quantity;

    if (total > 50) {
        reject("A deck may contain at most 50 cards in total.");
        return false;
    }

    QString error;
    const QSqlDatabase db = LocalCardStore::open(path_, &error);
    if (!db.isOpen()) {
        reject(error);
        return false;
    }

    QSqlQuery query(db);
    if (!query.prepare("SELECT global_id FROM cards "
                       "WHERE card_number=? COLLATE NOCASE LIMIT 2")) {
        reject("Cannot read global_id. Update the local database.\n" + query.lastError().text());
        return false;
    }

    DeckCode::Deck encoded;
    for (const auto &entry : entries) {
        const QString code = entry.card.value("cardCode").toString();
        query.bindValue(0, code);
        if (!query.exec()) {
            reject(query.lastError().text());
            return false;
        }
        if (!query.next()) {
            reject("Cannot resolve this card in the local database: " + code);
            return false;
        }
        bool ok = false;
        const qulonglong id = query.value(0).toString().toULongLong(&ok);
        if (!ok || id == 0 || id > std::numeric_limits<std::uint32_t>::max()) {
            reject("Invalid global_id for card: " + code);
            return false;
        }
        if (query.next() || query.lastError().isValid()) {
            reject("Cannot uniquely resolve card: " + code);
            return false;
        }
        query.finish();
        if (entry.quantity < 1 || entry.quantity > 50
            || !encoded.emplace(std::uint32_t(id), std::uint32_t(entry.quantity)).second) {
            reject("Invalid quantity or duplicate global_id: " + code);
            return false;
        }
    }

    std::string codecError;
    const std::string code = DeckCode::encode(encoded, codecError);
    if (code.empty()) {
        reject(QString::fromStdString(codecError));
        return false;
    }

    QGuiApplication::clipboard()->setText(QString::fromStdString(code));
    setStatus("Deck code copied to clipboard.");
    return true;
}

void DeckBuilderController::importDeckCode()
{
    if (exporting_)
        return;

    QInputDialog dialog(dialogParent_);
    dialog.setWindowTitle("Import deck code");
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setOption(QInputDialog::UsePlainTextEditForTextInput, true);
    dialog.setLabelText("Paste WSDeckImporter/1: deck code:");
    dialog.setTextValue(QString());
    dialog.setOkButtonText("Import");
    dialog.setCancelButtonText("Cancel");

    if (auto *editor = dialog.findChild<QPlainTextEdit *>()) {
        editor->setMinimumHeight(72);
        editor->setMaximumHeight(96);
        editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    dialog.resize(480, 180);

    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString text = dialog.textValue().trimmed();

    auto reject = [this](const QString &message) {
        setStatus(message);
        QMessageBox::warning(dialogParent_, "Deck code", message);
    };

    DeckCode::Deck decoded;
    std::string codecError;
    if (!DeckCode::decode(text.toStdString(), decoded, codecError)) {
        reject(QString::fromStdString(codecError));
        return;
    }

    QString error;
    const QSqlDatabase db = LocalCardStore::open(path_, &error);
    if (!db.isOpen()) {
        reject(error);
        return;
    }

    QSqlQuery query(db);
    if (!query.prepare("SELECT set_edition,card_number FROM cards WHERE global_id=? LIMIT 2")) {
        reject("Cannot read global_id. Update the local database.\n" + query.lastError().text());
        return;
    }

    QVector<DeckBuilderDeckModel::Entry> imported;
    QSet<QString> seenCodes;
    for (const auto &[id, quantity] : decoded) {
        query.bindValue(0, QVariant::fromValue<qlonglong>(id));
        if (!query.exec()) {
            reject(query.lastError().text());
            return;
        }
        if (!query.next()) {
            reject(QString("global_id %1 is missing from the local database. Update the database "
                           "and try again.")
                       .arg(qulonglong(id)));
            return;
        }
        const QString edition = query.value(0).toString();
        const QString code = query.value(1).toString();
        if (query.next() || query.lastError().isValid()) {
            reject(QString("global_id %1 is not unique in this database.").arg(qulonglong(id)));
            return;
        }
        query.finish();
        if ((edition != "jp" && edition != "en") || code.isEmpty() || seenCodes.contains(code)) {
            reject("Invalid or duplicate card identity in this database.");
            return;
        }
        const QVariantMap card = LocalCardStore::card(path_, edition, code, language_);
        if (card.isEmpty()) {
            reject("Could not load card details: " + code);
            return;
        }
        seenCodes.insert(code);
        imported.append(DeckBuilderDeckModel::Entry{card, int(quantity)});
    }
    // All IDs and card details are valid before touching the existing deck.
    if (!confirmDiscard())
        return;
    closeReference();
    name_ = "Imported deck";
    clearDeckHistory();
    deck_.replace(std::move(imported));
    selected_.clear();
    emit selectedChanged();
    changedDeck(); // Imported code has not been saved as a local TXT file.
    emit stateChanged();
    setStatus("Deck code imported. Save as TXT to keep a local copy.");
}

DeckBuilderController::DeckBuilderController(QString path, QWidget *dialogParent)
    : QObject(dialogParent)
    , path_(std::move(path))
    , deck_(this)
    , dialogParent_(dialogParent)
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
{
    status_ = status;
    emit stateChanged();
}

int DeckBuilderController::totalCards() const
{
    int total = 0;
    for (const auto &entry : deck_.entries())
        total += entry.quantity;
    return total;
}

int DeckBuilderController::climaxCards() const
{
    int total = 0;
    for (const auto &entry : deck_.entries())
        if (entry.card.value("cardKind") == "CX")
            total += entry.quantity;
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
        for (const auto &entry : deck_.entries())
            codes << entry.card.value("cardCode").toString();
        if (codes != referenceDeckCodes_)
            openReference(referenceKind_, referenceValue_);
    }
}

void DeckBuilderController::setDeckName(const QString &name)
{
    if (name_ == name)
        return;
    name_ = name;
    changedDeck();
    emit stateChanged();
}

int DeckBuilderController::quantity(const QString &code) const
{
    return deck_.quantity(code);
}

QString DeckBuilderController::imageSource(const QString &picture) const
{
    return DeckBuilderImages::source(picture);
}

void DeckBuilderController::selectCard(const QVariantMap &card)
{
    const QString code = card.value("cardCode").toString().trimmed();

    if (code.isEmpty())
        return;

    QString error;

    const QVariantMap resolved = LocalCardStore::cardByCode(path_, code, language_, &error);

    if (resolved.isEmpty()) {
        setStatus(error);
        return;
    }

    selected_ = resolved;
    emit selectedChanged();
}

void DeckBuilderController::addCard(const QVariantMap &card)
{
    changeQuantity(card, 1);
}

void DeckBuilderController::changeQuantity(const QVariantMap &card, int delta)
{
    if (exporting_ || editionChangePending_)
        return;

    const QString code = card.value("cardCode").toString().trimmed().toUpper();
    if (code.isEmpty())
        return;

    const int old = deck_.quantity(code);
    const qint64 wanted = qMax<qint64>(0, qint64(old) + delta);
    if (wanted == old)
        return;
    if (wanted > 50 || qint64(totalCards()) - old + wanted > 50) {
        setStatus("A deck may contain at most 50 cards in total.");
        return;
    }

    QVariantMap normalized;
    for (const auto &entry : deck_.entries()) {
        if (entry.card.value("cardCode").toString().trimmed().toUpper() == code) {
            normalized = entry.card;
            break;
        }
    }

    if (normalized.isEmpty()) {
        QString error;

        normalized = LocalCardStore::cardByCode(path_, code, language_, &error);

        if (normalized.isEmpty()) {
            setStatus(error);
            return;
        }
    }

    normalized.insert("cardCode", code);

    if ((old == 0) != (wanted == 0)) {
        undoEdits_.append(
            MembershipEdit{code, normalized.value("setEdition").toString(), old, int(wanted)});
        if (undoEdits_.size() > 100)
            undoEdits_.removeFirst();
        redoEdits_.clear();
    }

    deck_.setQuantity(normalized, int(wanted));
    changedDeck();
}

bool DeckBuilderController::confirmDiscard()
{
    if (exporting_) {
        QMessageBox::information(dialogParent_,
                                 "Deck export",
                                 "Finish or cancel the image export first.");
        return false;
    }

    if (!dirty_)
        return true;

    const auto answer = QMessageBox::question(dialogParent_,
                                              "Save deck?",
                                              "Save your deck before continuing?",
                                              QMessageBox::Save | QMessageBox::Discard
                                                  | QMessageBox::Cancel,
                                              QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveText();

    return answer == QMessageBox::Discard;
}

void DeckBuilderController::requestEdition(const QString &edition)
{
    if (editionChangePending_ || exporting_)
        return;

    const QString target = edition.trimmed().toLower();
    if ((target != "jp" && target != "en") || target == edition_)
        return;

    editionChangePending_ = true;
    emit stateChanged();

    QTimer::singleShot(0, this, [this, target] {
        setEdition(target);
        editionChangePending_ = false;
        emit stateChanged();
    });
}

bool DeckBuilderController::setEdition(const QString &edition)
{
    const QString target = edition.trimmed().toLower();

    if (target != QStringLiteral("jp") && target != QStringLiteral("en")) {
        return false;
    }

    if (target == edition_)
        return true;

    if (exporting_)
        return false;

    closeReference();
    edition_ = target;
    selected_.clear();
    emit selectedChanged();

    reload();
    emit stateChanged();

    return true;
}

void DeckBuilderController::setLanguage(const QString &language)
{
    if ((language != "EN" && language != "JP") || language == language_)
        return;

    closeReference();
    language_ = language;
    QVector<DeckBuilderDeckModel::Entry> refreshed = deck_.entries();

    for (auto &entry : refreshed) {
        const QVariantMap card = LocalCardStore::cardByCode(path_,
                                                            entry.card.value("cardCode").toString(),
                                                            language_);

        if (!card.isEmpty())
            entry.card = card;
    }

    deck_.replace(std::move(refreshed));
    changedDeck(dirty_);
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
        if (serial != titlesGeneration_)
            return;
        titlesRunning_ = false;
        titles_ = result.value("titles").toList();
        catalogueNotice_ = result.value("notice").toString();
        const QString error = result.value("error").toString();
        setStatus(!error.isEmpty()    ? error
                  : titles_.isEmpty() ? "No local cards are available in this edition."
                                      : "Choose a set.");
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

    if (!searchRunning_)
        dispatchSearch();
}

void DeckBuilderController::loadMore()
{
    if (busy() || title_.isEmpty() || !hasMore())
        return;

    loadError_.clear();
    searchPending_ = true;
    pendingAppend_ = true;
    dispatchSearch();
}

void DeckBuilderController::dispatchSearch()
{
    searchPending_ = false;
    if (title_.isEmpty()) {
        results_.clear();
        totalMatches_ = 0;
        emit resultsChanged();
        emit stateChanged();
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
    connect(watcher,
            &QFutureWatcher<QVariantMap>::finished,
            this,
            [this, watcher, serial, append, offset] {
                const QVariantMap result = watcher->result();
                watcher->deleteLater();
                searchRunning_ = false;
                loadingMore_ = false;
                if (serial == generation_) {
                    const QVariantList cards = result.value("cards").toList();
                    loadError_ = result.value("error").toString();
                    if (loadError_.isEmpty() && cards.isEmpty()
                        && result.value("total").toInt() > offset)
                        loadError_ = "The card database changed while loading. Reload the set.";
                    if (loadError_.isEmpty()) {
                        if (append)
                            results_.append(cards);
                        else
                            results_ = cards;
                        totalMatches_ = result.value("total").toInt();
                        setStatus(
                            QString("%1 of %2 cards loaded").arg(results_.size()).arg(totalMatches_));
                        emit resultsChanged();
                    } else {
                        setStatus(loadError_);
                    }
                }
                if (searchPending_)
                    dispatchSearch();
                else
                    emit stateChanged();
            });

    watcher->setFuture(QtConcurrent::run([path, edition, title, filters, language, offset] {
        const QVariantMap result
            = LocalCardStore::deckPage(path, edition, title, filters, language, offset, 60);
        LocalCardStore::releaseThreadConnection(path);
        return result;
    }));
}

void DeckBuilderController::setReferenceSort(const QString &group,
                                             const QString &sort,
                                             bool descending)
{
    if (group != "none" && group != "color" && group != "level")
        return;

    if (sort != "code" && sort != "name" && sort != "level" && sort != "power"
        && sort != "colorLevelCostPower")
        return;

    if (referenceGroup_ == group && referenceSort_ == sort && referenceDescending_ == descending)
        return;

    referenceGroup_ = group;
    referenceSort_ = sort;
    referenceDescending_ = descending;
    if (referenceOpen_)
        openReference(referenceKind_, referenceValue_);
}

void DeckBuilderController::setReferenceDeckOnly(bool enabled)
{
    if (referenceDeckOnly_ == enabled)
        return;

    referenceDeckOnly_ = enabled; // Session only; deliberately not written to Config.
    if (referenceOpen_)
        openReference(referenceKind_, referenceValue_);
    else
        emit referenceChanged();
}

void DeckBuilderController::openReference(const QString &kind, const QString &value)
{
    if ((!referenceDeckOnly_ && title_.isEmpty()) || value.trimmed().isEmpty()
        || (kind != "trait" && kind != "name" && kind != "ability"))
        return;

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

    if (!referenceRunning_)
        dispatchReference();
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
    if (referenceRunning_ || !referenceHasMore())
        return;
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
    connect(watcher,
            &QFutureWatcher<QVariantMap>::finished,
            this,
            [this, watcher, serial, append, offset] {
                const QVariantMap result = watcher->result();
                watcher->deleteLater();
                referenceRunning_ = false;
                if (serial == referenceGeneration_) {
                    const QVariantList cards = result.value("cards").toList();
                    referenceError_ = result.value("error").toString();
                    if (referenceError_.isEmpty() && cards.isEmpty()
                        && result.value("total").toInt() > offset)
                        referenceError_
                            = "The card database changed. Close and reopen this reference.";
                    if (referenceError_.isEmpty()) {
                        if (append)
                            referenceResults_.append(cards);
                        else
                            referenceResults_ = cards;
                        referenceTotal_ = result.value("total").toInt();
                    }
                    emit referenceChanged();
                }
                if (referencePending_)
                    dispatchReference();
                else
                    emit referenceChanged();
            });

    watcher->setFuture(QtConcurrent::run([path, edition, title, filters, language, offset] {
        const QVariantMap result
            = LocalCardStore::deckPage(path, edition, title, filters, language, offset, 60);
        LocalCardStore::releaseThreadConnection(path);
        return result;
    }));
}

bool DeckBuilderController::saveText()
{
    if (!saveEntries(deck_.entries(), edition_, name_))
        return false;
    changedDeck(false);
    return true;
}

bool DeckBuilderController::saveEntries(const QVector<DeckBuilderDeckModel::Entry> &entries,
                                        const QString &edition,
                                        const QString &deckName)
{
    Q_UNUSED(edition);

    if (entries.isEmpty()) {
        setStatus("There are no cards to export.");
        return false;
    }

    QString path = QFileDialog::getSaveFileName(dialogParent_,
                                                "Save deck",
                                                "deck.txt",
                                                "Deck text (*.txt)");
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".txt";

    QString cleanName = deckName;
    cleanName.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
    cleanName = cleanName.trimmed();
    if (cleanName.isEmpty())
        cleanName = "My Deck";

    QString text = "# WSDeckImporter Exported\n";
    text += "# name=" + QString::fromLatin1(QUrl::toPercentEncoding(cleanName)) + '\n';
    text += "Name: " + cleanName + '\n';
    text += "Date: " + QDateTime::currentDateTime().toString("HH:mm  dd-MM-yyyy") + '\n';
    text += "Sleeve: \n\nCards:\n";

    for (const auto &entry : entries) {
        const QString cardCode = entry.card.value("cardCode").toString().trimmed();
        if (cardCode.isEmpty() || cardCode.contains(QRegularExpression("\\s"))
            || entry.quantity < 1) {
            setStatus("Cannot export an invalid card entry.");
            return false;
        }
        for (int copy = 0; copy < entry.quantity; ++copy)
            text += cardCode + '\n';
    }

    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus("Could not save deck: " + file.errorString());
        return false;
    }

    setStatus("Deck saved: " + path);
    return true;
}

void DeckBuilderController::newDeck()
{
    if (!confirmDiscard())
        return;
    clearDeckHistory();
    deck_.replace({});
    name_ = "My Deck";
    changedDeck(false);
    emit stateChanged();
}

void DeckBuilderController::renameDeck()
{
    if (editionChangePending_)
        return;

    bool accepted = false;
    QString name = QInputDialog::getText(dialogParent_,
                                         "Rename deck",
                                         "Deck name:",
                                         QLineEdit::Normal,
                                         name_,
                                         &accepted)
                       .trimmed();
    if (!accepted)
        return;

    name.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
    if (name.isEmpty()) {
        QMessageBox::warning(dialogParent_, "Rename deck", "Enter a deck name.");
        return;
    }
    setDeckName(name);
}

bool DeckBuilderController::confirmReplaceDeck(const QString &title,
                                               const QString &message,
                                               const QString &actionText)
{
    if (deck_.entries().isEmpty() && !dirty_)
        return true;

    QMessageBox box(QMessageBox::Warning, title, message, QMessageBox::NoButton, dialogParent_);
    auto *save = box.addButton("Save and " + actionText.toLower(), QMessageBox::AcceptRole);
    auto *replace = box.addButton(actionText, QMessageBox::DestructiveRole);
    auto *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();

    if (box.clickedButton() == save)
        return saveText();

    return box.clickedButton() == replace;
}

bool DeckBuilderController::replaceCards(const QVariantList &rows, const QString &edition)
{
    Q_UNUSED(edition);

    auto reject = [this](const QString &message) {
        QMessageBox::warning(dialogParent_, "Copy to My Deck", message);
        return false;
    };

    if (exporting_ || editionChangePending_)
        return reject("Finish the current operation first.");
    if (rows.isEmpty())
        return reject("There are no detected cards to copy.");

    QVector<DeckBuilderDeckModel::Entry> candidate;
    QHash<QString, int> positions;
    qint64 total = 0;

    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString code = row.value("cardCode").toString().trimmed().toUpper();
        bool ok = false;
        const qlonglong copies = row.value("copies").toString().toLongLong(&ok);

        if (code.isEmpty() || !ok || copies < 1 || copies > 50 || copies > 50 - total)
            return reject("The detected list must contain between 1 and 50 cards in total.");

        QString lookupError;

        const QVariantMap card = LocalCardStore::cardByCode(path_, code, language_, &lookupError);

        if (card.isEmpty())
            return reject(lookupError);

        total += copies;
        if (positions.contains(code)) {
            candidate[positions.value(code)].quantity += int(copies);
        } else {
            positions.insert(code, int(candidate.size()));
            candidate.append(DeckBuilderDeckModel::Entry{card, int(copies)});
        }
    }

    if (!confirmReplaceDeck("Replace My Deck?",
                            "Copying detected cards will replace every card currently in My Deck. "
                            "Save the current deck first?",
                            "Replace"))
        return false;

    closeReference();

    name_ = "Detected cards";
    clearDeckHistory();

    deck_.replace(std::move(candidate));

    selected_.clear();
    emit selectedChanged();

    changedDeck();
    emit stateChanged();

    setStatus("Detected cards copied to My Deck.");
    return true;
}

bool DeckBuilderController::exportSnapshot(const QVariantList &rows,
                                           const QString &edition,
                                           const QString &name,
                                           const QString &format)
{
    if (exporting_)
        return false;

    if (rows.isEmpty() || rows.size() > 1000) {
        setStatus("Select between 1 and 1000 distinct rows to export.");
        return false;
    }

    QVector<DeckBuilderDeckModel::Entry> entries;
    QHash<QString, int> positions;

    for (const QVariant &value : rows) {
        QVariantMap card = value.toMap();
        const QString code = card.value("cardCode").toString().trimmed().toUpper();
        bool ok = false;
        const qlonglong copies = card.value("copies").toString().toLongLong(&ok);

        if (code.isEmpty() || !ok || copies < 1 || copies > 999) {
            setStatus("Invalid card code or quantity in the collection.");
            return false;
        }

        card.insert("cardCode", code);
        card.remove("copies");

        if (positions.contains(code)) {
            auto &entry = entries[positions.value(code)];

            if (copies > 999 - entry.quantity) {
                setStatus("An exported quantity exceeds 999.");
                return false;
            }

            entry.quantity += int(copies);
        } else {
            positions.insert(code, int(entries.size()));
            entries.append(DeckBuilderDeckModel::Entry{card, int(copies)});
        }
    }

    if (format == "txt")
        return saveEntries(entries, edition, name);

    if (format == "png")
        return exportEntries(entries, name);

    if (format == "code")
        return copyCodeEntries(entries);

    setStatus("Unsupported export format.");
    return false;
}

QVariantList DeckBuilderController::deckRows() const
{
    QVariantList rows;

    for (const auto &entry : deck_.entries()) {
        QVariantMap row = entry.card;
        row.insert("copies", entry.quantity);
        rows.append(row);
    }

    return rows;
}

void DeckBuilderController::openText()
{
    if (exporting_)
        return;

    const QString filePath = QFileDialog::getOpenFileName(dialogParent_,
                                                          "Open deck",
                                                          {},
                                                          "Deck text (*.txt)");
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
        setStatus("Unable to open this deck file (maximum 2 MiB).");
        return;
    }

    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    const QStringList lines = text.split('\n');
    QString name = QFileInfo(filePath).completeBaseName();
    bool sectioned = false;
    bool declaredSectionFormat = false;

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line == "Cards:")
            sectioned = true;
        if (line == "# WSDeckImporter Exported" || line == "# WeissSim Deck")
            declaredSectionFormat = true;
        if (line.startsWith("# name="))
            name = QUrl::fromPercentEncoding(line.mid(7).toUtf8());
    }

    if (declaredSectionFormat && !sectioned) {
        setStatus("This deck file is missing its Cards: section.");
        return;
    }

    // Recognize category headings in grouped deck exports.
    const auto isCategoryHeading = [](QString value) {
        value = value.trimmed().toCaseFolded();

        if (value.endsWith(QLatin1Char(':')))
            value.chop(1);

        value = value.trimmed();

        return value == QStringLiteral("characters") || value == QStringLiteral("events")
               || value == QStringLiteral("climaxes");
    };

    bool groupedFormat = false;
    int groupedTitleLine = 0;

    // Existing Cards: files continue through their existing parser.
    // Otherwise recognize either:
    //   deck name + category heading
    // or:
    //   category heading with no deck-name line
    if (!sectioned) {
        int firstContentLine = 0;
        QString firstContent;

        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();

            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            if (firstContentLine == 0) {
                if (isCategoryHeading(line)) {
                    groupedFormat = true;
                    break;
                }

                firstContentLine = i + 1;
                firstContent = line;
                continue;
            }

            if (isCategoryHeading(line)) {
                groupedFormat = true;
                groupedTitleLine = firstContentLine;
                name = firstContent;
            }

            break;
        }
    }

    QVector<DeckBuilderDeckModel::Entry> imported;
    QHash<QString, int> positions;
    int importedTotal = 0;
    bool insideCards = !sectioned;
    static const QRegularExpression quantityRow("^([1-9][0-9]{0,2})\\s+(\\S+)(?:\\s+.*)?$");
    static const QRegularExpression bareCode("^[^\\s:]+/[^\\s:]+$");
    static const QRegularExpression codeQuantityRow(
        QStringLiteral(R"(^([^\s:]+/[^\s:]+)\s+([1-9][0-9]{0,2})(?:\s+.*)?$)"));

    int lineNumber = 0;
    for (const QString &raw : lines) {
        ++lineNumber;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        if (groupedFormat) {
            if (lineNumber == groupedTitleLine || isCategoryHeading(line))
                continue;
        }

        if (!insideCards) {
            if (line == "Cards:")
                insideCards = true;
            else if (line.startsWith("Name:"))
                name = line.mid(5).trimmed();
            else if (!line.startsWith("Date:") && !line.startsWith("Sleeve:")) {
                setStatus(QString("Invalid deck header on line %1.").arg(lineNumber));
                return;
            }
            continue;
        }

        QString code;
        int copies = 1;

        const auto quantityFirst = quantityRow.match(line);
        const auto codeFirst = codeQuantityRow.match(line);

        if (quantityFirst.hasMatch()) {
            // Existing format: quantity, code, optional name.
            copies = quantityFirst.captured(1).toInt();
            code = quantityFirst.captured(2).toUpper();

        } else if (codeFirst.hasMatch()) {
            // Additional format: code, quantity, optional name.
            code = codeFirst.captured(1).toUpper();
            copies = codeFirst.captured(2).toInt();

        } else if (bareCode.match(line).hasMatch()) {
            // Existing simulator format: one code per copy.
            code = line.toUpper();

        } else {
            setStatus(QString("Invalid deck entry on line %1.").arg(lineNumber));
            return;
        }

        if (copies > 50 - importedTotal) {
            setStatus("A deck may contain at most 50 cards in total.");
            return;
        }
        importedTotal += copies;

        if (positions.contains(code)) {
            imported[positions.value(code)].quantity += copies;
        } else {
            QString lookupError;

            const QVariantMap card = LocalCardStore::cardByCode(path_,
                                                                code,
                                                                language_,
                                                                &lookupError);

            if (card.isEmpty()) {
                setStatus(lookupError);
                return;
            }
            positions.insert(code, int(imported.size()));
            imported.append(DeckBuilderDeckModel::Entry{card, copies});
        }
    }

    if (imported.isEmpty()) {
        setStatus("Cannot import an empty deck.");
        return;
    }
    if (!confirmDiscard())
        return;

    closeReference();
    name_ = name.trimmed().isEmpty() ? QStringLiteral("My Deck") : name.trimmed();
    clearDeckHistory();
    deck_.replace(std::move(imported));
    selected_.clear();
    emit selectedChanged();
    changedDeck(false);
    setStatus("Opened deck: " + filePath);
}

//undo/redo methods
void DeckBuilderController::clearDeckHistory()
{
    undoEdits_.clear();
    redoEdits_.clear();
}

bool DeckBuilderController::applyMembershipEdit(const MembershipEdit &edit, bool undo)
{
    if (exporting_ || editionChangePending_)
        return false;

    const int wanted = undo ? edit.before : edit.after;
    const int current = deck_.quantity(edit.code);
    if (wanted < 0 || wanted > 50 || qint64(totalCards()) - current + wanted > 50) {
        setStatus(
            "Cannot restore this card: My Deck would exceed 50 cards. Make room and try again.");
        return false;
    }

    QVariantMap card;
    if (wanted > 0) {
        card = LocalCardStore::card(path_, edit.edition, edit.code, language_);
        if (card.isEmpty()) {
            setStatus("Cannot restore card details: " + edit.code);
            return false;
        }
    } else {
        card.insert("cardCode", edit.code);
        card.insert("setEdition", edit.edition);
    }

    deck_.setQuantity(card, wanted);
    return true;
}

void DeckBuilderController::undoDeckEdit()
{
    if (undoEdits_.isEmpty())
        return;
    MembershipEdit edit = undoEdits_.last();
    if (edit.before == 0)
        edit.after = deck_.quantity(edit.code);
    if (!applyMembershipEdit(edit, true))
        return;
    undoEdits_.removeLast();
    redoEdits_.append(edit);
    changedDeck();
    setStatus("Undid card addition/removal.");
}

void DeckBuilderController::redoDeckEdit()
{
    if (redoEdits_.isEmpty())
        return;
    MembershipEdit edit = redoEdits_.last();
    if (edit.after == 0)
        edit.before = deck_.quantity(edit.code);
    if (!applyMembershipEdit(edit, false))
        return;
    redoEdits_.removeLast();
    undoEdits_.append(edit);
    changedDeck();
    setStatus("Redid card addition/removal.");
}