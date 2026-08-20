#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "vela/python.h"
#include "vela/log.h"
#include "vela/util.h"
#include "vela/metrics.h"
#include "vela/server.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>

static PyObject *g_app;
static PyObject *g_loop;
static PyObject *g_asyncio;
static PyObject *g_run;
static PyObject *g_empty_bytes;
static int g_ready;

static const char ASGI_RUNNER[] =
    "def vela_run(app, loop, scope, body):\n"
    "    result = {'status': 200, 'headers': [], 'body': bytearray(), 'started': False}\n"
    "    delivered = False\n"
    "    async def receive():\n"
    "        nonlocal delivered\n"
    "        if not delivered:\n"
    "            delivered = True\n"
    "            return {'type': 'http.request', 'body': body, 'more_body': False}\n"
    "        return {'type': 'http.disconnect'}\n"
    "    async def send(msg):\n"
    "        t = msg.get('type')\n"
    "        if t == 'http.response.start':\n"
    "            result['status'] = int(msg.get('status') or 200)\n"
    "            result['headers'] = msg.get('headers') or []\n"
    "            result['started'] = True\n"
    "        elif t == 'http.response.body':\n"
    "            b = msg.get('body') or b''\n"
    "            if b:\n"
    "                result['body'].extend(b)\n"
    "    loop.run_until_complete(app(scope, receive, send))\n"
    "    return result\n";

static void drain_pyerr(const char *where)
{
    if (!PyErr_Occurred())
        return;
    PyObject *t, *v, *tb;
    PyErr_Fetch(&t, &v, &tb);
    PyErr_NormalizeException(&t, &v, &tb);
    PyObject *s = PyObject_Str(v ? v : t);
    const char *msg = s ? PyUnicode_AsUTF8(s) : "?";
    MY_ERROR("Python exception in %s: %s", where, msg ? msg : "?");
    PyErr_Restore(t, v, tb);
    PyErr_Print();
    Py_XDECREF(s);
}

static int append_http_start(my_conn_t *c, int status, PyObject *headers)
{
    char line[128];
    int n = snprintf(line, sizeof line, "HTTP/1.1 %d %s\r\n", status,
                     status == 200 ? "OK" : "Response");
    my_buf_append(&c->out, line, (size_t)n);
    int has_len = 0, has_conn = 0, has_server = 0;
    Py_ssize_t nh = headers && PySequence_Check(headers) ? PySequence_Size(headers) : 0;
    for (Py_ssize_t i = 0; i < nh; i++) {
        PyObject *pair = PySequence_GetItem(headers, i);
        PyObject *k = NULL, *v = NULL;
        if (!pair)
            continue;
        if (PySequence_Check(pair) && PySequence_Size(pair) >= 2) {
            k = PySequence_GetItem(pair, 0);
            v = PySequence_GetItem(pair, 1);
        }
        Py_DECREF(pair);
        if (!k || !v) {
            Py_XDECREF(k);
            Py_XDECREF(v);
            continue;
        }
        const char *ks = NULL, *vs = NULL;
        Py_ssize_t kl = 0, vl = 0;
        if (PyBytes_Check(k))
            PyBytes_AsStringAndSize(k, (char **)&ks, &kl);
        else if (PyUnicode_Check(k))
            ks = PyUnicode_AsUTF8AndSize(k, &kl);
        if (PyBytes_Check(v))
            PyBytes_AsStringAndSize(v, (char **)&vs, &vl);
        else if (PyUnicode_Check(v))
            vs = PyUnicode_AsUTF8AndSize(v, &vl);
        if (!ks || !vs) {
            Py_DECREF(k);
            Py_DECREF(v);
            continue;
        }
        if (kl == 14 && !strncasecmp(ks, "content-length", 14))
            has_len = 1;
        if (kl == 10 && !strncasecmp(ks, "connection", 10))
            has_conn = 1;
        if (kl == 6 && !strncasecmp(ks, "server", 6))
            has_server = 1;
        my_buf_append(&c->out, ks, (size_t)kl);
        my_buf_append(&c->out, ": ", 2);
        my_buf_append(&c->out, vs, (size_t)vl);
        my_buf_append(&c->out, "\r\n", 2);
        Py_DECREF(k);
        Py_DECREF(v);
    }
    if (!has_server)
        my_buf_append(&c->out, "Server: vela/0.1\r\n", 18);
    if (!has_conn) {
        int keep = c->req.keep_alive && has_len;
        my_buf_append(&c->out, keep ? "Connection: keep-alive\r\n" : "Connection: close\r\n",
                      keep ? 24 : 19);
        c->close_after = !keep;
        c->keep_alive = keep;
    }
    my_buf_append(&c->out, "\r\n", 2);
    return 0;
}

