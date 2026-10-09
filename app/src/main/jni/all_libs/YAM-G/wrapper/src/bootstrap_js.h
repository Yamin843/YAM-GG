// Auto-generated. Do not edit.
#ifndef YAMGG_BOOTSTRAP_JS_H
#define YAMGG_BOOTSTRAP_JS_H

namespace yamgg_bootstrap {

static const char kBootstrapSrc[] = R"YAMJS(
(function () {
    "use strict";

    // ═══════ HANDLE REGISTRY ═══════
    var table = Object.create(null);
    var nextHandle = 1;

    // لا حدود ثابتة. الحذف تلقائي عندما يجمع JS الـ handle object.
    // FinalizationRegistry متاح في QuickJS 2020+ و V8 8+.
    var registry = (typeof FinalizationRegistry === "function")
        ? new FinalizationRegistry(function (id) {
            if (Object.prototype.hasOwnProperty.call(table, id)) {
                delete table[id];
            }
        })
        : null;

    function alloc(o) {
        var id = nextHandle++;
        table[id] = o;
        if (registry && o !== null && typeof o === "object") {
            try { registry.register(o, id); } catch (e) {}
        }
        return id;
    }

    function drop(id) {
        if (Object.prototype.hasOwnProperty.call(table, id)) {
            var o = table[id];
            if (registry && o !== null && typeof o === "object") {
                try { registry.unregister(o); } catch (e) {}
            }
            delete table[id];
        }
    }
    function get(id) { return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null; }
    function drop(id) { if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id]; }
    function dropAll() { table = Object.create(null); nextHandle = 1; }

    var LB = "\u005B";
    var RB = "\u005D";

    // ═══════ REPLY ENVELOPE ═══════
    function reply(id, ok, kind, value, handle, error) {
        var m = { id: id, ok: !!ok };
        if (kind)   m.kind = kind;
        if (value  !== undefined && value  !== null) m.result = value;
        if (handle !== undefined && handle !== null) m.handle = handle;
        if (error)  m.error = String(error);
        try { send({ type: "reply", payload: JSON.stringify(m) }); } catch (e) {}
    }
    function replyHandle(id, h) { reply(id, true, "handle", null, h); }
    function replyValue(id, v)  { reply(id, true, "value", v, null); }
    function replyVoid(id)      { reply(id, true, "void", null, null); }
    function replyError(id, e)  { reply(id, false, "error", null, null, (e && e.message) ? e.message : String(e)); }

    // ═══════ JNI SIG PARSER ═══════
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

    // ═══════ MATERIALIZER (complex Java types) ═══════
    function mat(v) {
        if (v === null || v === undefined) return v;
        if (typeof v !== "object") return v;

        // Handle reference
        if (typeof v.handle === "number") return get(v.handle);

        if (!("kind" in v)) {
            if ("value" in v) return v.value;
            return v;
        }

        switch (v.kind) {
        case "int": case "short": case "byte": return v.value | 0;

        case "long":
            if (typeof Int64 === "function") return Int64(String(v.value));
            try { send({type:"log", level:"warn",
                        message:"Int64 unavailable; long may lose precision"}); } catch (e) {}
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

        case "enum": {
            var ec = Java.use(v.className);
            return ec.valueOf(v.enumName);
        }

        case "array": {
            var elType = v.elementType || "java.lang.Object";
            var elems = (v.elements || []).map(mat);
            return Java.array(elType, elems);
        }

        // Array where every element is a Java handle (already-instantiated)
        case "handle_array": {
            var hElType = v.elementType || "java.lang.Object";
            var hElems = (v.handles || []).map(function (h) { return get(h); });
            return Java.array(hElType, hElems);
        }

        // Nested array (arbitrary dimensions): [[int, int], [int, int]]
        case "nested_array": {
            var nElType = v.elementType || "java.lang.Object";
            var dims = v.dimensions || 1;
            // Build Java type name: [[Ljava.lang.Object; for dims=2
            var javaType = nElType;
            for (var d = 0; d < dims; d++) javaType = "[" + javaType;
            var nElems = (v.elements || []).map(function (e) {
                // If sub-element is already a nested_array spec, recurse.
                if (e && typeof e === "object" && e.kind === "nested_array") {
                    return mat(e);
                }
                return mat(e);
            });
            return Java.array(javaType, nElems);
        }

        case "list": {
            var listType = v.className || "java.util.ArrayList";
            var ArrayList = Java.use(listType);
            var lst = ArrayList.$new();
            var items = v.items || [];
            for (var li = 0; li < items.length; li++) {
                lst.add(mat(items[li]));
            }
            return lst;
        }

        case "set": {
            var setType = v.className || "java.util.HashSet";
            var HashSet = Java.use(setType);
            var st = HashSet.$new();
            var sitems = v.items || [];
            for (var si = 0; si < sitems.length; si++) {
                st.add(mat(sitems[si]));
            }
            return st;
        }

        case "map": {
            var mapType = v.className || "java.util.HashMap";
            var HashMap = Java.use(mapType);
            var mp = HashMap.$new();
            var entries = v.entries || [];
            for (var ei = 0; ei < entries.length; ei++) {
                var e = entries[ei];
                var mk = ("k" in e) ? mat(e.k) : null;
                var mv = ("v" in e) ? mat(e.v) : null;
                if (e.k != null && typeof e.k === "object" && e.k.kind === "string") {
                    mk = Java.use("java.lang.String").$new(e.k.value);
                }
                if (e.v != null && typeof e.v === "object" && e.v.kind === "string") {
                    mv = Java.use("java.lang.String").$new(e.v.value);
                }
                mp.put(mk, mv);
            }
            return mp;
        }

        case "construct": {
            var cc = Java.use(v.className);
            var args = (v.args || []).map(mat);
            var inst;
            if (v.ctorSig) {
                inst = cc.$alloc();
                cc.$init.overload.apply(cc.$init, parseSig(v.ctorSig))
                           .call(inst, ...args);
            } else {
                inst = cc.$new.apply(cc, args);
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

    // ═══════ DESCRIBE (unbounded, cycle-safe) ═══════
    // لا حدود على عدد العناصر أو عمق الكائنات. نستخدم WeakSet لتتبع
    // الدورات (cycles) لتفادي infinite recursion فقط.
    function describe(v) {
        var seen = new WeakSet();
        return describeRec(v, seen, 0);
    }

    function describeRec(v, seen, depth) {
        if (v === null) return "null";
        if (v === undefined) return "undefined";
        var t = typeof v;
        if (t === "number" || t === "boolean") return String(v);
        if (t === "string") return JSON.stringify(v);
        if (t === "function") return "<fn " + (v.name || "anon") + ">";
        if (t !== "object") return String(v);

        // cycle check
        if (typeof seen.add === "function") {
            if (seen.has(v)) return "<circular>";
            try { seen.add(v); } catch (e) {}
        }

        try {
            var cn = v.$className ? String(v.$className) : "";
            if (!cn) {
                // plain JS object — لا نتعمق
                return JSON.stringify(v);
            }

            if (cn === "java.lang.String") return JSON.stringify(String(v));

            // الأغلفة البدائية
            if (cn === "java.lang.Integer" || cn === "java.lang.Long" ||
                cn === "java.lang.Short" || cn === "java.lang.Byte" ||
                cn === "java.lang.Float" || cn === "java.lang.Double" ||
                cn === "java.lang.Boolean" || cn === "java.lang.Character")
                return String(v);

            // المصفوفات — بلا حدود
            if (cn.charAt(0) === LB) {
                var out = [];
                var n = 0;
                try { n = Number(v.length) || 0; } catch (e) { n = 0; }
                for (var i = 0; i < n; i++) {
                    var el;
                    try { el = v[i]; } catch (e) { el = null; }
                    out.push(describeRec(el, seen, depth + 1));
                }
                return "(" + cn + ")" + LB + out.join(",") + RB;
            }

            // Map
            if (cn.indexOf("Map") >= 0) {
                var mp = [];
                try {
                    var it = v.entrySet().iterator();
                    while (it.hasNext()) {
                        var e = it.next();
                        var k = e.getKey();
                        var val = e.getValue();
                        mp.push(describeRec(k, seen, depth + 1) + ":" +
                                describeRec(val, seen, depth + 1));
                    }
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + "){" + mp.join(",") + "}";
            }

            // List / Set / Collection
            if (cn.indexOf("List") >= 0 ||
                cn.indexOf("Set") >= 0 ||
                cn.indexOf("Collection") >= 0 ||
                cn.indexOf("Queue") >= 0 ||
                cn.indexOf("Deque") >= 0) {
                var li = [];
                try {
                    var it2 = v.iterator();
                    while (it2.hasNext()) {
                        li.push(describeRec(it2.next(), seen, depth + 1));
                    }
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + ")" + LB + li.join(",") + RB;
            }

            return "<" + cn + " " + String(v) + ">";
        } catch (e) {
            return "<err:" + e + ">";
        }
    }

    // ═══════ cpp_* HANDLERS ═══════
    var handlers = {
        cpp_use_class: function (cmd) { replyHandle(cmd.id, alloc(Java.use(cmd.className))); },
        cpp_get_method: function (cmd) {
            var c = get(cmd.classHandle); if (!c) throw new Error("no class handle");
            var m = c[cmd.methodName]; if (!m) throw new Error("no method: " + cmd.methodName);
            var method = cmd.signature ? m.overload.apply(m, parseSig(cmd.signature))
                                       : ((m.overloads && m.overloads.length > 0) ? m.overloads[0] : m);
            replyHandle(cmd.id, alloc(method));
        },
        cpp_cast: function (cmd) {
            var cls = get(cmd.classHandle); var obj = get(cmd.objectHandle);
            if (!cls || !obj) throw new Error("cast: missing handle");
            replyHandle(cmd.id, alloc(Java.cast(obj, cls)));
        },
        cpp_create_string: function (cmd) { replyHandle(cmd.id, alloc(Java.use("java.lang.String").$new(cmd.value))); },
        cpp_new_instance: function (cmd) {
            var cls = get(cmd.classHandle); if (!cls) throw new Error("no class handle");
            var args = (cmd.args || []).map(mat);
            var inst;
            if (cmd.ctorSig && cmd.ctorSig.length > 0) {
                inst = cls.$alloc();
                var ctor = cls.$init.overload.apply(cls.$init,
                                                     parseSig(cmd.ctorSig));
                ctor.call(inst, ...args);
            } else {
                inst = cls.$new.apply(cls, args);
            }
            if (cmd.fields) {
                for (var i = 0; i < cmd.fields.length; i++) {
                    var fname = cmd.fields[i].name;
                    var fval = mat(cmd.fields[i].value);
                    inst[fname] = fval;
                }
            }
            replyHandle(cmd.id, alloc(inst));
        },
        cpp_array_of: function (cmd) {
            var elType = cmd.elementType || "java.lang.Object";
            var elems = (cmd.elements || []).map(mat);
            replyHandle(cmd.id, alloc(Java.array(elType, elems)));
        },
        cpp_array_length: function (cmd) { var arr = get(cmd.handle); replyValue(cmd.id, arr ? Number(arr.length) : 0); },
        cpp_array_get: function (cmd) {
            var arr = get(cmd.handle); var el = arr ? arr[cmd.index] : null;
            if (el === null || el === undefined) { replyValue(cmd.id, null); return; }
            if (typeof el === "object" && el.$className) replyHandle(cmd.id, alloc(el));
            else replyValue(cmd.id, el);
        },
        cpp_array_set: function (cmd) {
            var arr = get(cmd.handle); if (!arr) throw new Error("no array");
            arr[cmd.index] = mat(cmd.value); replyVoid(cmd.id);
        },
        cpp_release_handle: function (cmd) { drop(cmd.handle); replyVoid(cmd.id); },
        cpp_list_handles: function (cmd) {
            var list = [];
            for (var k in table) {
                if (!Object.prototype.hasOwnProperty.call(table, k)) continue;
                var o = table[k];
                list.push({ id: Number(k), kind: "java", className: (o && o.$className) || "" });
            }
            replyValue(cmd.id, list);
        },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },
        cpp_get_field: function (cmd) {
            var cls = Java.use(cmd.className);
            var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            var isStatic = false;
            try {
                var fields = cls.class.getDeclaredFields();
                for (var i = 0; i < fields.length; i++) {
                    if (String(fields[i].getName()) === cmd.fieldName) {
                        isStatic = (Number(fields[i].getModifiers()) & 8) !== 0;
                        break;
                    }
                }
            } catch (e) {}
            replyValue(cmd.id, JSON.stringify({ static: isStatic, handle: alloc(f) }));
        },
        cpp_inspect_handle: function (cmd) {
            var obj = get(cmd.handleId); if (!obj) throw new Error("no handle");
            replyValue(cmd.id, { className: obj.$className || "unknown", stringValue: describe(obj) });
        },

        cpp_read_static: function (cmd) {
            var cls = Java.use(cmd.className);
            var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            replyValue(cmd.id, describe(f.value));
        },
        cpp_write_static: function (cmd) {
            var cls = Java.use(cmd.className);
            var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            f.value = mat(cmd.value);
            replyVoid(cmd.id);
        },
        cpp_read_field: function (cmd) {
            var inst = get(cmd.handleId);
            if (!inst) throw new Error("no instance");
            var v = inst[cmd.fieldName];
            replyValue(cmd.id, describe(v));
        },
        cpp_write_field: function (cmd) {
            var inst = get(cmd.handleId);
            if (!inst) throw new Error("no instance");
            inst[cmd.fieldName] = mat(cmd.value);
            replyVoid(cmd.id);
        },
        cpp_list_own: function (cmd) {
            var c = Java.use(cmd.className);
            var props = Object.getOwnPropertyNames(c);
            var out = [];
            for (var i = 0; i < props.length; i++) {
                var k = props[i];
                if (k.charAt(0) === "_") continue;
                if (k === "$init" || k === "$alloc" || k === "$new") continue;
                if (k === "class" || k === "valueOf") continue;
                out.push(k);
            }
            sendMaybeChunked({type: "list_own_result",
                              className: cmd.className,
                              items: out});
            replyValue(cmd.id, JSON.stringify(out));
        },
        cpp_list_overloads: function (cmd) {
            var c = Java.use(cmd.className);
            var m = c[cmd.methodName];
            if (!m) throw new Error("no method: " + cmd.methodName);
            var out = [];
            if (m.overloads) {
                for (var i = 0; i < m.overloads.length; i++) {
                    var ov = m.overloads[i];
                    var args = ov.argumentTypes
                        ? ov.argumentTypes.map(function(t){return t.className;}).join(", ")
                        : "";
                    var ret = ov.returnType ? ov.returnType.className : "?";
                    out.push("(" + args + ") -> " + ret);
                }
            }
            sendMaybeChunked({type: "list_overloads_result",
                              className: cmd.className,
                              methodName: cmd.methodName,
                              items: out});
            replyValue(cmd.id, JSON.stringify(out));
        },
        cpp_list_static_fields: function (cmd) {
            var cls = Java.use(cmd.className);
            var fields = cls.class.getDeclaredFields();
            var out = [];
            for (var i = 0; i < fields.length; i++) {
                var mods = Number(fields[i].getModifiers());
                if ((mods & 8) === 0) continue;
                var name = String(fields[i].getName());
                var type = String(fields[i].getType().getName());
                var val = "";
                try {
                    fields[i].setAccessible(true);
                    var raw = fields[i].get(null);
                    val = (raw === null) ? "null" : String(raw);
                } catch (e) { val = "<err>"; }
                out.push(name + " : " + type + " = " + val);
            }
            replyValue(cmd.id, JSON.stringify(out));
        },
        cpp_probe_class: function (cmd) {
            var c = Java.use(cmd.className);
            var methods = [];
            var ms = c.class.getDeclaredMethods();
            for (var i = 0; i < ms.length; i++) {
                try { ms[i].setAccessible(true); } catch (e2) {}
                var pts = ms[i].getParameterTypes();
                var args = [];
                for (var j = 0; j < pts.length; j++) {
                    args.push({name: "arg" + j, typeName: String(pts[j].getName())});
                }
                methods.push({
                    name: String(ms[i].getName()),
                    ret: String(ms[i].getReturnType().getName()),
                    isStatic: (Number(ms[i].getModifiers()) & 8) !== 0,
                    args: args
                });
            }
            var fields = [];
            var fs = c.class.getDeclaredFields();
            for (var k = 0; k < fs.length; k++) {
                fields.push({
                    name: String(fs[k].getName()),
                    type: String(fs[k].getType().getName()),
                    value: ""
                });
            }
            var ctors = [];
            try {
                var cs = c.class.getDeclaredConstructors();
                for (var q = 0; q < cs.length; q++) {
                    var ptypes = cs[q].getParameterTypes();
                    var cargs = [];
                    for (var w = 0; w < ptypes.length; w++) {
                        cargs.push({name: "arg" + w, typeName: String(ptypes[w].getName())});
                    }
                    ctors.push({params: cargs});
                }
            } catch (e3) {}
            sendMaybeChunked({type: "class_probe", className: cmd.className,
                               methods: methods, fields: fields,
                               constructors: ctors});
            replyValue(cmd.id, JSON.stringify({
                methods: methods.length,
                fields: fields.length
            }));
        },
        cpp_call_method: function (cmd) {
            var m = get(cmd.methodHandle);
            if (!m) throw new Error("no method handle");
            var inst = cmd.instanceHandle ? get(cmd.instanceHandle) : null;
            var args = (cmd.args || []).map(mat);
            var r;
            if (inst) r = m.apply(inst, args);
            else      r = m.apply(null, args);
            if (r === null || r === undefined) replyValue(cmd.id, null);
            else if (typeof r === "object" && r.$className)
                replyHandle(cmd.id, alloc(r));
            else replyValue(cmd.id, r);
        },
        cpp_hook_method: function (cmd) {
            var m = get(cmd.methodHandle);
            if (!m) throw new Error("no method handle");
            var cbId = cmd.callbackId;

            if (m.__ygg_hook_cb === cbId) { replyVoid(cmd.id); return; }
            if (m.__ygg_hook_orig === undefined) {
                m.__ygg_hook_orig = m.implementation;
            }
            m.__ygg_hook_cb = cbId;
            var orig = m.__ygg_hook_orig;
            // captureReturn: يمنع تسريب handle على كل return إذا لم يطلبه C++.
            // الافتراضي false — الـ callback 2-arg لا يرى return أصلاً.
            var captureReturn = (cmd.captureReturn === true);

            m.implementation = function () {
                var a = Array.prototype.slice.call(arguments);

                // ─── ON ENTER ───
                var ah = [];
                for (var i = 0; i < a.length; i++) {
                    try { ah.push(alloc(a[i])); }
                    catch (e) { ah.push(0); }
                }
                // thisHandle: يُخصَّص فقط عند الطلب (كثيف في hook)
                var th = 0;
                if (cmd.captureThis === true) {
                    try { th = alloc(this); } catch (e) {}
                }
                try {
                    send({type: "hook_cb", callbackId: cbId, phase: "enter",
                          argHandles: ah, thisHandle: th});
                } catch (e) {}

                // ─── CALL ORIGINAL ───
                var r, threw = false, exMsg = "";
                try {
                    r = orig.apply(this, a);
                } catch (e) {
                    threw = true;
                    exMsg = "" + e;
                    // ─── ON EXCEPTION ───
                    try {
                        send({type: "hook_cb", callbackId: cbId,
                              phase: "exception",
                              exceptionMessage: exMsg});
                    } catch (e2) {}
                    // إعادة الرمي بعد إشعار
                    throw e;
                }

                // ─── ON LEAVE ───
                var rh = 0;
                if (captureReturn && r !== null && r !== undefined) {
                    try { rh = alloc(r); } catch (e) {}
                }
                try {
                    send({type: "hook_cb", callbackId: cbId, phase: "leave",
                          returnHandle: rh,
                          isVoid: (r === undefined)});
                } catch (e) {}

                return r;
            };
            replyVoid(cmd.id);
        },
        cpp_unhook_method: function (cmd) {
            var m = get(cmd.methodHandle);
            if (!m) { replyVoid(cmd.id); return; }
            if (m.__ygg_hook_orig !== undefined) {
                m.implementation = m.__ygg_hook_orig;
                delete m.__ygg_hook_orig;
                delete m.__ygg_hook_cb;
            }
            replyVoid(cmd.id);
        },
        cpp_get_field_instance: function (cmd) {
            var inst = get(cmd.instanceHandle);
            if (!inst) throw new Error("no instance");
            var v = inst[cmd.fieldName];
            if (v === null || v === undefined) { replyValue(cmd.id, null); return; }
            if (typeof v === "object" && v.$className)
                replyHandle(cmd.id, alloc(v));
            else replyValue(cmd.id, v);
        },
        cpp_set_field_instance: function (cmd) {
            var inst = get(cmd.instanceHandle);
            if (!inst) throw new Error("no instance");
            inst[cmd.fieldName] = mat(cmd.value);
            replyVoid(cmd.id);
        },
        cpp_eval: function (cmd) {
            var t0 = Date.now();
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                var r = fn();
                var rs = describe(r);
                send({type: "eval_result", id: cmd.id, ok: true,
                      result: rs, durationMs: Date.now() - t0});
                reply(cmd.id, true, "value", rs);
            } catch (e) {
                var es = "" + e;
                send({type: "eval_result", id: cmd.id, ok: false,
                      error: es, durationMs: Date.now() - t0});
                replyError(cmd.id, e);
            }
        },
        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_pong: function (cmd) { send({ type: "cpp_pong_received", id: cmd.id }); replyVoid(cmd.id); }
    };

    // ═══════ NATIVE HANDLERS ═══════
    var nativeHandlers = {
        eval: function (cmd) {
            var id = cmd.id;
            var t0 = Date.now();
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                var r = fn();
                send({type: "eval_result", id: id, ok: true,
                      result: describe(r), durationMs: Date.now() - t0});
            } catch (e) {
                send({type: "eval_result", id: id, ok: false,
                      error: "" + e, durationMs: Date.now() - t0});
            }
        },
        run_user_script: function (cmd) {
            var t0 = Date.now();
            var nm = cmd.name || "anon";
            var code = cmd.code || "";
            try {
                Java.performNow(function () {
                    var fn = new Function(code);
                    fn();
                });
                send({type: "user_script_loaded", name: nm,
                      size: code.length, ok: true,
                      durationMs: Date.now() - t0});
            } catch (e) {
                send({type: "user_script_loaded", name: nm,
                      ok: false, error: "" + e,
                      durationMs: Date.now() - t0});
                send({type: "script_failed", name: nm, error: "" + e});
            }
        },
        load_scripts_batch: function (cmd) {
            var ok = 0, fail = 0;
            var list = cmd.scripts || [];
            for (var i = 0; i < list.length; i++) {
                try {
                    var code = list[i].code || "";
                    Java.performNow(function () {
                        (new Function(code))();
                    });
                    ok++;
                    send({type: "user_script_loaded", name: list[i].name,
                          ok: true, size: code.length});
                } catch (e) {
                    fail++;
                    send({type: "script_failed", name: list[i].name, error: "" + e});
                }
            }
            send({type: "scripts_batch_done", ok: ok, fail: fail});
        },
        enumerate_classes: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                sendMaybeChunked({type: "classes_list",
                                   count: all.length,
                                   classes: all});
            } catch (e) {
                send({type: "classes_list", count: 0, classes: [],
                      error: "" + e});
            }
        },
        enumerate_loaders: function (cmd) {
            try {
                var ls = Java.enumerateClassLoadersSync();
                var out = [];
                for (var i = 0; i < ls.length; i++) {
                    try { out.push(String(ls[i])); } catch (e) {}
                }
                sendMaybeChunked({type: "loaders_list", count: out.length, loaders: out});
            } catch (e) {
                send({type: "loaders_list", count: 0, loaders: []});
            }
        },
        backtrace: function (cmd) {
            try {
                var limit = cmd.limit || 8;
                var bt = Java.backtrace({limit: limit});
                var frames = [];
                if (bt && bt.frames) {
                    for (var i = 0; i < bt.frames.length; i++) {
                        var f = bt.frames[i];
                        frames.push(String(f.signature ||
                            (f.className + "." + f.methodName)));
                    }
                }
                sendMaybeChunked({type: "backtrace_result", frames: frames});
            } catch (e) {
                send({type: "backtrace_result", frames: []});
            }
        }
    };

    // ═══════ CHUNKED SENDER ═══════
    // For any payload whose JSON representation exceeds 200 KB, split it
    // into 60 KB chunks. The C++ events layer reassembles them (see
    // yam_events.cpp::chunks::).
    var _nextChunkSession = 1;
    var CHUNK_THRESHOLD = 200 * 1024;
    var CHUNK_SIZE = 60 * 1024;

    function sendMaybeChunked(obj) {
        var json;
        try { json = JSON.stringify(obj); }
        catch (e) { json = ""; }
        if (json.length <= CHUNK_THRESHOLD) {
            send(obj);
            return;
        }
        var kind = obj && obj.type ? String(obj.type) : "chunked";
        var sid = _nextChunkSession++;
        var total = Math.max(1, Math.ceil(json.length / CHUNK_SIZE));

        send({type: "chunk_begin", sessionId: sid, kind: kind,
              totalChunks: total, totalBytes: json.length});
        for (var i = 0; i < total; i++) {
            send({type: "chunk_data", sessionId: sid, index: i,
                  totalChunks: total,
                  data: json.substring(i * CHUNK_SIZE,
                                        (i + 1) * CHUNK_SIZE)});
        }
        send({type: "chunk_end", sessionId: sid, kind: kind});
    }

    // ═══════ COMMAND DISPATCHER ═══════
    function unwrapCmd(msg) {
        if (!msg) return null;
        if (typeof msg === "string") {
            try { return JSON.parse(msg); } catch (e) { return null; }
        }
        if (msg.payload !== undefined && msg.payload !== null) return msg.payload;
        if (msg.action) return msg;
        return null;
    }

    function dispatchCmd(cmd) {
        if (!cmd || !cmd.action) return;
        var h = handlers[cmd.action];
        if (h) {
            try { h(cmd); }
            catch (e) { try { replyError(cmd.id, e); } catch (e2) {} }
            return;
        }
        var nh = nativeHandlers[cmd.action];
        if (nh) {
            try { nh(cmd); }
            catch (e) { try { replyError(cmd.id, e); } catch (e2) {} }
            return;
        }
        try { replyError(cmd.id, new Error("unknown: " + cmd.action)); } catch (e) {}
    }

    // ═══════ RECEIVER (recv channel) ═══════
    recv("yamgg_cmd", function (msg) {
        dispatchCmd(unwrapCmd(msg));
    });

    // ═══════ CLASS LOADER RESOLVER ═══════
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
                        send({ type: "loader_resolved", index: i, total: loaders.length });
                        return g_targetLoader;
                    }
                } catch (e) {}
            }
        } catch (e) { send({type:"loader_scan_err", message:"" + e}); }
        return null;
    }
    function getModViewClass() {
        if (g_modViewClass !== null) return g_modViewClass;
        var ldr = resolveLoader();
        if (!ldr) return null;
        try {
            g_modViewClass = Java.use("com.yamgg.modview.ModView");
            send({ type: "modview_class_ok" });
        } catch (e) {
            send({type:"modview_class_err", message:"" + e});
            return null;
        }
        return g_modViewClass;
    }

    // ═══════ ATTACH ═══════
    var attachDone = false;
    var attachAttempts = 0;

    function pickBestActivity(map) {
        if (!map) return null;
        var best = null;
        try {
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
                    try { if (act.isFinishing()) continue; } catch (e) {}
                    var focused = false;
                    try { focused = act.hasWindowFocus(); } catch (e) {}
                    if (focused) return act;
                    if (!best) best = act;
                } catch (e) {}
            }
        } catch (e) {}
        return best;
    }

    function tryOne(tag, act) {
        if (!act || attachDone) return false;
        var Mv = getModViewClass();
        if (!Mv) return false;
        try { if (act.isFinishing()) return false; } catch (e) {}
        try {
            Mv.attach(act);
            attachDone = true;
            send({ type: "attach_ok", strategy: tag,
                   className: "" + act.getClass().getName(),
                   attempts: attachAttempts });
            return true;
        } catch (e) {
            send({ type: "attach_try_err", strategy: tag, message: "" + e });
            return false;
        }
    }

    function tryUnityPlayer() {
        try {
            var UP = Java.use("com.unity3d.player.UnityPlayer");
            var ca = UP.currentActivity.value;
            if (ca && tryOne("UnityPlayer.currentActivity", ca)) return true;
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
            var act = pickBestActivity(map);
            if (act && tryOne("mActivities(best)", act)) return true;
        } catch (e) {}
        return false;
    }
    function tryWindowManager() {
        try {
            var WMG = Java.use("android.view.WindowManagerGlobal");
            var inst = WMG.getInstance();
            var roots = inst.mRoots.value;
            if (!roots) return false;
            var n = roots.size();
            var ActCls = Java.use("android.app.Activity");
            for (var j = 0; j < n; j++) {
                try {
                    var ri = roots.get(j);
                    var view = ri.mView.value;
                    if (!view) continue;
                    var ctx = view.getContext();
                    try {
                        var act = Java.cast(ctx, ActCls);
                        if (act && tryOne("WManager" + j, act)) return true;
                    } catch (e) {}
                } catch (e) {}
            }
        } catch (e) {}
        return false;
    }
    function tryJavaChoose() {
        if (attachDone) return false;
        var found = false;
        try {
            Java.choose("android.app.Activity", {
                onMatch: function (a) {
                    if (found || attachDone) return "stop";
                    if (tryOne("Java.choose", a)) { found = true; return "stop"; }
                },
                onComplete: function () {}
            });
        } catch (e) {}
        return found;
    }
    function tryAttachOnce() {
        attachAttempts++;
        Java.performNow(function () {
            if (attachDone) return;
            if (tryUnityPlayer())   return;
            if (tryMActivities())   return;
            if (tryWindowManager()) return;
            if (attachAttempts % 5 === 0) tryJavaChoose();
        });
    }
    function loopAttach() {
        if (attachDone) return;
        try { tryAttachOnce(); }
        catch (e) { send({type:"attach_outer_err", message:"" + e}); }
        setTimeout(loopAttach, 1000);
    }

    // ═══════ COMMAND POLLER ═══════
    var pollInstalled = false;
    var pollCount = 0;
    var lastAliveAt = 0;
    var POLL_INTERVAL_MS = 150;   // 6.7 ticks/sec
    var ALIVE_EVERY_MS  = 30000;  // 1 alive log / 30s

    function installPoller() {
        if (pollInstalled) return;
        pollInstalled = true;
        send({type: "poller_started", interval: POLL_INTERVAL_MS});

        setInterval(function () {
            pollCount++;
            Java.performNow(function () {
                var MV;
                try { MV = Java.use("com.yamgg.modview.ModView"); }
                catch (e) { return; }

                // ─────────────────────────────────────────────────────
                // بلا حد صناعي. نُفرّغ الطابور بالكامل في كل دورة.
                // الحماية الوحيدة: كشف حلقة منطقية (نفس الأمر يعود).
                // ─────────────────────────────────────────────────────
                var drained = 0;
                var lastRaw = null;
                var sameCount = 0;

                for (;;) {
                    var raw;
                    try { raw = MV.nativeGetPendingCmd(); }
                    catch (e) {
                        send({type: "poller_err", message: "" + e});
                        break;
                    }
                    if (!raw) break;

                    if (raw === lastRaw) {
                        sameCount++;
                        if (sameCount > 100) {
                            send({type: "poller_loop_detected",
                                  raw: raw.substring(0, 200)});
                            break;
                        }
                    } else {
                        sameCount = 0;
                        lastRaw = raw;
                    }
                    drained++;

                    var obj = null;
                    try { obj = JSON.parse(raw); }
                    catch (e) {
                        send({type: "poller_parse_err", message: "" + e});
                        continue;
                    }
                    if (!obj) continue;

                    var cmd = (obj.payload && obj.payload.action)
                        ? obj.payload
                        : obj;
                    if (!cmd || !cmd.action) {
                        send({type: "poller_no_action",
                              raw: raw.substring(0, 200)});
                        continue;
                    }
                    try { dispatchCmd(cmd); }
                    catch (e) {
                        try { replyError(cmd.id, e); } catch (e2) {}
                    }
                }

                // alive log فقط عندما لا يحدث أي شيء لـ ALIVE_EVERY_MS
                if (drained === 0) {
                    var now = Date.now();
                    if (now - lastAliveAt >= ALIVE_EVERY_MS) {
                        lastAliveAt = now;
                        send({type: "poller_alive", count: pollCount});
                    }
                }
            });
        }, POLL_INTERVAL_MS);
    }

    // ═══════ HOOKS ═══════
    function installAllHooks() {
        try {
            Java.performNow(function () {
                try {
                    var Activity = Java.use("android.app.Activity");
                    var origOnResume = Activity.onResume;
                    Activity.onResume.implementation = function () {
                        try { origOnResume.call(this); } catch (e) {}
                        if (!attachDone) { try { tryOne("onResume", this); } catch (e) {} }
                    };
                    send({type:"hook_installed"});
                } catch (e) { send({type:"hook_error", message: "install: " + e}); }
            });
        } catch (e) { send({type:"hook_outer_error", message: "" + e}); }
    }

    // ═══════ RPC EXPORTS ═══════
    try {
        rpc.exports = {
            ping: function () { return Date.now(); },
            eval: function (code) {
                try {
                    var fn = new Function("return (" + code + ");");
                    return { ok: true, result: describe(fn()) };
                } catch (e) {
                    return { ok: false, error: "" + e };
                }
            },
            run_script: function (name, code) {
                try {
                    Java.performNow(function () {
                        (new Function(code))();
                    });
                    return { ok: true };
                } catch (e) {
                    return { ok: false, error: "" + e };
                }
            }
        };
    } catch (e) {}

    // ═══════ READY + SCHEDULE ═══════
    try { send({ type: "cpp_ready" }); }       catch (e) {}
    try { send({ type: "cpp_ready_final" }); } catch (e) {}

    setTimeout(function () {
        try { installAllHooks(); }
        catch (e) { send({type:"install_err", message: "" + e}); }
        try { loopAttach(); }
        catch (e) { send({type:"attach_start_err", message: "" + e}); }
        try { installPoller(); }
        catch (e) { send({type:"poller_err", message: "" + e}); }
    }, 800);
})();
)YAMJS";

} // namespace yamgg_bootstrap

#endif // YAMGG_BOOTSTRAP_JS_H
