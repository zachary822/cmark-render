/* Renders markdown with cmark-gfm under caller-chosen CMARK_OPT_* flags and
 * core syntax extensions. With neither, it is cmark-gfm's default: plain
 * CommonMark under CMARK_OPT_DEFAULT.
 *
 * Every allocation cmark hands back is freed here, so repeated renders don't
 * grow memory.
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pthread.h>
#include <string.h>

#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm.h"

/* Every flag that changes parsing or HTML output. CMARK_OPT_SAFE and
 * CMARK_OPT_NORMALIZE are left out: cmark-gfm keeps them as no-ops. */
#define KNOWN_OPTIONS                                                          \
  (CMARK_OPT_SOURCEPOS | CMARK_OPT_HARDBREAKS | CMARK_OPT_UNSAFE |             \
   CMARK_OPT_NOBREAKS | CMARK_OPT_VALIDATE_UTF8 | CMARK_OPT_SMART |            \
   CMARK_OPT_GITHUB_PRE_LANG | CMARK_OPT_LIBERAL_HTML_TAG |                    \
   CMARK_OPT_FOOTNOTES | CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE |                \
   CMARK_OPT_TABLE_PREFER_STYLE_ATTRIBUTES | CMARK_OPT_FULL_INFO_STRING)

/* cmark-gfm's core extensions, looked up by name when the module loads. */
#define N_EXTENSIONS 5
static const char *const extension_names[N_EXTENSIONS] = {
    "autolink", "strikethrough", "table", "tagfilter", "tasklist",
};
static cmark_syntax_extension *extensions[N_EXTENSIONS];

/* While parsing inlines, cmark adds each extension's characters ("~" for
 * strikethrough) to a global table and clears them after
 * (cmark_manage_extensions_special_characters), so a concurrent parse could
 * lose its strikethrough. Renders run without the GIL, and parses one at a
 * time. */
static pthread_mutex_t render_lock = PTHREAD_MUTEX_INITIALIZER;

/* A fork while another thread holds render_lock would leave the child with a
 * lock nothing releases. Forking waits for the render and unlocks in both. */
static void lock_render(void) { pthread_mutex_lock(&render_lock); }
static void unlock_render(void) { pthread_mutex_unlock(&render_lock); }

/* Fills chosen with the extensions named by names, in the caller's order and
 * each once, since cmark would run a twice-attached extension twice. Returns
 * how many, or -1 with an exception set. */
static int choose_extensions(PyObject *names,
                             cmark_syntax_extension *chosen[N_EXTENSIONS]) {
  /* A str is iterable too, but "table" would mean "t", "a", ... */
  if (PyUnicode_Check(names)) {
    PyErr_SetString(PyExc_TypeError,
                    "extensions must be an iterable of str, not a str");
    return -1;
  }
  PyObject *iterator = PyObject_GetIter(names);
  if (iterator == NULL) {
    return -1;
  }
  int count = 0;
  int attached[N_EXTENSIONS] = {0};
  PyObject *name;
  while ((name = PyIter_Next(iterator)) != NULL) {
    int found = -1;
    if (PyUnicode_Check(name)) {
      for (int i = 0; i < N_EXTENSIONS; i++) {
        if (PyUnicode_CompareWithASCIIString(name, extension_names[i]) == 0) {
          found = i;
          break;
        }
      }
    }
    if (found < 0) {
      PyErr_Format(PyExc_ValueError, "unknown cmark-gfm extension: %R", name);
      Py_DECREF(name);
      Py_DECREF(iterator);
      return -1;
    }
    Py_DECREF(name);
    if (!attached[found]) {
      attached[found] = 1;
      chosen[count++] = extensions[found];
    }
  }
  Py_DECREF(iterator);
  return PyErr_Occurred() ? -1 : count;
}

