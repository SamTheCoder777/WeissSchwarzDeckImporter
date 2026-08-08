#pragma once
// OfficialFallback — when EncoreDecks lacks a card, fetch it from the official
// WS API, reshape it to the EncoreDecks card layout (Japanese in the NP block),
// store it in cardList.db, and cache its image. After that first fetch the card
// flows through the normal cardDataFor / image-provider path like any other.
//
// Official data URL:
//   https://ws-tcg.com/manage/CardListUser/searchJson?keyword=<urlenc cardcode>
// Official image URL (derivable from cardcode, no API needed):
//   https://ws-tcg.com/wordpress/wp-content/images/cardlist/<L>/<set>/<code>.png

#include <QObject>
#include <QString>
#include <QJsonObject>

class OfficialFallback {
public:
    // Build the official image URL purely from a cardcode (no network).
    // "BD/W54-061SPMb" -> ".../cardlist/b/bd_w54/bd_w54_061spmb.png"
    static QString imageUrlFromCardcode(const QString& cardcode);

    // Build the official data query URL for a cardcode.
    static QString dataUrlFromCardcode(const QString& cardcode);

    // Reshape one official searchJson item into an EncoreDecks-style card object
    // (cardcode, set, cardtype, colour, level/cost/power/soul, rarity, trigger,
    //  imagepath, EN:{}, NP:{name,ability[],flavor,attributes[]}, _source).
    static QJsonObject reshapeOfficialItem(const QJsonObject& item);
};
