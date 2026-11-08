.pragma library
.import "TriggerIcons.js" as Triggers

function escapeHtml(value) {
    return String(value).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
        .replace(/"/g,"&quot;").replace(/'/g,"&#39;");
}
function compactName(value) {
    var term=String(value || "").trim();
    if(term.charAt(0)==="「" && term.charAt(term.length-1)==="」") term=term.slice(1,-1);
    return term.replace(/\s+/g, "").toLowerCase();
}
function nameMatches(value, text) {
    var term=compactName(value);
    return term.length>0 && compactName(text).indexOf(term)!==-1;
}
function normalizedAbilityText(value) {
    return String(value || "").replace(/<[/\\]?\s*br\s*[/\\]?>/gi, " ").replace(/[【】\u00a0\u3000]/g," ");
}
function abilityPattern(value) {
    var term=String(value || "").trim();
    if (term.charAt(0)==="『" && term.charAt(term.length-1)==="』") term=term.slice(1,-1);
    term=normalizedAbilityText(term).trim();
    var pieces=term.match(/[A-Za-z_]+|[0-9]+|[^\s]/g) || [];
    if (!pieces.length) return null;
    for(var i=0;i<pieces.length;++i) pieces[i]=pieces[i].replace(/[.*+?^${}()|[\]\\]/g,"\\$&");
    var before=/^[A-Za-z0-9_]/.test(term)?"(^|[^A-Za-z0-9_])":"";
    var after=/[A-Za-z0-9_]$/.test(term)?"(?=$|[^A-Za-z0-9_])":"";
    return new RegExp(before+pieces.join("\\s*")+after,"i");
}
function abilityMatchesPattern(pattern, text) {
    return !!pattern && pattern.test(normalizedAbilityText(text));
}
function abilityMatches(value, text) { return abilityMatchesPattern(abilityPattern(value),text); }

function referenceHtml(value, hoveredReference) {
    var input=String(value || "").replace(/<[/\\]?\s*br\s*[/\\]?>/gi,"\n");
    var pattern=/(【CONT】[ \t\u00a0\u3000]*【LINK】[ \t\u00a0\u3000]*)(「[^「」\r\n]+」|『[^『』\r\n]+』|[^【\r\n]+)|《([^《》\r\n]+)》|「([^「」\r\n]+)」|『([^『』\r\n]+)』/g;
    var output="",previous=0,match;
    while((match=pattern.exec(input))!==null) {
        output+=escapeHtml(input.slice(previous,match.index));
        var kind,term,visible;
        if(match[1]!==undefined) {
            output+=escapeHtml(match[1]);
            visible=match[2];term=visible.trim();kind="ability";
            // A quoted LINK title keeps its quotes and remains an ability reference.
        } else {
            visible=match[0];
            kind=match[3]!==undefined?"trait":match[4]!==undefined?"name":"ability";
            term=match[3]!==undefined?match[3]:match[4]!==undefined?match[4]:match[5];
            // An unpaired LINK definition must not turn its following title into a reference.
            if(/【LINK】[ \t\u00a0\u3000]*$/.test(input.slice(0,match.index))) {
                output+=escapeHtml(visible);previous=pattern.lastIndex;continue;
            }
        }
        if(!term.trim()) { output+=escapeHtml(visible);previous=pattern.lastIndex;continue; }
        var color=kind==="trait"?"#77d9a0":kind==="name"?"#80baff":"#efc779";
        var background=kind==="trait"?"#294638":kind==="name"?"#293e56":"#49402b";
        var link="cardref:"+kind+":"+encodeURIComponent(term);
        var style="color:"+color+"; text-decoration:none;";
        if(link===hoveredReference) style+=" background-color:"+background+";";
        output+='<a href="'+escapeHtml(link)+'" style="'+style+'">'+escapeHtml(visible)+'</a>';
        previous=pattern.lastIndex;
    }
    output+=escapeHtml(input.slice(previous));
    return Triggers.replaceMarkers(output.replace(/\r\n?/g,"\n").replace(/\n/g,"<br>"));
}
