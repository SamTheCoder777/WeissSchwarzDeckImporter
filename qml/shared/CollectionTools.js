.pragma library
.import "ReferenceText.js" as References

function colorRank(card) {
    var raw = String(card.color || "").replace(/\\\./g, ".").toUpperCase();
    var names = ["RED", "YELLOW", "GREEN", "BLUE"];
    var found = [];
    for (var i = 0; i < names.length; ++i)
        if (new RegExp("(^|[^A-Z])" + names[i] + "([^A-Z]|$)").test(raw)) found.push(i);
    return found.length > 1 ? 4 : found.length ? found[0] : 5;
}
function numberCompare(a, b, descending) {
    var av = /^[0-9]+$/.test(String(a === undefined ? "" : a).trim());
    var bv = /^[0-9]+$/.test(String(b === undefined ? "" : b).trim());
    if (av !== bv) return av ? -1 : 1;
    if (!av) return 0;
    var diff = Number(a) - Number(b);
    return descending ? -diff : diff;
}
function isClimax(card) {
    return String(card.cardKind || "").toUpperCase() === "CX";
}
function level(card) {
    if (isClimax(card)) return "";
    var value = String(card.level === undefined ? "" : card.level).trim();
    return /^[0-9]+$/.test(value) ? String(Number(value)) : "";
}
function sortRows(rows, group, sort, descending) {
    var result = (rows || []).slice();
    result.sort(function(a, b) {
        var compare = 0;
        if (group === "color") compare = colorRank(a) - colorRank(b);
        if (group === "level") {
            if (isClimax(a) !== isClimax(b)) return isClimax(a) ? 1 : -1;
            compare = numberCompare(level(a), level(b), descending);
        }
        if (compare) return compare;
        if (sort === "composite") {
            compare = colorRank(a) - colorRank(b);
            if (compare) return compare;
            for (var i = 0, fields = ["level", "cost", "power"]; i < fields.length; ++i) {
                compare = numberCompare(a[fields[i]], b[fields[i]], descending);
                if (compare) return compare;
            }
        } else if (sort === "level" || sort === "power") {
            compare = numberCompare(a[sort], b[sort], descending);
            if (compare) return compare;
        } else if (sort === "name") {
            var an = String(a.cardName || "").toLowerCase();
            var bn = String(b.cardName || "").toLowerCase();
            compare = an < bn ? -1 : an > bn ? 1 : 0;
            if (compare) return descending ? -compare : compare;
        }
        var ac = String(a.cardCode || "").toLowerCase();
        var bc = String(b.cardCode || "").toLowerCase();
        compare = ac < bc ? -1 : ac > bc ? 1 : 0;
        return sort === "code" && descending ? -compare : compare;
    });
    return result;
}
function matches(rows, kind, value) {
    var pattern = kind === "ability" ? References.abilityPattern(value) : null;
    var term = String(value || "").trim().toLowerCase();
    if (!term) return [];
    return (rows || []).filter(function(card) {
        if (kind === "name") return References.nameMatches(value, card.cardName || "");
        if (kind === "trait") return [card.feature1, card.feature2, card.feature3].some(function(t) {
            return String(t || "").trim().toLowerCase() === term;
        });
        return kind === "ability" && [card.text, card.originalText, card.translatedText].some(function(t) {
            return References.abilityMatchesPattern(pattern, t || "");
        });
    });
}
function visualRows(rows, group, sort, descending, columns, collapsed) {
    var sorted = sortRows(rows, group, sort, descending);
    var groups = [], current;
    for (var i = 0; i < sorted.length; ++i) {
        var card = sorted[i];
        var key = group === "color" ? String(colorRank(card))
                : group === "level" ? (isClimax(card) ? "cx" : level(card) === "" ? "unknown" : level(card)) : "all";
        var label = group === "color" ? ["Red", "Yellow", "Green", "Blue", "Multicolor", "Other"][Number(key)]
                  : group === "level" ? (key === "cx" ? "Climax" : key === "unknown" ? "Other" : "Level " + key) : "Cards";
        if (!current || current.key !== key) {
            current = {key:key, label:label, cards:[]};
            groups.push(current);
        }
        current.cards.push(card);
    }
    var result = [];
    for (var g = 0; g < groups.length; ++g) {
        current = groups[g];
        if (group !== "none") result.push({header:true, key:current.key, label:current.label, count:current.cards.length, cards:[]});
        if (group !== "none" && collapsed[current.key]) continue;
        for (i = 0; i < current.cards.length; i += columns)
            result.push({header:false, key:current.key, label:"", count:0, cards:current.cards.slice(i, i + columns)});
    }
    return result;
}
