.pragma library

var labels = {DISCOVERY:"Discovery", CHANCE:"Chance", SOUL:"Soul", CHOICE:"Choice", DRAW:"Draw", FOCUS:"Focus", GATE:"Gate", COMEBACK:"Comeback",
              POOL:"Pool", RETURN:"Return", TREASURE:"Treasure", SHOT:"Shot", STANDBY:"Standby"};
var aliases = {BOOK:"DRAW", STOCK:"POOL", SALVAGE:"COMEBACK", PANTS:"GATE", BAG:"POOL", BOUNCE:"RETURN", BAR:"TREASURE"};
function canonical(value) {
    var key = String(value || "").toUpperCase();
    key = aliases[key] || key;
    return labels[key] ? key : "";
}
function tokens(value) {
    // Keep duplicates: two Soul symbols must render as two images.
    var input = String(value || "").replace(/\\\./g, ".");
    var parts = input.match(/\[\[([A-Za-z0-9_-]+)\.gif\]\]|\[([A-Za-z0-9_-]+)\]|[A-Za-z0-9_-]+/gi) || [];
    var out = [];
    for (var i = 0; i < parts.length; ++i) {
        var key = canonical(parts[i].replace(/^\[\[|\]\]$/g, "").replace(/^\[|\]$/g, "").replace(/\.gif$/i, ""));
        if (key) out.push(key);
    }
    return out;
}
function source(key) {
    key = canonical(key);
    var files = {DRAW:"draw", RETURN:"bounce", COMEBACK:"salvage", POOL:"stock"};
    return key ? "qrc:/icon/triggers/" + (files[key] || key.toLowerCase()) + ".gif" : "";
}
function label(key) { return labels[canonical(key)] || ""; }
function replaceMarkers(html, iconSize) {
    var size = Math.max(8, Math.min(22, Number(iconSize) || 14));
    // Only explicit tokens become images; ordinary words and reference URLs stay intact.
    return String(html || "").replace(/\[\[([A-Za-z0-9_-]+)(?:\\?\.)gif\]\]|\[([A-Za-z0-9_-]+)\]/gi,
        function(full, gifName, shortName) {
            var key = canonical(gifName || shortName);
            return key ? '<img src="' + source(key) + '" width="' + size + '" height="' + size + '" alt="' + label(key) + '">' : full;
        });
}

function inlineText(value, iconSize) {
    var text = String(value === undefined || value === null ? "" : value)
        .replace(/<br\s*\/?>/gi, "\n")
        .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;").replace(/'/g, "&#39;")
        .replace(/\r\n?/g, "\n").replace(/\n/g, "<br>");
    return replaceMarkers(text, iconSize);
}

function filterOptions(values) {
    // Same canonicalize -> unique -> sort policy as DatabaseUtil::distinctTriggers().
    var keys = [];
    for (var i = 0; i < values.length; ++i) {
        var parsed = tokens(values[i]);
        for (var j = 0; j < parsed.length; ++j)
            if (keys.indexOf(parsed[j]) < 0) keys.push(parsed[j]);
    }
    keys.sort();
    var result = [{value:"", label:"Any trigger"}, {value:"none", label:"No trigger"}];
    for (var k = 0; k < keys.length; ++k)
        result.push({value:keys[k], label:label(keys[k])});
    return result;
}