static PyObject *render_html(PyObject *module, PyObject *args,
                             PyObject *kwargs) {
  static char *keywords[] = {"", "options", "extensions", NULL};
  PyObject *arg;
  int options = CMARK_OPT_DEFAULT;
  PyObject *names = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "U|iO:render_html", keywords,
                                   &arg, &options, &names)) {
    return NULL;
  }
  if (options & ~KNOWN_OPTIONS) {
    PyErr_Format(PyExc_ValueError, "unknown cmark options: %#x",
                 (unsigned)(options & ~KNOWN_OPTIONS));
    return NULL;
  }
  cmark_syntax_extension *chosen[N_EXTENSIONS];
  int n_chosen = names == NULL ? 0 : choose_extensions(names, chosen);
  if (n_chosen < 0) {
    return NULL;
  }
  Py_ssize_t length;
  const char *text = PyUnicode_AsUTF8AndSize(arg, &length);
  if (text == NULL) {
    return NULL;
  }

  /* text points into the str argument, which the caller keeps alive for the
   * call. */
  char *html = NULL;
  Py_BEGIN_ALLOW_THREADS
  /* Taken without the GIL, so a thread waiting here can't block one that
   * holds the lock and needs the GIL back. */
  pthread_mutex_lock(&render_lock);
  cmark_parser *parser = cmark_parser_new(options);
  for (int i = 0; i < n_chosen; i++) {
    cmark_parser_attach_syntax_extension(parser, chosen[i]);
  }
  cmark_parser_feed(parser, text, (size_t)length);
  /* The parser hands the tree to the caller; cmark_parser_free leaves it. */
  cmark_node *document = cmark_parser_finish(parser);
  /* finish has cleared the special characters again, and rendering only
   * reads the tree and the extensions, so it runs alongside other parses. */
  pthread_mutex_unlock(&render_lock);
  if (document != NULL) {
    html = cmark_render_html(document, options,
                             cmark_parser_get_syntax_extensions(parser));
    cmark_node_free(document);
  }
  cmark_parser_free(parser);
  Py_END_ALLOW_THREADS

  if (html == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "cmark returned no document");
    return NULL;
  }
  PyObject *result = PyUnicode_DecodeUTF8(html, (Py_ssize_t)strlen(html), NULL);
  /* cmark_render_html allocates with the tree's allocator, which is the
   * default one for a parser from cmark_parser_new. */
  cmark_get_default_mem_allocator()->free(html);
  return result;
}

static int add_options(PyObject *module) {
#define ADD(name)                                                              \
  if (PyModule_AddIntConstant(module, #name, CMARK_OPT_##name) < 0) {          \
    return -1;                                                                 \
  }
  ADD(SOURCEPOS)
  ADD(HARDBREAKS)
  ADD(UNSAFE)
  ADD(NOBREAKS)
  ADD(VALIDATE_UTF8)
  ADD(SMART)
  ADD(GITHUB_PRE_LANG)
  ADD(LIBERAL_HTML_TAG)
  ADD(FOOTNOTES)
  ADD(STRIKETHROUGH_DOUBLE_TILDE)
  ADD(TABLE_PREFER_STYLE_ATTRIBUTES)
  ADD(FULL_INFO_STRING)
#undef ADD
  return 0;
}

static int exec_module(PyObject *module) {
  static int fork_handlers_registered = 0;
  if (!fork_handlers_registered) {
    if (pthread_atfork(lock_render, unlock_render, unlock_render) != 0) {
      PyErr_SetString(PyExc_ImportError, "pthread_atfork failed");
      return -1;
    }
    fork_handlers_registered = 1;
  }
  cmark_gfm_core_extensions_ensure_registered();
  for (int i = 0; i < N_EXTENSIONS; i++) {
    extensions[i] = cmark_find_syntax_extension(extension_names[i]);
    if (extensions[i] == NULL) {
      PyErr_Format(PyExc_ImportError, "cmark-gfm is missing the %s extension",
                   extension_names[i]);
      return -1;
    }
  }
  return add_options(module);
}

PyDoc_STRVAR(render_html_doc,
             "render_html($module, text, /, options=0, extensions=())\n--\n\n"
             "Render markdown to HTML. options is a bitwise OR of cmark-gfm's "
             "CMARK_OPT_* flags; extensions names core extensions to attach: "
             "autolink, strikethrough, table, tagfilter, tasklist.");

static PyMethodDef methods[] = {
    {"render_html", (PyCFunction)(void (*)(void))render_html,
     METH_VARARGS | METH_KEYWORDS, render_html_doc},
    {NULL, NULL, 0, NULL},
};

static PyModuleDef_Slot slots[] = {
    {Py_mod_exec, exec_module},
    {0, NULL},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "cmark_render._cmark",
    .m_doc = "Markdown rendering with cmark-gfm.",
    .m_size = 0,
    .m_methods = methods,
    .m_slots = slots,
};

PyMODINIT_FUNC PyInit__cmark(void) { return PyModuleDef_Init(&module_def); }
