.pragma library
.import "TriggerIcons.js" as Triggers

function escapeHtml(value) {
    return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}
function compactName(value) {
    var term = String(value || "").trim();
    if (term.charAt(0) === "「" && term.charAt(term.length - 1) === "」")
        term = term.slice(1, -1);
    return term.replace(/\s+/g, "").toLowerCase();
}
function nameMatches(value, text) {
    var term = compactName(value);
    return term.length > 0 && compactName(text).indexOf(term) !== -1;
}
function normalizedAbilityText(value) {
    return String(value || "").replace(/<[/\\]?\s*br\s*[/\\]?>/gi, " ").replace(/[【】\u00a0\u3000]/g, " ");
}
function abilityPattern(value) {
    var term = String(value || "").trim();
    if (term.charAt(0) === "『" && term.charAt(term.length - 1) === "』")
        term = term.slice(1, -1);
    term = normalizedAbilityText(term).trim();
    var pieces = term.match(/[A-Za-z_]+|[0-9]+|[^\s]/g) || [];
    if (!pieces.length)
        return null;
    for (var i = 0; i < pieces.length; ++i)
        pieces[i] = pieces[i].replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    var before = /^[A-Za-z0-9_]/.test(term) ? "(^|[^A-Za-z0-9_])" : "";
    var after = /[A-Za-z0-9_]$/.test(term) ? "(?=$|[^A-Za-z0-9_])" : "";
    return new RegExp(before + pieces.join("\\s*") + after, "i");
}
function abilityMatchesPattern(pattern, text) {
    return !!pattern && pattern.test(normalizedAbilityText(text));
}
function abilityMatches(value, text) {
    return abilityMatchesPattern(abilityPattern(value), text);
}

function referenceHtml(value, hoveredReference) {
    var input = String(value || "").replace(/<[/\\]?\s*br\s*[/\\]?>/gi, "\n");

    function link(kind, term, visible) {
        if (!String(term).trim())
            return escapeHtml(visible);

        var color = kind === "trait" ? "#77d9a0" : kind === "name" ? "#80baff" : "#efc779";

        var background = kind === "trait" ? "#294638" : kind === "name" ? "#293e56" : "#49402b";

        var href = "cardref:" + kind + ":" + encodeURIComponent(term);
        var style = "color:" + color + "; text-decoration:none;";

        if (href === hoveredReference)
            style += " background-color:" + background + ";";

        return '<a href="' + escapeHtml(href) + '" style="' + style + '">' + escapeHtml(visible) + '</a>';
    }

    function abilityEnd(text, start) {
        var depth = 0;

        for (var i = start; i < text.length; ++i) {
            if (text.charAt(i) === "『")
                ++depth;
            else if (text.charAt(i) === "』" && --depth === 0)
                return i;
        }

        return -1;
    }

    function render(text, outerAbility, depth) {
        function ordinary(chunk) {
            return outerAbility === null ? escapeHtml(chunk) : link("ability", outerAbility, chunk);
        }

        if (depth > 32)
            return ordinary(text);

        var pattern = /(【CONT】[ \t\u00a0\u3000]*【LINK】[ \t\u00a0\u3000]*)(「[^「」\r\n]+」|『[^『』\r\n]+』|[^【\r\n]+)|《([^《》\r\n]+)》|「([^「」\r\n]+)」|『/g;

        var output = "";
        var previous = 0;
        var match;

        while ((match = pattern.exec(text)) !== null) {
            output += ordinary(text.slice(previous, match.index));

            if (match[1] !== undefined) {
                // Preserve the existing CONT + LINK ability-search rule.
                output += ordinary(match[1]);
                output += link("ability", match[2].trim(), match[2]);
            } else if (match[3] !== undefined || match[4] !== undefined) {
                var trait = match[3] !== undefined;
                var prefix = text.slice(0, match.index).replace(/[ \t\u00a0\u3000]+$/, "");
                var followsLink = prefix.endsWith("【LINK】");
                output += link(followsLink ? "ability" : trait ? "trait" : "name", followsLink ? match[0] : trait ? match[3] : match[4], match[0]);
            } else {
                var end = abilityEnd(text, match.index);

                if (end < 0) {
                    output += ordinary(match[0]);
                } else {
                    var term = text.slice(match.index + 1, end);

                    if (!term.trim()) {
                        output += ordinary(text.slice(match.index, end + 1));
                    } else {
                        output += link("ability", term, "『");
                        output += render(term, term, depth + 1);
                        output += link("ability", term, "』");
                    }

                    pattern.lastIndex = end + 1;
                }
            }

            previous = pattern.lastIndex;
        }

        return output + ordinary(text.slice(previous));
    }

    var html = render(input, null, 0).replace(/\r\n?/g, "\n").replace(/\n/g, "<br>");

    return Triggers.replaceMarkers(html);
}