static PyObject *make_scope(my_conn_t *c)
{
    PyObject *scope = PyDict_New();
    PyObject *tmp;
    tmp = PyUnicode_FromString("http");
    PyDict_SetItemString(scope, "type", tmp);
    Py_DECREF(tmp);
    PyObject *asgi = PyDict_New();
    tmp = PyUnicode_FromString("3.0");
    PyDict_SetItemString(asgi, "version", tmp);
    Py_DECREF(tmp);
    tmp = PyUnicode_FromString("2.3");
    PyDict_SetItemString(asgi, "spec_version", tmp);
    Py_DECREF(tmp);
    PyDict_SetItemString(scope, "asgi", asgi);
    Py_DECREF(asgi);
    char ver[8];
    snprintf(ver, sizeof ver, "%d.%d", c->req.http_major, c->req.http_minor);
    tmp = PyUnicode_FromString(ver);
    PyDict_SetItemString(scope, "http_version", tmp);
    Py_DECREF(tmp);
    tmp = PyUnicode_FromString(c->req.method);
    PyDict_SetItemString(scope, "method", tmp);
    Py_DECREF(tmp);
    tmp = PyUnicode_FromString("http");
    PyDict_SetItemString(scope, "scheme", tmp);
    Py_DECREF(tmp);

    const char *tgt = c->req.target ? c->req.target : "/";
    const char *q = strchr(tgt, '?');
    PyObject *path, *qs, *raw;
    if (q) {
        path = PyUnicode_DecodeUTF8(tgt, (Py_ssize_t)(q - tgt), "replace");
        qs = PyBytes_FromString(q + 1);
        raw = PyBytes_FromStringAndSize(tgt, (Py_ssize_t)(q - tgt));
    } else {
        path = PyUnicode_DecodeUTF8(tgt, (Py_ssize_t)strlen(tgt), "replace");
        qs = PyBytes_FromStringAndSize("", 0);
        raw = PyBytes_FromString(tgt);
    }
    PyDict_SetItemString(scope, "path", path);
    PyDict_SetItemString(scope, "raw_path", raw);
    PyDict_SetItemString(scope, "query_string", qs);
    Py_DECREF(path);
    Py_DECREF(raw);
    Py_DECREF(qs);
    tmp = PyUnicode_FromString("");
    PyDict_SetItemString(scope, "root_path", tmp);
    Py_DECREF(tmp);

    PyObject *hdrs = PyList_New(c->req.nheaders);
    for (int i = 0; i < c->req.nheaders; i++) {
        PyObject *k = PyBytes_FromStringAndSize(c->req.headers[i].name,
                                                (Py_ssize_t)c->req.headers[i].nlen);
        PyObject *v = PyBytes_FromStringAndSize(c->req.headers[i].value,
                                                (Py_ssize_t)c->req.headers[i].vlen);
        char *ks;
        Py_ssize_t kn;
        PyBytes_AsStringAndSize(k, &ks, &kn);
        for (Py_ssize_t j = 0; j < kn; j++)
            if (ks[j] >= 'A' && ks[j] <= 'Z')
                ks[j] = (char)(ks[j] - 'A' + 'a');
        PyObject *tup = PyTuple_Pack(2, k, v);
        PyList_SET_ITEM(hdrs, i, tup);
        Py_DECREF(k);
        Py_DECREF(v);
    }
    PyDict_SetItemString(scope, "headers", hdrs);
    Py_DECREF(hdrs);

    PyObject *client = PyTuple_New(2);
    char ip[INET6_ADDRSTRLEN] = "127.0.0.1";
    int port = 0;
    if (c->peer.ss_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&c->peer;
        inet_ntop(AF_INET, &in->sin_addr, ip, sizeof ip);
        port = ntohs(in->sin_port);
    }
    PyTuple_SET_ITEM(client, 0, PyUnicode_FromString(ip));
    PyTuple_SET_ITEM(client, 1, PyLong_FromLong(port));
    PyDict_SetItemString(scope, "client", client);
    Py_DECREF(client);

    PyObject *server = PyTuple_New(2);
    PyTuple_SET_ITEM(server, 0, PyUnicode_FromString("0.0.0.0"));
    PyTuple_SET_ITEM(server, 1, PyLong_FromLong(8000));
    PyDict_SetItemString(scope, "server", server);
    Py_DECREF(server);
    return scope;
}

int my_py_ready(void) { return g_ready && g_app; }

