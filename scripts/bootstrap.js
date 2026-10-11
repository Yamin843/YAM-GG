// ===========================================================================
// YAM-GG bootstrap.js — advanced Java layer
// ===========================================================================
(function () {
    "use strict";

    try { send({type:"boot_top_v6"}); } catch (e) {}

    // ─── Handle registry ──────────────────────────────────────────────
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
        if (kind)   m.kind = kind;
        if (value  !== undefined && value  !== null) m.result = value;
        if (handle !== undefined && handle !== null) m.handle = handle;
        if (error)  m.error = String(error);
        try { send({ type: "reply", payload: JSON.stringify(m) }); } catch (e) {}
    }
    function replyHandle(id, h) { reply(id, true,  "handle", null, h); }
    function replyValue(id, v)  { reply(id, true,  "value",  v,    null); }
    function replyVoid(id)      { reply(id, true,  "void",   null, null); }
    function replyError(id, e)  {
        reply(id, false, "error", null, null,
              (e && e.message) ? e.message : String(e));
    }

    // ─── JNI sig parser ───────────────────────────────────────────────
    function prim(c) {
        switch (c) {
        case "Z": return "boolean"; case "B": return "byte";
        case "C": return "char";    case "S": return "short";
        case "I": return "int";     case "J": return "long";
        case "F": return "float";   case "D": return "double";
        case "V": return "void";    default:  return c;
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
                    out.push(pre + sig.substring(i + 1, e2).split("/").join("."));
                    i = e2 + 1;
                } else {
                    var pre2 = "";
                    for (var p2 = 0; p2 < d; p2++) pre2 += LB;
                    out.push(pre2 + sig.charAt(i));
                    i++;
                }
            } else { out.push(prim(c)); i++; }
        }
        return out;
    }

    // ─── Type classification from class name ──────────────────────────
    function classifyType(typeName) {
        if (!typeName) return "object";
        if (typeName === "int") return "int";
        if (typeName === "long") return "long";
        if (typeName === "short") return "short";
        if (typeName === "byte") return "byte";
        if (typeName === "float") return "float";
        if (typeName === "double") return "double";
        if (typeName === "boolean") return "boolean";
        if (typeName === "char") return "char";
        if (typeName === "void") return "void";
        if (typeName === "java.lang.String") return "string";
        if (typeName.charAt(0) === LB) return "array";
        if (typeName.indexOf("Map") >= 0) return "map";
        if (typeName.indexOf("List") >= 0 ||
            typeName.indexOf("Set") >= 0 ||
            typeName.indexOf("Collection") >= 0) return "list";
        return "object";
    }

    // ─── describe (cycle-safe, depth-limited) ────────────────────────
    function describe(v, maxDepth) {
        return describeRec(v, new WeakSet(), 0, maxDepth === undefined ? 6 : maxDepth);
    }
    function describeRec(v, seen, depth, maxDepth) {
        if (v === null) return "null";
        if (v === undefined) return "undefined";
        var t = typeof v;
        if (t === "number" || t === "boolean") return String(v);
        if (t === "string") return JSON.stringify(v);
        if (t === "function") return "<fn " + (v.name || "anon") + ">";
        if (t !== "object") return String(v);
        if (typeof seen.add === "function") {
            if (seen.has(v)) return "<circular>";
            try { seen.add(v); } catch (e) {}
        }
        if (depth > maxDepth) return "<...>";
        try {
            var cn = v.$className ? String(v.$className) : "";
            if (!cn) return JSON.stringify(v);
            if (cn === "java.lang.String") return JSON.stringify(String(v));
            if (cn === "java.lang.Integer" || cn === "java.lang.Long" ||
                cn === "java.lang.Short" || cn === "java.lang.Byte" ||
                cn === "java.lang.Float" || cn === "java.lang.Double" ||
                cn === "java.lang.Boolean" || cn === "java.lang.Character")
                return String(v);
            if (cn.charAt(0) === LB) {
                var out = [], n = 0;
                try { n = Number(v.length) || 0; } catch (e) { n = 0; }
                var cap = Math.min(n, 20);
                for (var i = 0; i < cap; i++) {
                    var el;
                    try { el = v[i]; } catch (e) { el = null; }
                    out.push(describeRec(el, seen, depth + 1, maxDepth));
                }
                if (n > cap) out.push("..." + (n - cap) + " more");
                return "(" + cn + ")" + LB + out.join(",") + RB;
            }
            if (cn.indexOf("Map") >= 0) {
                var mp = [];
                try {
                    var it = v.entrySet().iterator();
                    var count = 0;
                    while (it.hasNext() && count < 12) {
                        var e = it.next();
                        mp.push(describeRec(e.getKey(), seen, depth + 1, maxDepth) + ":" +
                                describeRec(e.getValue(), seen, depth + 1, maxDepth));
                        count++;
                    }
                    if (it.hasNext()) mp.push("...");
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + "){" + mp.join(",") + "}";
            }
            if (cn.indexOf("List") >= 0 || cn.indexOf("Set") >= 0 ||
                cn.indexOf("Collection") >= 0) {
                var li = [];
                try {
                    var it2 = v.iterator();
                    var c2 = 0;
                    while (it2.hasNext() && c2 < 20) {
                        li.push(describeRec(it2.next(), seen, depth + 1, maxDepth));
                        c2++;
                    }
                    if (it2.hasNext()) li.push("...");
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + ")" + LB + li.join(",") + RB;
            }
            return "<" + cn + " " + String(v) + ">";
        } catch (e) { return "<err:" + e + ">"; }
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
                        data:json.substring(i*CHUNK_SIZE, (i+1)*CHUNK_SIZE)}); } catch (e) {}
        }
        try { send({type:"chunk_end", sessionId:sid, kind:kind}); } catch (e) {}
    }

    // ─── Materializer: JSON → Java (with type detection) ──────────────
    function mat(v, expectedType) {
        if (v === null || v === undefined) return v;
        if (typeof v !== "object") {
            // Try to interpret raw value based on expected Java type
            if (expectedType) {
                if (expectedType === "int" || expectedType === "short" ||
                    expectedType === "byte" || expectedType === "long")
                    return parseInt(v, 10) || 0;
                if (expectedType === "float" || expectedType === "double")
                    return parseFloat(v) || 0;
                if (expectedType === "boolean") return !!v;
                if (expectedType === "char") return String(v).charAt(0);
                if (expectedType === "java.lang.String") return String(v);
            }
            return v;
        }
        if (typeof v.handle === "number") return get(v.handle);
        if (!("kind" in v)) {
            if ("value" in v) return v.value;
            return v;
        }
        switch (v.kind) {
        case "int": case "short": case "byte": return v.value | 0;
        case "long":
            if (typeof Int64 === "function") return Int64(String(v.value));
            return Number(v.value);
        case "float": case "double": return Number(v.value);
        case "boolean": return !!v.value;
        case "string":  return String(v.value);
        case "char": {
            var sv = String(v.value);
            return sv.length > 0 ? sv.charAt(0) : String.fromCharCode(0);
        }
        case "null":      return null;
        case "undefined": return undefined;
        case "enum":      return Java.use(v.className).valueOf(v.enumName);
        case "array": {
            var elType = v.elementType || "java.lang.Object";
            return Java.array(elType, (v.elements || []).map(function(e) {
                return mat(e, elType);
            }));
        }
        case "arrayJson": {
            var elT = v.elementType || "java.lang.Object";
            try {
                var parsed = JSON.parse(v.json);
                if (Array.isArray(parsed)) {
                    return Java.array(elT, parsed.map(function(e) {
                        return mat(e, elT);
                    }));
                }
            } catch (e) {}
            return null;
        }
        case "construct": {
            var cc = Java.use(v.className);
            var cargs = (v.args || []).map(function(a) { return mat(a); });
            var inst;
            if (v.ctorSig) {
                inst = cc.$alloc();
                cc.$init.overload.apply(cc.$init, parseSig(v.ctorSig))
                    .call(inst, ...cargs);
            } else {
                inst = cc.$new.apply(cc, cargs);
            }
            if (v.fields) {
                for (var fi = 0; fi < v.fields.length; fi++) {
                    inst[v.fields[fi].name] = mat(v.fields[fi].value);
                }
            }
            return inst;
        }
        case "expr": {
            try {
                var fn = new Function("Java", "return (" + v.expr + ");");
                return fn(Java);
            } catch (e) { return null; }
        }
        default: return v.value;
        }
    }

    // ─── Smart overload selection ─────────────────────────────────────
    function argMatchesType(arg, jtype) {
        if (arg === null || arg === undefined) return true;  // null matches anything
        var t = typeof arg;
        var tn = jtype.className;
        if (t === "number") {
            if (tn === "int" || tn === "long" || tn === "short" ||
                tn === "byte" || tn === "float" || tn === "double" ||
                tn === "java.lang.Integer" || tn === "java.lang.Long" ||
                tn === "java.lang.Short" || tn === "java.lang.Byte" ||
                tn === "java.lang.Float" || tn === "java.lang.Double" ||
                tn === "java.lang.Number" || tn === "java.lang.Object")
                return true;
            return false;
        }
        if (t === "string") {
            if (tn === "java.lang.String" || tn === "java.lang.CharSequence" ||
                tn === "java.lang.Object") return true;
            return false;
        }
        if (t === "boolean") {
            if (tn === "boolean" || tn === "java.lang.Boolean" ||
                tn === "java.lang.Object") return true;
            return false;
        }
        if (t === "object" && arg.$className) {
            var acn = String(arg.$className);
            if (acn === tn || tn === "java.lang.Object") return true;
            // Try assignable check via Class.isAssignableFrom
            try {
                var argCls = Java.use(acn);
                var targetCls = Java.use(tn);
                return argCls.class.isAssignableFrom(targetCls.class) ||
                       targetCls.class.isAssignableFrom(argCls.class);
            } catch (e) { return false; }
        }
        return false;
    }

    function pickOverload(overloads, args) {
        if (!overloads || overloads.length === 0) return null;
        // 1. Exact arity
        var candidates = [];
        for (var i = 0; i < overloads.length; i++) {
            if (overloads[i].argumentTypes.length === args.length)
                candidates.push(overloads[i]);
        }
        if (candidates.length === 0) return null;
        if (candidates.length === 1) return candidates[0];
        // 2. Exact type match
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
        // 3. Fallback
        return candidates[0];
    }

    // ═══════════════════════════════════════════════════════════════
    //  cpp_* handlers (reply-based)
    // ═══════════════════════════════════════════════════════════════
    var cppHandlers = {
        cpp_use_class: function (cmd) {
            replyHandle(cmd.id, alloc(Java.use(cmd.className)));
        },
        cpp_get_method: function (cmd) {
            var c = get(cmd.classHandle);
            if (!c) throw new Error("no class handle");
            var m = c[cmd.methodName];
            if (!m) throw new Error("no method: " + cmd.methodName);
            var method = cmd.signature
                ? m.overload.apply(m, parseSig(cmd.signature))
                : ((m.overloads && m.overloads.length > 0) ? m.overloads[0] : m);
            replyHandle(cmd.id, alloc(method));
        },
        cpp_release_handle: function (cmd) { drop(cmd.handle); replyVoid(cmd.id); },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },
        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_eval: function (cmd) {
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                reply(cmd.id, true, "value", describe(fn()));
            } catch (e) { replyError(cmd.id, e); }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  jv_* handlers — the Java layer
    // ═══════════════════════════════════════════════════════════════
    var jvHandlers = {

        // Enumerate loaded classes.
        jv_enumerate: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                var f = cmd.filter || "";
                var out = [];
                for (var i = 0; i < all.length; i++) {
                    if (f && all[i].indexOf(f) < 0) continue;
                    out.push(all[i]);
                }
                sendMaybeChunked({type:"classes_list", count:out.length,
                                  filter:f, classes:out});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[], error:""+e});
            }
        },

        // Full class detail: methods (with generics) + fields + constructors.
        jv_detail: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var jcls = C.class;

                // ─ Methods ─────────────────────────────────────────────
                var methods = [];
                var ms = jcls.getDeclaredMethods();
                for (var i = 0; i < ms.length; i++) {
                    try {
                        var m = ms[i];
                        m.setAccessible(true);
                        var pts = m.getParameterTypes();
                        var genTypes = null;
                        try { genTypes = m.getGenericParameterTypes(); } catch (e) {}
                        var args = [];
                        for (var j = 0; j < pts.length; j++) {
                            var tn = String(pts[j].getName());
                            var kd = classifyType(tn);
                            var ev = [];
                            if (kd === "object") {
                                try {
                                    var pc = Java.use(tn);
                                    if (pc.class.isEnum()) {
                                        kd = "enum";
                                        var vals = pc.values();
                                        for (var e2 = 0; e2 < vals.length; e2++)
                                            ev.push(String(vals[e2].name()));
                                    }
                                } catch (ex) {}
                            }
                            var generic = null;
                            if (genTypes && genTypes[j]) {
                                try { generic = String(genTypes[j].toString()); } catch (e) {}
                            }
                            args.push({typeName: tn, kind: kd,
                                        enumValues: ev, generic: generic});
                        }
                        var mods = m.getModifiers();
                        methods.push({
                            name: String(m.getName()),
                            ret: String(m.getReturnType().getName()),
                            isStatic: (mods & 8) !== 0,
                            isAbstract: (mods & 1024) !== 0,
                            args: args
                        });
                    } catch (e) {}
                }

                // ─ Fields ──────────────────────────────────────────────
                var fields = [];
                var fs = jcls.getDeclaredFields();
                for (var k = 0; k < fs.length; k++) {
                    try {
                        var f = fs[k];
                        f.setAccessible(true);
                        var mods2 = f.getModifiers();
                        var isStatic = (mods2 & 8) !== 0;
                        var fval = "";
                        if (isStatic) {
                            try {
                                var raw = f.get(null);
                                fval = describe(raw);
                            } catch (e) { fval = "<read err>"; }
                        }
                        fields.push({
                            name: String(f.getName()),
                            type: String(f.getType().getName()),
                            isStatic: isStatic,
                            value: fval
                        });
                    } catch (e) {}
                }

                // ─ Constructors ────────────────────────────────────────
                var ctors = [];
                var cs = jcls.getDeclaredConstructors();
                for (var q = 0; q < cs.length; q++) {
                    try {
                        cs[q].setAccessible(true);
                        var cpts = cs[q].getParameterTypes();
                        var cargs = [];
                        for (var w = 0; w < cpts.length; w++) {
                            var ctn = String(cpts[w].getName());
                            cargs.push({typeName: ctn, kind: classifyType(ctn),
                                        enumValues: []});
                        }
                        ctors.push({args: cargs});
                    } catch (e) {}
                }

                sendMaybeChunked({
                    type:"class_detail_result",
                    className:cmd.className,
                    methods:methods,
                    fields:fields,
                    constructors:ctors
                });
            } catch (e) {
                send({type:"class_detail_result", className:cmd.className,
                      methods:[], fields:[], constructors:[], error:""+e});
            }
        },

        // Trace a method (all overloads), with args + return + backtrace.
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
                            var s = [];
                            for (var k = 0; k < a.length; k++) {
                                try { s.push(describe(a[k], 3)); }
                                catch (e) { s.push("<err>"); }
                            }
                            // Backtrace
                            var bt = "";
                            try {
                                var b = Java.backtrace({limit: 6});
                                if (b && b.frames) {
                                    var fr = [];
                                    for (var f = 0; f < b.frames.length; f++) {
                                        var fr0 = b.frames[f];
                                        fr.push(String(fr0.className || "") +
                                                "." + String(fr0.methodName || ""));
                                    }
                                    bt = " bt:[" + fr.join(" < ") + "]";
                                }
                            } catch (e) {}
                            try {
                                send({type:"trace_line", level:"log",
                                      line:"[" + cmd.className + "." +
                                           cmd.methodName + "] (" +
                                           s.join(", ") + ")" + bt});
                            } catch (e) {}
                            var r;
                            try {
                                r = ov.__ygg_trace
                                    ? ov.__ygg_trace.apply(this, a) : undefined;
                            } catch (ex) {
                                try {
                                    send({type:"trace_line", level:"error",
                                          line:"[" + cmd.className + "." +
                                               cmd.methodName + "] THREW: " + ex});
                                } catch (e2) {}
                                throw ex;
                            }
                            if (r !== undefined) {
                                try {
                                    send({type:"trace_line", level:"log",
                                          line:"[" + cmd.className + "." +
                                               cmd.methodName + "] -> " +
                                               describe(r, 3)});
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

        // Find instances with full field dump.
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
                                    fields.push({
                                        name: String(f.getName()),
                                        value: describe(f.get(x), 3)
                                    });
                                } catch (e) {
                                    fields.push({name: String(f.getName()),
                                                 value: "<err>"});
                                }
                            }
                            items.push({handle:h,
                                        className:String(x.$className),
                                        fields:fields});
                        } catch (e) {}
                    },
                    onComplete: function() {}
                });
                send({type:"instances_result", className:cmd.className,
                      items:items});
            } catch (e) {
                send({type:"instances_result", className:cmd.className,
                      items:[], error:""+e});
            }
        },

        // Create a new instance via constructor.
        jv_new: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var args = (cmd.argsSpec || []).map(function(a) { return mat(a); });
                var inst = C.$new.apply(C, args);
                var h = alloc(inst);
                send({type:"call_result", ok:true,
                      result:"new " + cmd.className + " handle=" + h});
            } catch (e) {
                send({type:"call_result", ok:false, error:""+e});
            }
        },

        // Smart call: pick overload by types.
        jv_call: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var args = (cmd.argsSpec || []).map(function(a) { return mat(a); });
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
                if (!chosen) throw new Error("no matching overload for " +
                    args.length + " args");
                var r = isStatic ? chosen.apply(null, args)
                                 : chosen.apply(inst, args);
                send({type:"call_result", ok:true, result:describe(r, 6)});
            } catch (e) {
                send({type:"call_result", ok:false, error:""+e});
            }
        },

        // Deep inspect an object (recursive).
        jv_inspect: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var C = Java.use(String(obj.$className));
                var fields = [];
                var flds = C.class.getDeclaredFields();
                for (var i = 0; i < flds.length; i++) {
                    var f = flds[i];
                    try {
                        f.setAccessible(true);
                        fields.push({
                            name: String(f.getName()),
                            type: String(f.getType().getName()),
                            value: describe(f.get(obj), 4)
                        });
                    } catch (e) {}
                }
                sendMaybeChunked({
                    type:"inspect_result",
                    className: String(obj.$className),
                    fields: fields
                });
            } catch (e) {
                send({type:"inspect_result", error:""+e});
            }
        },

        // Watch a field: poll it and emit on change.
        jv_watch_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var fieldName = cmd.fieldName;
                var intervalMs = cmd.intervalMs || 500;
                if (!obj.__ygg_watchers) obj.__ygg_watchers = {};
                if (obj.__ygg_watchers[fieldName]) return;
                var lastVal = describe(obj[fieldName]);
                obj.__ygg_watchers[fieldName] = setInterval(function() {
                    try {
                        var cur = describe(obj[fieldName]);
                        if (cur !== lastVal) {
                            send({type:"watch_change",
                                  fieldName:fieldName,
                                  oldValue:lastVal,
                                  newValue:cur});
                            lastVal = cur;
                        }
                    } catch (e) {}
                }, intervalMs);
                send({type:"watch_started", fieldName:fieldName});
            } catch (e) {
                send({type:"watch_err", error:""+e});
            }
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

        // Read instance field (any type).
        jv_read_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                var v = obj[cmd.fieldName];
                send({type:"field_result", ok:true, value:describe(v, 4)});
            } catch (e) {
                send({type:"field_result", ok:false, error:""+e});
            }
        },

        jv_write_field: function (cmd) {
            try {
                var obj = get(cmd.handle);
                if (!obj) throw new Error("no handle");
                obj[cmd.fieldName] = mat(cmd.value);
                send({type:"field_write_ok"});
            } catch (e) {
                send({type:"field_write_err", error:""+e});
            }
        },

        // Hook a method (enter / leave / exception).
        jv_hook: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var tag = cmd.className + "." + cmd.methodName;
                for (var i = 0; i < MM.overloads.length; i++) {
                    (function(ov) {
                        if (ov.__ygg_hook !== undefined) return;
                        ov.__ygg_hook = ov.implementation || null;
                        ov.implementation = function() {
                            var a = Array.prototype.slice.call(arguments);
                            var as = [];
                            for (var k = 0; k < a.length; k++) {
                                try { as.push(describe(a[k], 3)); }
                                catch (e) { as.push("<err>"); }
                            }
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
                                    bt = " bt:[" + fr.join(" < ") + "]";
                                }
                            } catch (e) {}
                            send({type:"hook_enter", tag:tag,
                                  args:as, backtrace:bt});
                            var r;
                            try {
                                r = ov.__ygg_hook
                                    ? ov.__ygg_hook.apply(this, a) : undefined;
                            } catch (ex) {
                                send({type:"hook_exception", tag:tag,
                                      exception:""+ex});
                                throw ex;
                            }
                            send({type:"hook_leave", tag:tag,
                                  ret:describe(r, 3)});
                            return r;
                        };
                    })(MM.overloads[i]);
                }
                send({type:"hook_ok", tag:tag});
            } catch (e) {
                send({type:"hook_err", error:""+e});
            }
        },

        jv_unhook: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (MM && MM.overloads) {
                    for (var i = 0; i < MM.overloads.length; i++) {
                        var ov = MM.overloads[i];
                        if (ov.__ygg_hook !== undefined) {
                            ov.implementation = ov.__ygg_hook;
                            delete ov.__ygg_hook;
                        }
                    }
                }
                send({type:"unhook_ok"});
            } catch (e) { send({type:"unhook_err", error:""+e}); }
        },

        // Evaluate arbitrary JS with Java in scope.
        jv_eval: function (cmd) {
            try {
                var fn = new Function("Java", "return (" + cmd.code + ");");
                var r = fn(Java);
                send({type:"eval_result", ok:true, result:describe(r, 6)});
            } catch (e) {
                send({type:"eval_result", ok:false, error:""+e});
            }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  native handlers
    // ═══════════════════════════════════════════════════════════════
    var nativeHandlers = {
        eval: function (cmd) {
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                send({type:"eval_result", ok:true, result:describe(fn())});
            } catch (e) {
                send({type:"eval_result", ok:false, error:""+e});
            }
        },
        run_user_script: function (cmd) {
            var nm = cmd.name || "anon";
            var code = cmd.code || "";
            try {
                Java.performNow(function () { (new Function(code))(); });
                send({type:"user_script_loaded", name:nm,
                      size:code.length, ok:true});
            } catch (e) {
                send({type:"user_script_loaded", name:nm,
                      ok:false, error:""+e});
            }
        },
        enumerate_classes: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                sendMaybeChunked({type:"classes_list", count:all.length, classes:all});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[], error:""+e});
            }
        }
    };

    // ─── Dispatcher ───────────────────────────────────────────────────
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

    // ─── ModView loader + attach ──────────────────────────────────────
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
                    var cls = loaders[i].loadClass("com.yamgg.modview.ModView");
                    if (cls) {
                        g_targetLoader = loaders[i];
                        Java.classFactory.loader = g_targetLoader;
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
        try {
            g_modViewClass = Java.use("com.yamgg.modview.ModView");
            send({type:"modview_class_ok"});
        } catch (e) {
            send({type:"modview_class_err", message:""+e});
            return null;
        }
        return g_modViewClass;
    }

    var attachDone = false;
    var attachAttempts = 0;

    function tryOne(tag, act) {
        if (!act || attachDone) return false;
        var Mv = getModViewClass();
        if (!Mv) return false;
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

    function tryUnityPlayer() {
        try {
            var UP = Java.use("com.unity3d.player.UnityPlayer");
            var ca = UP.currentActivity.value;
            if (ca && tryOne("UnityPlayer", ca)) return true;
        } catch (e) {}
        return false;
    }
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
        } catch (e) {}
        return false;
    }

    function tryAttachOnce() {
        attachAttempts++;
        Java.performNow(function () {
            if (attachDone) return;
            if (tryUnityPlayer()) return;
            if (tryMActivities()) return;
        });
    }

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
                            try { tryOne("onResume", this); } catch (e) {}
                        }
                    };
                    send({type:"hook_installed"});
                } catch (e) {
                    send({type:"hook_error", message:"install: " + e});
                }
            });
        } catch (e) { send({type:"hook_outer_error", message:""+e}); }
    }

    // ─── Poller — THE event loop ─────────────────────────────────────
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
                    try { send({type:"boot_poller_v6"}); } catch (e) {}
                    installAllHooks();
                    tryAttachOnce();
                    try { send({type:"boot_poller_done_v6"}); } catch (e) {}
                }

                if (!attachDone) {
                    try { tryAttachOnce(); } catch (e) {}
                }

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
    try { send({type:"boot_signals_done_v6"}); } catch (e) {}
})();
