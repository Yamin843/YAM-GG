// ===========================================================================
// YAM-GG bootstrap.js — full type-aware Java layer
// ===========================================================================
(function () {
    "use strict";
    try { send({type:"boot_top_v7"}); } catch (e) {}

    // ─── Handles ──────────────────────────────────────────────────────
    var table = Object.create(null);
    var nextHandle = 1;
    function alloc(o) { var id = nextHandle++; table[id] = o; return id; }
    function get(id) { return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null; }
    function drop(id) { if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id]; }
    function dropAll() { table = Object.create(null); nextHandle = 1; }

    var LB = "\u005B";
    var RB = "\u005D";

    // ─── Reply envelope ───────────────────────────────────────────────
    function reply(id, ok, kind, value, handle, error) {
        var m = { id: id, ok: !!ok };
        if (kind) m.kind = kind;
        if (value !== undefined && value !== null) m.result = value;
        if (handle !== undefined && handle !== null) m.handle = handle;
        if (error) m.error = String(error);
        try { send({type:"reply", payload:JSON.stringify(m)}); } catch (e) {}
    }
    function replyHandle(id, h) { reply(id, true, "handle", null, h); }
    function replyValue(id, v)  { reply(id, true, "value", v, null); }
    function replyVoid(id)      { reply(id, true, "void", null, null); }
    function replyError(id, e)  { reply(id, false, "error", null, null,
                                        (e && e.message) ? e.message : String(e)); }

    // ═══════════════════════════════════════════════════════════════
    //  TYPE CLASSIFIER — every Java type we know
    // ═══════════════════════════════════════════════════════════════
    function classifyType(typeName) {
        if (!typeName) return "object";
        switch (typeName) {
            case "void": return "void";
            case "int": return "int";
            case "long": return "long";
            case "short": return "short";
            case "byte": return "byte";
            case "float": return "float";
            case "double": return "double";
            case "boolean": return "boolean";
            case "char": return "char";
            case "java.lang.String": return "string";
            case "java.lang.CharSequence": return "string";
            case "java.lang.Integer": case "java.lang.Long":
            case "java.lang.Short": case "java.lang.Byte":
            case "java.lang.Float": case "java.lang.Double":
            case "java.lang.Number": case "java.lang.Boolean":
            case "java.lang.Character": return "boxed";
            case "[B": return "byte_array";
            case "[I": case "[S": case "[J": case "[F":
            case "[D": case "[C": case "[Z": return "prim_array";
            case "java.util.Date": case "java.sql.Timestamp":
                return "date";
            case "java.io.File": return "file";
            case "android.graphics.Color": return "color";
            case "org.json.JSONObject": case "org.json.JSONArray":
                return "json";
            case "android.content.Intent": return "intent";
            case "android.os.Bundle": return "bundle";
            case "android.content.Context": return "context";
        }
        if (typeName.charAt(0) === LB) return "object_array";
        if (typeName.indexOf("Map") >= 0) return "map";
        if (typeName.indexOf("Set") >= 0) return "set";
        if (typeName.indexOf("List") >= 0) return "list";
        if (typeName.indexOf("Collection") >= 0 ||
            typeName.indexOf("Iterable") >= 0) return "list";
        if (typeName.indexOf("Runnable") >= 0 ||
            typeName.indexOf("Listener") >= 0 ||
            typeName.indexOf("Callback") >= 0) return "interface";
        try {
            var C = Java.use(typeName);
            if (C.class.isEnum()) return "enum";
            var mods = C.class.getModifiers();
            if ((mods & 512) !== 0) return "interface";
            if ((mods & 1024) !== 0) return "abstract";
        } catch (e) {}
        return "object";
    }

    // ═══════════════════════════════════════════════════════════════
    //  PRESETS — ready-to-use templates per kind
    // ═══════════════════════════════════════════════════════════════
    function presetForKind(kind, typeName) {
        switch (kind) {
            case "int": case "short": case "byte":
                return {kind:kind, value:0};
            case "long":   return {kind:"long", value:0};
            case "float":  return {kind:"float", value:0.0};
            case "double": return {kind:"double", value:0.0};
            case "boolean":return {kind:"boolean", value:false};
            case "char":   return {kind:"char", value:"a"};
            case "string": return {kind:"string", value:""};
            case "boxed":  return {kind:"null"};
            case "byte_array":  return {kind:"byte_array", hex:"00 00 00 00"};
            case "prim_array":  return {kind:"array",
                                        elementType:"int", elements:[]};
            case "object_array": {
                var el = typeName && typeName.length > 1
                       ? typeName.substring(1).replace(/\//g,".") : "java.lang.Object";
                return {kind:"array", elementType:el, elements:[]};
            }
            case "list":   return {kind:"list",
                                   className:typeName||"java.util.ArrayList",
                                   items:[]};
            case "set":    return {kind:"set",
                                   className:typeName||"java.util.HashSet",
                                   items:[]};
            case "map":    return {kind:"map",
                                   className:typeName||"java.util.HashMap",
                                   entries:[]};
            case "json":   return {kind:"json",
                                   className:typeName||"org.json.JSONObject",
                                   value:"{}"};
            case "color":  return {kind:"color", value:0xFF000000};
            case "date":   return {kind:"expr", expr:"new java.util.Date()"};
            case "file":   return {kind:"string", value:"/sdcard/"};
            case "intent": return {kind:"expr",
                                   expr:"new android.content.Intent()"};
            case "bundle": return {kind:"expr",
                                   expr:"new android.os.Bundle()"};
            case "context":return {kind:"expr",
                                   expr:"Java.use('android.app.ActivityThread')"
                                       +".currentApplication()"};
            case "interface": return {kind:"null"};
            case "abstract":  return {kind:"null"};
            case "enum": {
                try {
                    var C = Java.use(typeName);
                    var vals = C.values();
                    if (vals.length > 0)
                        return {kind:"enum", className:typeName,
                                enumName:String(vals[0].name())};
                } catch (e) {}
                return {kind:"null"};
            }
            default: return {kind:"null"};
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  describeTyped — STRUCTURED data (not a string)
    // ═══════════════════════════════════════════════════════════════
    function describeTyped(v, depth) {
        depth = depth || 0;
        if (depth > 5) return {kind:"truncated", display:"<...>"};
        if (v === null) return {kind:"null", display:"null"};
        if (v === undefined) return {kind:"null", display:"undefined"};
        var t = typeof v;
        if (t === "boolean") return {kind:"boolean", value:v, display:String(v)};
        if (t === "number") {
            return Number.isInteger(v)
                ? {kind:"long", value:v, display:String(v)}
                : {kind:"double", value:v, display:String(v)};
        }
        if (t === "string") return {kind:"string", value:v, display:JSON.stringify(v)};
        if (t === "function") return {kind:"fn", display:"<fn "+(v.name||"anon")+">"};
        if (t !== "object") return {kind:"raw", display:String(v)};

        try {
            var cn = v.$className ? String(v.$className) : "";
            if (!cn) return {kind:"raw", display:JSON.stringify(v)};

            // Primitive wrappers
            if (cn === "java.lang.String")
                return {kind:"string", value:String(v),
                        display:JSON.stringify(String(v))};
            if (cn === "java.lang.Integer" || cn === "java.lang.Short" ||
                cn === "java.lang.Byte" || cn === "java.lang.Long")
                return {kind:"long", value:Number(v), display:String(v)};
            if (cn === "java.lang.Float" || cn === "java.lang.Double")
                return {kind:"double", value:Number(v), display:String(v)};
            if (cn === "java.lang.Boolean")
                return {kind:"boolean", value:(String(v)==="true"),
                        display:String(v)};
            if (cn === "java.lang.Character")
                return {kind:"char", value:String(v),
                        display:JSON.stringify(String(v))};

            // Enum
            var isEnum = false;
            try { isEnum = Java.use(cn).class.isEnum(); } catch (e) {}
            if (isEnum) {
                var hn = 0;
                try { hn = alloc(v); } catch (e) {}
                return {kind:"enum", className:cn, handle:hn,
                        name:String(v.name()), ordinal:Number(v.ordinal()),
                        display:cn+"."+String(v.name())};
            }

            // Arrays
            if (cn.charAt(0) === LB) {
                var n = 0;
                try { n = Number(v.length) || 0; } catch (e) {}
                var items = [];
                var cap = Math.min(n, 32);
                for (var i = 0; i < cap; i++) {
                    var el;
                    try { el = v[i]; } catch (e) { el = null; }
                    items.push(describeTyped(el, depth + 1));
                }
                var hnA = 0;
                try { hnA = alloc(v); } catch (e) {}
                return {kind:"array", className:cn, handle:hnA,
                        elementType:cn.substring(1), length:n, items:items,
                        display:"("+cn+") length="+n};
            }

            // Map
            if (cn.indexOf("Map") >= 0) {
                var entries = [];
                var n2 = 0;
                try {
                    var it = v.entrySet().iterator();
                    while (it.hasNext() && n2 < 24) {
                        var e = it.next();
                        entries.push({key:describeTyped(e.getKey(), depth+1),
                                      value:describeTyped(e.getValue(), depth+1)});
                        n2++;
                    }
                } catch (e) {}
                var hnM = 0;
                try { hnM = alloc(v); } catch (e) {}
                return {kind:"map", className:cn, handle:hnM, length:n2,
                        entries:entries, display:"{"+n2+" entries}"};
            }

            // List/Set/Collection
            if (cn.indexOf("List") >= 0 || cn.indexOf("Set") >= 0 ||
                cn.indexOf("Collection") >= 0) {
                var items2 = [];
                var n3 = 0;
                try {
                    var it2 = v.iterator();
                    while (it2.hasNext() && n3 < 32) {
                        items2.push(describeTyped(it2.next(), depth + 1));
                        n3++;
                    }
                } catch (e) {}
                var hnL = 0;
                try { hnL = alloc(v); } catch (e) {}
                return {kind:(cn.indexOf("Set")>=0 ? "set" : "list"),
                        className:cn, handle:hnL, length:n3, items:items2,
                        display:"["+n3+" items]"};
            }

            // Object: fields
            var fields = [];
            try {
                var C = Java.use(cn);
                var flds = C.class.getDeclaredFields();
                var cap2 = Math.min(flds.length, 24);
                for (var k = 0; k < cap2; k++) {
                    var f = flds[k];
                    try {
                        f.setAccessible(true);
                        fields.push({
                            name:String(f.getName()),
                            typeName:String(f.getType().getName()),
                            typed:describeTyped(f.get(v), depth + 1)
                        });
                    } catch (e) {}
                }
            } catch (e) {}
            var hnO = 0;
            try { hnO = alloc(v); } catch (e) {}
            return {kind:"object", className:cn, handle:hnO, fields:fields,
                    display:"<"+cn+" "+String(v)+">"};
        } catch (e) {
            return {kind:"error", display:"<err:"+e+">"};
        }
    }

    // ─── Legacy describe for logs (compact) ──────────────────────────
    function describe(v) {
        var t = describeTyped(v, 0);
        return formatTypedCompact(t);
    }
    function formatTypedCompact(t) {
        if (!t || !t.kind) return "<invalid>";
        switch (t.kind) {
            case "null": case "string": case "int": case "long":
            case "short": case "byte": case "float": case "double":
            case "boolean": case "char": case "enum":
                return t.display || "";
            case "array": case "list": case "set": {
                var parts = [];
                var items = t.items || [];
                for (var i = 0; i < items.length; i++)
                    parts.push(formatTypedCompact(items[i]));
                return "(" + (t.className||"") + ")[" + parts.join(",") + "]";
            }
            case "map": {
                var parts2 = [];
                var es = t.entries || [];
                for (var j = 0; j < es.length; j++)
                    parts2.push(formatTypedCompact(es[j].key) + ":" +
                                formatTypedCompact(es[j].value));
                return "{" + parts2.join(",") + "}";
            }
            case "object": {
                return t.display || "";
            }
            default: return t.display || "";
        }
    }

    // ─── Full multi-line tree format for trace output ────────────────
    function formatTypedTree(t, indent) {
        indent = indent || "";
        if (!t || !t.kind) return indent + "<invalid>";
        switch (t.kind) {
            case "null": return indent + "null";
            case "string": case "int": case "long": case "short":
            case "byte": case "float": case "double": case "boolean":
            case "char": case "enum":
                return indent + (t.display || "");
            case "truncated": return indent + "<...>";
            case "array": case "list": case "set": {
                var out = indent + "(" + (t.className || t.elementType || "") +
                          ") length=" + (t.length || 0);
                var items = t.items || [];
                for (var i = 0; i < items.length; i++)
                    out += "\n" + indent + "  [" + i + "] " +
                           formatTypedTree(items[i], "").trim();
                return out;
            }
            case "map": {
                var out2 = indent + "(" + t.className + ") entries=" +
                           (t.length || 0);
                var es = t.entries || [];
                for (var j = 0; j < es.length; j++) {
                    out2 += "\n" + indent + "  [" +
                            formatTypedTree(es[j].key, "").trim() + "] = " +
                            formatTypedTree(es[j].value, "").trim();
                }
                return out2;
            }
            case "object": {
                var out3 = indent + t.display;
                var flds = t.fields || [];
                for (var f = 0; f < flds.length; f++) {
                    out3 += "\n" + indent + "  ." + flds[f].name +
                            " (" + flds[f].typeName + ") = " +
                            formatTypedTree(flds[f].typed, "").trim();
                }
                return out3;
            }
            default: return indent + (t.display || "<unknown>");
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  matTyped — construct Java from typed spec (any kind)
    // ═══════════════════════════════════════════════════════════════
    function matTyped(spec, expectedType) {
        if (spec === null || spec === undefined) return null;
        if (typeof spec !== "object") return spec;
        if (typeof spec.handle === "number") return get(spec.handle);
        if (!("kind" in spec)) {
            if ("value" in spec) return spec.value;
            return spec;
        }
        switch (spec.kind) {
            case "null": return null;
            case "int": case "short": case "byte":
                return parseInt(spec.value, 10) || 0;
            case "long":
                if (typeof Int64 === "function")
                    return Int64(String(spec.value || 0));
                return Number(spec.value || 0);
            case "float": case "double":
                return parseFloat(spec.value) || 0;
            case "boolean": return !!spec.value;
            case "string": return String(spec.value || "");
            case "char": {
                var sv = String(spec.value || "\u0000");
                return sv.length > 0 ? sv.charAt(0) : String.fromCharCode(0);
            }
            case "enum":
                return Java.use(spec.className).valueOf(spec.enumName);
            case "handle": return get(spec.handle);
            case "byte_array": {
                var hex = String(spec.hex || "").replace(/\s/g, "");
                var len = hex.length / 2;
                var b = Java.array("byte", []);
                var arr = Java.use("[B").$new(len);
                for (var i = 0; i < len; i++) {
                    var v2 = parseInt(hex.substr(i*2, 2), 16) || 0;
                    if (v2 > 127) v2 -= 256;
                    arr[i] = v2;
                }
                return arr;
            }
            case "array": {
                var elT = spec.elementType || "java.lang.Object";
                var elems = (spec.elements || []).map(function(e) {
                    return matTyped(e, elT);
                });
                return Java.array(elT, elems);
            }
            case "arrayJson": {
                var elT2 = spec.elementType || "java.lang.Object";
                try {
                    var parsed = JSON.parse(spec.json || "[]");
                    if (Array.isArray(parsed))
                        return Java.array(elT2, parsed.map(function(e) {
                            return matTyped(e, elT2);
                        }));
                } catch (e) {}
                return null;
            }
            case "list": {
                var L = Java.use(spec.className || "java.util.ArrayList");
                var list = L.$new();
                var litems = spec.items || [];
                for (var li = 0; li < litems.length; li++)
                    list.add(matTyped(litems[li]));
                return list;
            }
            case "set": {
                var S = Java.use(spec.className || "java.util.HashSet");
                var st = S.$new();
                var sitems = spec.items || [];
                for (var si = 0; si < sitems.length; si++)
                    st.add(matTyped(sitems[si]));
                return st;
            }
            case "map": {
                var M = Java.use(spec.className || "java.util.HashMap");
                var mp = M.$new();
                var entries = spec.entries || [];
                for (var ei = 0; ei < entries.length; ei++) {
                    var en = entries[ei];
                    mp.put(matTyped(en.k), matTyped(en.v));
                }
                return mp;
            }
            case "json": {
                var JC = Java.use(spec.className || "org.json.JSONObject");
                return JC.$new(String(spec.value || "{}"));
            }
            case "color": {
                var cv = Number(spec.value) || 0;
                return Java.use("android.graphics.Color").argb(
                    (cv >> 24) & 0xFF, (cv >> 16) & 0xFF,
                    (cv >> 8) & 0xFF, cv & 0xFF);
            }
            case "construct": {
                var C = Java.use(spec.className);
                var cargs = (spec.args || []).map(function(a) {
                    return matTyped(a);
                });
                var inst;
                if (spec.ctorSig) {
                    inst = C.$alloc();
                    C.$init.overload.apply(C.$init, parseSig(spec.ctorSig))
                        .call(inst, ...cargs);
                } else {
                    inst = C.$new.apply(C, cargs);
                }
                if (spec.fields) {
                    for (var fi = 0; fi < spec.fields.length; fi++)
                        inst[spec.fields[fi].name] =
                            matTyped(spec.fields[fi].value);
                }
                return inst;
            }
            case "expr": {
                try {
                    var fn = new Function("Java", "return ("+spec.expr+");");
                    return fn(Java);
                } catch (e) { return null; }
            }
            default: return spec.value !== undefined ? spec.value : null;
        }
    }

    function parseSig(sig) {
        if (!sig) return [];
        var out = [], i = 0;
        while (i < sig.length) {
            var c = sig.charAt(i);
            if (c === "(") { i++; continue; }
            if (c === ")") break;
            if (c === "L") {
                var e = sig.indexOf(";", i);
                if (e === -1) break;
                out.push(sig.substring(i + 1, e).split("/").join("."));
                i = e + 1;
            } else if (c === LB) {
                var d = 0;
                while (sig.charAt(i) === LB) { d++; i++; }
                if (sig.charAt(i) === "L") {
                    var e2 = sig.indexOf(";", i);
                    if (e2 === -1) break;
                    var pre = "";
                    for (var p = 0; p < d; p++) pre += LB;
                    out.push(pre + sig.substring(i+1, e2).split("/").join("."));
                    i = e2 + 1;
                } else {
                    var pre2 = "";
                    for (var p2 = 0; p2 < d; p2++) pre2 += LB;
                    out.push(pre2 + sig.charAt(i));
                    i++;
                }
            } else {
                var pn = "";
                if (c === "Z") pn = "boolean";
                else if (c === "B") pn = "byte";
                else if (c === "C") pn = "char";
                else if (c === "S") pn = "short";
                else if (c === "I") pn = "int";
                else if (c === "J") pn = "long";
                else if (c === "F") pn = "float";
                else if (c === "D") pn = "double";
                else if (c === "V") pn = "void";
                else pn = c;
                out.push(pn);
                i++;
            }
        }
        return out;
    }

    // ─── Chunked sender ───────────────────────────────────────────────
    var _nextChunkSession = 1;
    var CHUNK_THRESHOLD = 128 * 1024;
    var CHUNK_SIZE      = 128 * 1024;
    function sendMaybeChunked(obj) {
        var json;
        try { json = JSON.stringify(obj); } catch (e) { json = ""; }
        if (json.length <= CHUNK_THRESHOLD) {
            try { send(obj); } catch (e) {}
            return;
        }
        var kind = obj && obj.type ? String(obj.type) : "chunked";
        var sid = _nextChunkSession++;
        var total = Math.max(1, Math.ceil(json.length / CHUNK_SIZE));
        try { send({type:"chunk_begin", sessionId:sid, kind:kind,
                    totalChunks:total, totalBytes:json.length}); } catch (e) {}
        for (var i = 0; i < total; i++) {
            try { send({type:"chunk_data", sessionId:sid, index:i,
                        totalChunks:total,
                        data:json.substring(i*CHUNK_SIZE,
                                            (i+1)*CHUNK_SIZE)}); } catch (e) {}
        }
        try { send({type:"chunk_end", sessionId:sid, kind:kind}); } catch (e) {}
    }

    // ─── Smart overload picker ────────────────────────────────────────
    function argMatchesType(arg, jtype) {
        if (arg === null || arg === undefined) return true;
        var t = typeof arg;
        var tn = jtype.className;
        if (t === "number") {
            return (tn === "int" || tn === "long" || tn === "short" ||
                    tn === "byte" || tn === "float" || tn === "double" ||
                    tn === "java.lang.Integer" || tn === "java.lang.Long" ||
                    tn === "java.lang.Number" || tn === "java.lang.Object");
        }
        if (t === "string") {
            return (tn === "java.lang.String" ||
                    tn === "java.lang.CharSequence" ||
                    tn === "java.lang.Object");
        }
        if (t === "boolean") {
            return (tn === "boolean" || tn === "java.lang.Boolean" ||
                    tn === "java.lang.Object");
        }
        if (t === "object" && arg.$className) {
            var acn = String(arg.$className);
            if (acn === tn || tn === "java.lang.Object") return true;
            try {
                var aC = Java.use(acn);
                var tC = Java.use(tn);
                return aC.class.isAssignableFrom(tC.class) ||
                       tC.class.isAssignableFrom(aC.class);
            } catch (e) { return false; }
        }
        return false;
    }

    function pickOverload(overloads, args) {
        if (!overloads || overloads.length === 0) return null;
        var candidates = [];
        for (var i = 0; i < overloads.length; i++) {
            if (overloads[i].argumentTypes.length === args.length)
                candidates.push(overloads[i]);
        }
        if (candidates.length === 0) return null;
        if (candidates.length === 1) return candidates[0];
        for (var k = 0; k < candidates.length; k++) {
            var ov = candidates[k];
            var ok = true;
            for (var j = 0; j < args.length; j++) {
                if (!argMatchesType(args[j], ov.argumentTypes[j])) {
                    ok = false; break;
                }
            }
            if (ok) return ov;
        }
        return candidates[0];
    }

    // ═══════════════════════════════════════════════════════════════
    //  cpp_* handlers
    // ═══════════════════════════════════════════════════════════════
    var cppHandlers = {
        cpp_use_class: function (cmd) {
            replyHandle(cmd.id, alloc(Java.use(cmd.className)));
        },
        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_release_handle: function (cmd) { drop(cmd.handle); replyVoid(cmd.id); },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },
        cpp_eval: function (cmd) {
            try {
                var fn = new Function("return ("+(cmd.code||"null")+");");
                reply(cmd.id, true, "value", describe(fn()));
            } catch (e) { replyError(cmd.id, e); }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  jv_* handlers — the Java layer
    // ═══════════════════════════════════════════════════════════════
    var jvHandlers = {

        jv_enumerate: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                // IMPORTANT: always send ALL classes; C++ does live filtering
                // (case-insensitive, substring) on every keystroke.
                sendMaybeChunked({type:"classes_list", count:all.length,
                                  filter:"", classes:all});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[], error:""+e});
            }
        },

        jv_detail: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var jcls = C.class;
                var methods = [];
                var ms = jcls.getDeclaredMethods();
                for (var i = 0; i < ms.length; i++) {
                    try {
                        var m = ms[i];
                        m.setAccessible(true);
                        var pts = m.getParameterTypes();
                        var genTypes = null;
                        try { genTypes = m.getGenericParameterTypes(); }
                        catch (e) {}
                        var args = [];
                        for (var j = 0; j < pts.length; j++) {
                            var tn = String(pts[j].getName());
                            var kd = classifyType(tn);
                            var ev = [];
                            if (kd === "enum") {
                                try {
                                    var pc = Java.use(tn);
                                    var vals = pc.values();
                                    for (var e2 = 0; e2 < vals.length; e2++)
                                        ev.push(String(vals[e2].name()));
                                } catch (ex) {}
                            }
                            var gen = null;
                            if (genTypes && genTypes[j]) {
                                try { gen = String(genTypes[j].toString()); }
                                catch (e) {}
                            }
                            var defSpec = null;
                            try { defSpec = presetForKind(kd, tn); }
                            catch (e) { defSpec = {kind:"null"}; }
                            args.push({
                                typeName: tn, kind: kd, enumValues: ev,
                                generic: gen, defaultSpec: defSpec
                            });
                        }
                        var mods = m.getModifiers();
                        methods.push({
                            name: String(m.getName()),
                            ret: String(m.getReturnType().getName()),
                            retKind: classifyType(
                                String(m.getReturnType().getName())),
                            isStatic: (mods & 8) !== 0,
                            isAbstract: (mods & 1024) !== 0,
                            args: args
                        });
                    } catch (e) {}
                }

                var fields = [];
                var fs = jcls.getDeclaredFields();
                for (var k = 0; k < fs.length; k++) {
                    try {
                        var f = fs[k];
                        f.setAccessible(true);
                        var isStatic = (f.getModifiers() & 8) !== 0;
                        var fval = "";
                        var ftype = String(f.getType().getName());
                        if (isStatic) {
                            try { fval = describe(f.get(null)); }
                            catch (e) { fval = "<read err>"; }
                        }
                        fields.push({
                            name: String(f.getName()),
                            type: ftype,
                            typeKind: classifyType(ftype),
                            isStatic: isStatic,
                            value: fval
                        });
                    } catch (e) {}
                }

                var ctors = [];
                var cs = jcls.getDeclaredConstructors();
                for (var q = 0; q < cs.length; q++) {
                    try {
                        cs[q].setAccessible(true);
                        var cpts = cs[q].getParameterTypes();
                        var cargs = [];
                        for (var w = 0; w < cpts.length; w++) {
                            var ctn = String(cpts[w].getName());
                            var ck = classifyType(ctn);
                            var cdef = presetForKind(ck, ctn);
                            cargs.push({typeName: ctn, kind: ck,
                                        enumValues: [], defaultSpec: cdef});
                        }
                        ctors.push({args: cargs});
                    } catch (e) {}
                }

                sendMaybeChunked({
                    type:"class_detail_result",
                    className: cmd.className,
                    methods: methods, fields: fields, constructors: ctors
                });
            } catch (e) {
                send({type:"class_detail_result", className:cmd.className,
                      methods:[], fields:[], constructors:[], error:""+e});
            }
        },

        jv_trace: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var n = 0;
                for (var i = 0; i < MM.overloads.length; i++) {
                    (function(ov){
                        if (ov.__ygg_trace !== undefined) return;
                        ov.__ygg_trace = ov.implementation || null;
                        ov.implementation = function() {
                            var a = Array.prototype.slice.call(arguments);
                            // Typed args
                            var typedArgs = [];
                            for (var k = 0; k < a.length; k++) {
                                try { typedArgs.push(describeTyped(a[k], 0)); }
                                catch (e) { typedArgs.push({kind:"error",
                                                            display:"<err>"}); }
                            }
                            // Backtrace
                            var bt = "";
                            try {
                                var b = Java.backtrace({limit:8});
                                if (b && b.frames) {
                                    var fr = [];
                                    for (var f = 0; f < b.frames.length; f++) {
                                        var fr0 = b.frames[f];
                                        fr.push(String(fr0.className||"") + "." +
                                                String(fr0.methodName||""));
                                    }
                                    bt = fr.join(" < ");
                                }
                            } catch (e) {}
                            // Build multi-line trace output
                            var out = "[trace] " + cmd.className + "." +
                                      cmd.methodName;
                            for (var k2 = 0; k2 < typedArgs.length; k2++) {
                                out += "\n  arg" + k2 + " = " +
                                    formatTypedTree(typedArgs[k2], "").trim();
                            }
                            if (bt) out += "\n  bt: " + bt;
                            try {
                                send({type:"trace_line", level:"log", line:out});
                            } catch (e) {}
                            var r;
                            try {
                                r = ov.__ygg_trace
                                    ? ov.__ygg_trace.apply(this, a) : undefined;
                            } catch (ex) {
                                try {
                                    send({type:"trace_line", level:"error",
                                          line:"[trace] " + cmd.className +
                                               "." + cmd.methodName +
                                               " THREW: " + ex});
                                } catch (e2) {}
                                throw ex;
                            }
                            if (r !== undefined) {
                                try {
                                    var rt = describeTyped(r, 0);
                                    send({type:"trace_line", level:"log",
                                          line:"[trace] " + cmd.className +
                                               "." + cmd.methodName +
                                               " -> " +
                                               formatTypedTree(rt, "").trim()});
                                } catch (e) {}
                            }
                            return r;
                        };
                        n++;
                    })(MM.overloads[i]);
                }
                send({type:"trace_ok", className:cmd.className,
                      methodName:cmd.methodName, count:n});
            } catch (e) {
                send({type:"trace_err", error:""+e});
            }
        },

        jv_untrace: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (MM && MM.overloads) {
                    for (var i = 0; i < MM.overloads.length; i++) {
                        var ov = MM.overloads[i];
                        if (ov.__ygg_trace !== undefined) {
                            ov.implementation = ov.__ygg_trace;
                            delete ov.__ygg_trace;
                        }
                    }
                }
                send({type:"untrace_ok"});
            } catch (e) { send({type:"untrace_err", error:""+e}); }
        },

        jv_instances: function (cmd) {
            try {
                var items = [];
                var C = Java.use(cmd.className);
                Java.choose(cmd.className, {
                    onMatch: function(x) {
                        if (items.length >= 20) return "stop";
                        try {
                            var h = alloc(x);
                            var flds = C.class.getDeclaredFields();
                            var fields = [];
                            for (var i = 0; i < flds.length; i++) {
                                var f = flds[i];
                                if ((f.getModifiers() & 8) !== 0) continue;
                                try {
                                    f.setAccessible(true);
                                    var t = describeTyped(f.get(x), 0);
                                    fields.push({
                                        name: String(f.getName()),
                                        value: formatTypedTree(t, "").trim(),
                                        typed: t
                                    });
                                } catch (e) {}
                            }
                            items.push({handle:h,
                                        className:String(x.$className),
                                        fields:fields});
                        } catch (e) {}
                    },
                    onComplete: function() {}
                });
                sendMaybeChunked({type:"instances_result",
                                  className:cmd.className, items:items});
            } catch (e) {
                send({type:"instances_result", className:cmd.className,
                      items:[], error:""+e});
            }
        },

        jv_new: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var args = (cmd.argsSpec || []).map(function(a) {
                    return matTyped(a);
                });
                var inst = C.$new.apply(C, args);
                var h = alloc(inst);
                send({type:"call_result", ok:true,
                      result:"new " + cmd.className + " handle=" + h});
            } catch (e) {
                send({type:"call_result", ok:false, error:""+e});
            }
        },

        jv_call: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var args = (cmd.argsSpec || []).map(function(a) {
                    return matTyped(a);
                });
                var isStatic = cmd.isStatic === true;
                var inst = null;
                if (!isStatic) {
                    if (cmd.instanceHandle > 0) inst = get(cmd.instanceHandle);
                    else Java.choose(cmd.className, {
                        onMatch: function(x){ inst = x; return "stop"; },
                        onComplete: function() {}
                    });
                    if (!inst) throw new Error("no instance");
                }
                var chosen = pickOverload(MM.overloads, args);
                if (!chosen) throw new Error("no matching overload");
                var r = isStatic ? chosen.apply(null, args)
                                 : chosen.apply(inst, args);
                send({type:"call_result", ok:true,
                      result:formatTypedTree(describeTyped(r, 0), "").trim()});
            } catch (e) {
                send({type:"call_result", ok:false, error:""+e});
            }
        },

        jv_inspect: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var t = describeTyped(obj, 0);
                sendMaybeChunked({type:"inspect_result",
                                  className:String(obj.$className),
                                  typed:t});
            } catch (e) {
                send({type:"inspect_result", error:""+e});
            }
        },

        jv_read_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var t = describeTyped(obj[cmd.fieldName], 0);
                send({type:"field_result", ok:true,
                      value:formatTypedTree(t, "").trim(), typed:t});
            } catch (e) {
                send({type:"field_result", ok:false, error:""+e});
            }
        },

        jv_write_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                obj[cmd.fieldName] = matTyped(cmd.value);
                send({type:"field_write_ok"});
            } catch (e) {
                send({type:"field_write_err", error:""+e});
            }
        },

        jv_watch_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var fn = cmd.fieldName;
                var ms = cmd.intervalMs || 500;
                if (!obj.__ygg_watchers) obj.__ygg_watchers = {};
                if (obj.__ygg_watchers[fn]) return;
                var last = formatTypedTree(describeTyped(obj[fn], 0), "").trim();
                obj.__ygg_watchers[fn] = setInterval(function() {
                    try {
                        var cur = formatTypedTree(describeTyped(obj[fn], 0), "").trim();
                        if (cur !== last) {
                            send({type:"watch_change", fieldName:fn,
                                  oldValue:last, newValue:cur});
                            last = cur;
                        }
                    } catch (e) {}
                }, ms);
                send({type:"watch_started", fieldName:fn});
            } catch (e) { send({type:"watch_err", error:""+e}); }
        },

        jv_unwatch_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj || !obj.__ygg_watchers) return;
                var f = obj.__ygg_watchers[cmd.fieldName];
                if (f) { clearInterval(f); delete obj.__ygg_watchers[cmd.fieldName]; }
                send({type:"watch_stopped", fieldName:cmd.fieldName});
            } catch (e) {}
        },

        jv_eval: function (cmd) {
            try {
                var fn = new Function("Java", "return ("+cmd.code+");");
                var r = fn(Java);
                send({type:"eval_result", ok:true,
                      result:formatTypedTree(describeTyped(r, 0), "").trim()});
            } catch (e) {
                send({type:"eval_result", ok:false, error:""+e});
            }
        }
    };

    // ─── native handlers ─────────────────────────────────────────────
    var nativeHandlers = {
        eval: function (cmd) {
            try {
                var fn = new Function("return ("+(cmd.code||"null")+");");
                send({type:"eval_result", ok:true, result:describe(fn())});
            } catch (e) {
                send({type:"eval_result", ok:false, error:""+e});
            }
        },
        run_user_script: function (cmd) {
            var nm = cmd.name || "anon";
            var code = cmd.code || "";
            // Defer execution: the poller thread must not block on the
            // user's script (which may do slow Java calls and stall the
            // ART watchdog). We schedule on the next tick and use
            // Java.perform (async) instead of performNow.
            setTimeout(function() {
                try {
                    Java.perform(function() {
                        try {
                            (new Function(code))();
                            send({type:"user_script_loaded", name:nm,
                                  size:code.length, ok:true});
                        } catch (e) {
                            send({type:"user_script_loaded", name:nm,
                                  ok:false, error:""+e});
                            send({type:"script_failed", name:nm, error:""+e});
                        }
                    });
                } catch (e) {
                    send({type:"user_script_loaded", name:nm, ok:false,
                          error:"outer: "+e});
                }
            }, 0);
        },
        enumerate_classes: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                sendMaybeChunked({type:"classes_list", count:all.length,
                                  classes:all});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[], error:""+e});
            }
        }
    };

    // ─── Dispatcher ──────────────────────────────────────────────────
    function dispatchCmd(cmd) {
        if (!cmd || !cmd.action) return;
        var h = cppHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { replyError(cmd.id, e); } return; }
        h = jvHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { send({type:"jv_error", error:""+e}); } return; }
        h = nativeHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { send({type:"native_error", error:""+e}); } return; }
        if (cmd.action === "yamgg_tick" || cmd.action === "yamgg_boot") return;
        try { replyError(cmd.id, new Error("unknown: " + cmd.action)); } catch (e) {}
    }
    function unwrapCmd(msg) {
        if (!msg) return null;
        if (typeof msg === "string") {
            try { return JSON.parse(msg); } catch (e) { return null; }
        }
        if (msg.payload !== undefined && msg.payload !== null) return msg.payload;
        if (msg.action) return msg;
        return null;
    }
    try {
        recv("yamgg_cmd", function (msg) { dispatchCmd(unwrapCmd(msg)); });
    } catch (e) {}

    // ─── ModView loader ──────────────────────────────────────────────
    // CRITICAL: classFactory.loader is a GLOBAL that affects every Java.use.
    // We must NOT leave it set to our InMemoryDexClassLoader globally —
    // that would prevent Java.use from finding classes in the system
    // PathClassLoader (e.g. UnityPlayer, ActivityThread, WindowManager).
    // We only swap it temporarily, then restore.
    var g_targetLoader = null;
    var g_modViewClass = null;

    function resolveLoader() {
        if (g_targetLoader !== null) return g_targetLoader;
        try {
            var loaders = Java.enumerateClassLoadersSync();
            for (var i = 0; i < loaders.length; i++) {
                try {
                    var cn = String(loaders[i].getClass().getName());
                    if (cn.indexOf("InMemoryDexClassLoader") < 0 &&
                        cn.indexOf("DexClassLoader") < 0 &&
                        cn.indexOf("PathClassLoader") < 0) continue;
                    // Try loading ModView via this loader's loadClass
                    var cls = loaders[i].loadClass("com.yamgg.modview.ModView");
                    if (cls) {
                        g_targetLoader = loaders[i];
                        send({type:"loader_resolved", index:i, total:loaders.length});
                        return g_targetLoader;
                    }
                } catch (e) {}
            }
        } catch (e) { send({type:"loader_scan_err", message:""+e}); }
        return null;
    }

    function getModViewClass() {
        if (g_modViewClass !== null) return g_modViewClass;
        var ldr = resolveLoader();
        if (!ldr) return null;

        // Temporarily swap classFactory.loader to resolve ModView,
        // then restore so other Java.use calls work normally.
        var saved = null;
        try { saved = Java.classFactory.loader; } catch (e) {}
        try {
            Java.classFactory.loader = ldr;
            g_modViewClass = Java.use("com.yamgg.modview.ModView");
            send({type:"modview_class_ok"});
        } catch (e) {
            send({type:"modview_class_err", message:""+e});
            return null;
        } finally {
            try { Java.classFactory.loader = saved; } catch (e) {}
        }
        return g_modViewClass;
    }

    // ─── Attach (performNow-safe) ─────────────────────────────────────
    var attachDone = false;
    var attachAttempts = 0;

    function tryOne(tag, act) {
        if (!act || attachDone) return false;
        var Mv = getModViewClass();
        if (!Mv) { send({type:"attach_skip", tag:tag, reason:"no_class"});
                   return false; }
        try { if (act.isFinishing()) return false; } catch (e) {}
        try {
            Mv.attach(act);
            attachDone = true;
            send({type:"attach_ok", strategy:tag,
                  className:""+act.getClass().getName(),
                  attempts:attachAttempts});
            return true;
        } catch (e) {
            send({type:"attach_try_err", strategy:tag, message:""+e});
            return false;
        }
    }

    // ── Strategy 1: UnityPlayer.currentActivity / mActivity ─────────
    function tryUnityPlayer() {
        var tried = false;
        try {
            var UP = Java.use("com.unity3d.player.UnityPlayer");
            tried = true;
            // currentActivity (public static field)
            try {
                var ca = UP.currentActivity.value;
                if (ca && tryOne("UnityPlayer.currentActivity", ca)) return true;
            } catch (e) {}
            // mActivity (older versions)
            try {
                var ma = UP.mActivity.value;
                if (ma && tryOne("UnityPlayer.mActivity", ma)) return true;
            } catch (e) {}
        } catch (e) {
            send({type:"attach_skip", tag:"UnityPlayer", reason:""+e});
        }
        return false;
    }

    // ── Strategy 2: UnityPlayerActivity via Java.choose ──────────────
    function tryUnityPlayerActivity() {
        try {
            var found = null;
            Java.choose("com.unity3d.player.UnityPlayerActivity", {
                onMatch: function(x) { found = x; return "stop"; },
                onComplete: function() {}
            });
            if (found && tryOne("UnityPlayerActivity.choose", found)) return true;
        } catch (e) {}
        return false;
    }

    // ── Strategy 3: ActivityThread.mActivities ──────────────────────
    function tryMActivities() {
        try {
            var AT = Java.use("android.app.ActivityThread");
            var at = AT.currentActivityThread();
            if (!at) return false;
            var map = null;
            try { map = at.mActivities.value; } catch (e) {}
            if (!map) {
                try {
                    var fld = at.getClass().getDeclaredField("mActivities");
                    fld.setAccessible(true);
                    map = fld.get(at);
                } catch (e) {}
            }
            if (!map) return false;
            var n = map.size();
            for (var i = 0; i < n; i++) {
                try {
                    var rec = map.valueAt(i);
                    if (!rec) continue;
                    var wr = rec.activity.value;
                    var act = wr;
                    try {
                        var cn = "" + wr.getClass().getName();
                        if (cn.indexOf("WeakReference") >= 0) act = wr.get();
                    } catch (e) {}
                    if (!act) continue;
                    if (tryOne("mActivities", act)) return true;
                } catch (e) {}
            }
        } catch (e) {
            send({type:"attach_skip", tag:"mActivities", reason:""+e});
        }
        return false;
    }

    // ── Strategy 4: WindowManagerGlobal.mRoots ──────────────────────
    function tryWindowManager() {
        try {
            var WMG = Java.use("android.view.WindowManagerGlobal");
            var inst = WMG.getInstance();
            var roots = inst.mRoots.value;
            if (!roots) return false;
            var n = roots.size();
            var ActCls = Java.use("android.app.Activity");
            for (var i = 0; i < n; i++) {
                try {
                    var ri = roots.get(i);
                    var view = ri.mView.value;
                    if (!view) continue;
                    var ctx = view.getContext();
                    try {
                        var act = Java.cast(ctx, ActCls);
                        if (act && tryOne("WindowManager"+i, act)) return true;
                    } catch (e) {}
                } catch (e) {}
            }
        } catch (e) {
            send({type:"attach_skip", tag:"WindowManager", reason:""+e});
        }
        return false;
    }

    // ── Strategy 5: Java.choose on android.app.Activity ────────────
    function tryJavaChoose() {
        var found = false;
        try {
            Java.choose("android.app.Activity", {
                onMatch: function(a) {
                    if (found || attachDone) return "stop";
                    if (tryOne("Java.choose", a)) { found = true; return "stop"; }
                },
                onComplete: function() {}
            });
        } catch (e) {}
        return found;
    }

    // ── Inner: run strategies, MUST be called inside a Java context ──
    function tryAttachOnce_inner() {
        if (attachDone) return true;
        if (tryUnityPlayer())          return true;
        if (tryUnityPlayerActivity())  return true;
        if (tryMActivities())          return true;
        if (tryWindowManager())        return true;
        if (attachAttempts % 5 === 0)  { if (tryJavaChoose()) return true; }
        return false;
    }

    // ── Outer: wraps with performNow for callers OUTSIDE Java ctx ──
    function tryAttachOnce() {
        attachAttempts++;
        try {
            Java.performNow(function () { tryAttachOnce_inner(); });
        } catch (e) {
            send({type:"attach_outer_err", message:""+e});
        }
    }

    // ── onResume hook: pure Java context, call inner directly ──────
    var hooksInstalled = false;
    function installAllHooks() {
        if (hooksInstalled) return;
        hooksInstalled = true;
        try {
            Java.performNow(function () {
                try {
                    var Activity = Java.use("android.app.Activity");
                    var origOnResume = Activity.onResume;
                    Activity.onResume.implementation = function () {
                        try { origOnResume.call(this); } catch (e) {}
                        if (!attachDone) {
                            attachAttempts++;
                            try { tryAttachOnce_inner(); }
                            catch (e) {
                                send({type:"onresume_attach_err",
                                      message:""+e});
                            }
                        }
                    };
                    send({type:"hook_installed"});
                } catch (e) {
                    send({type:"hook_error", message:"install: " + e});
                }
            });
        } catch (e) { send({type:"hook_outer_error", message:""+e}); }
    }

    // ─── Poller — event loop ─────────────────────────────────────────
    var pollCount = 0;
    var POLL_INTERVAL_MS = 50;
    var ALIVE_EVERY_MS = 30000;
    var lastAliveAt = 0;
    var booted = false;
    function pollTick() {
        try {
            Java.performNow(function () {
                var MV = getModViewClass();
                if (!MV) return;
                if (!booted) {
                    booted = true;
                    try { send({type:"boot_poller_v7"}); } catch (e) {}
                    installAllHooks();
                    attachAttempts++;
                    try { tryAttachOnce_inner(); } catch (e) {}
                    try { send({type:"boot_poller_done_v7"}); } catch (e) {}
                }
                if (!attachDone) { attachAttempts++; try { tryAttachOnce_inner(); } catch (e) { send({type:"poll_attach_err", message:""+e}); } }
                var drained = 0;
                for (var i = 0; i < 128; i++) {
                    var raw;
                    try { raw = MV.nativeGetPendingCmd(); }
                    catch (e) { break; }
                    if (!raw) break;
                    var obj = null;
                    try { obj = JSON.parse(raw); } catch (e) { continue; }
                    var cmd = (obj.payload && obj.payload.action)
                        ? obj.payload : obj;
                    if (!cmd || !cmd.action) continue;
                    dispatchCmd(cmd);
                    drained++;
                }
                pollCount++;
                var now = Date.now();
                if ((now - lastAliveAt) >= ALIVE_EVERY_MS) {
                    lastAliveAt = now;
                    try { send({type:"poller_alive", count:pollCount,
                                drained:drained}); } catch (e) {}
                }
            });
        } catch (e) {
            try { send({type:"tick_err", message:""+e}); } catch (e2) {}
        }
    }
    try { send({type:"poller_started", interval:POLL_INTERVAL_MS}); } catch (e) {}
    setInterval(pollTick, POLL_INTERVAL_MS);
    try { send({type:"cpp_ready"}); }       catch (e) {}
    try { send({type:"cpp_ready_final"}); } catch (e) {}
    try { send({type:"boot_signals_done_v7"}); } catch (e) {}
})();