int my_py_init(const my_config_t *cfg)
{
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyStatus st = PyConfig_SetBytesString(&config, &config.program_name, "vela");
    if (PyStatus_Exception(st)) {
        PyConfig_Clear(&config);
        return -1;
    }
    st = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(st))
        return -1;

    if (cfg->python_path) {
        PyObject *sys = PyImport_ImportModule("sys");
        PyObject *path = PyObject_GetAttrString(sys, "path");
        char *dup = my_xstrdup(cfg->python_path);
        char *save = NULL;
        for (char *tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
            PyObject *s = PyUnicode_FromString(tok);
            PyList_Insert(path, 0, s);
            Py_DECREF(s);
        }
        free(dup);
        Py_DECREF(path);
        Py_DECREF(sys);
    }
    PyObject *sys = PyImport_ImportModule("sys");
    PyObject *path = PyObject_GetAttrString(sys, "path");
    PyObject *dot = PyUnicode_FromString(".");
    PyList_Insert(path, 0, dot);
    Py_DECREF(dot);
    Py_DECREF(path);
    Py_DECREF(sys);

    g_asyncio = PyImport_ImportModule("asyncio");
    if (!g_asyncio) {
        drain_pyerr("import asyncio");
        return -1;
    }
    g_loop = PyObject_CallMethod(g_asyncio, "new_event_loop", NULL);
    if (!g_loop) {
        drain_pyerr("new_event_loop");
        return -1;
    }
    PyObject *r = PyObject_CallMethod(g_asyncio, "set_event_loop", "O", g_loop);
    Py_XDECREF(r);

    PyObject *globals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
    PyObject *ok = PyRun_String(ASGI_RUNNER, Py_file_input, globals, globals);
    if (!ok) {
        drain_pyerr("asgi runner");
        Py_DECREF(globals);
        return -1;
    }
    Py_DECREF(ok);
    g_run = PyDict_GetItemString(globals, "vela_run");
    Py_XINCREF(g_run);
    Py_DECREF(globals);
    if (!g_run) {
        MY_ERROR("vela_run missing");
        return -1;
    }
    g_empty_bytes = PyBytes_FromStringAndSize("", 0);
    g_ready = 1;
    MY_INFO("CPython %s embedded", Py_GetVersion());
    return 0;
}

void my_py_fini(void)
{
    g_ready = 0;
    Py_XDECREF(g_app);
    Py_XDECREF(g_loop);
    Py_XDECREF(g_asyncio);
    Py_XDECREF(g_run);
    Py_XDECREF(g_empty_bytes);
    g_app = g_loop = g_asyncio = g_run = g_empty_bytes = NULL;
    if (Py_IsInitialized())
        Py_Finalize();
}

int my_py_load_app(const char *spec)
{
    if (!spec)
        return -1;
    char *dup = my_xstrdup(spec);
    char *colon = strrchr(dup, ':');
    if (!colon || colon == dup || !colon[1]) {
        MY_ERROR("ASGI target must be module:attribute, got '%s'", spec);
        free(dup);
        return -1;
    }
    *colon = 0;
    const char *modname = dup;
    const char *attr = colon + 1;
    PyObject *mod = PyImport_ImportModule(modname);
    if (!mod) {
        drain_pyerr("import app module");
        free(dup);
        return -1;
    }
    PyObject *obj = mod;
    Py_INCREF(obj);
    char *adup = my_xstrdup(attr);
    char *save = NULL;
    for (char *tok = strtok_r(adup, ".", &save); tok; tok = strtok_r(NULL, ".", &save)) {
        PyObject *next = PyObject_GetAttrString(obj, tok);
        Py_DECREF(obj);
        if (!next) {
            drain_pyerr("resolve app attribute");
            Py_DECREF(mod);
            free(adup);
            free(dup);
            return -1;
        }
        obj = next;
    }
    free(adup);
    if (!PyCallable_Check(obj)) {
        MY_ERROR("ASGI application is not callable");
        Py_DECREF(obj);
        Py_DECREF(mod);
        free(dup);
        return -1;
    }
    Py_XDECREF(g_app);
    g_app = obj;
    Py_DECREF(mod);
    MY_INFO("loaded ASGI application %s", spec);
    free(dup);
    return 0;
}

int my_py_handle_http(my_conn_t *c)
{
    if (!g_app || !g_loop || !g_run)
        return -1;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *scope = make_scope(c);
    PyObject *body;
    if (c->req.body_len == 0) {
        body = g_empty_bytes ? g_empty_bytes : PyBytes_FromStringAndSize("", 0);
        Py_INCREF(body);
    } else {
        body = PyBytes_FromStringAndSize(c->req.body, (Py_ssize_t)c->req.body_len);
    }
    PyObject *result = PyObject_CallFunctionObjArgs(g_run, g_app, g_loop, scope, body, NULL);
    Py_DECREF(scope);
    Py_DECREF(body);
    if (!result) {
        drain_pyerr("vela_run");
        PyGILState_Release(gstate);
        return -1;
    }
    PyObject *started = PyDict_GetItemString(result, "started");
    if (!started || !PyObject_IsTrue(started)) {
        Py_DECREF(result);
        PyGILState_Release(gstate);
        return -1;
    }
    PyObject *st = PyDict_GetItemString(result, "status");
    int status = st ? (int)PyLong_AsLong(st) : 200;
    PyObject *hdrs = PyDict_GetItemString(result, "headers");
    append_http_start(c, status, hdrs);
    PyObject *b = PyDict_GetItemString(result, "body");
    if (b) {
        char *p = NULL;
        Py_ssize_t n = 0;
        if (PyByteArray_Check(b)) {
            p = PyByteArray_AsString(b);
            n = PyByteArray_Size(b);
        } else if (PyBytes_Check(b)) {
            PyBytes_AsStringAndSize(b, &p, &n);
        }
        if (p && n > 0)
            my_buf_append(&c->out, p, (size_t)n);
    }
    Py_DECREF(result);
    PyGILState_Release(gstate);
    my_metrics_add_status(status);
    c->state = MY_CS_WRITE;
    return 0;
}
