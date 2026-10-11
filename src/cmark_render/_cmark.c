/* Renders markdown with cmark-gfm: CommonMark plus the
 * strikethrough and table extensions, raw HTML passed through
 * (CMARK_OPT_UNSAFE), and strikethrough only on "~~".
 *
 * Every allocation cmark hands back is freed here. The cmarkgfm package
 * leaked the syntax tree and the rendered HTML on each call.
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pthread.h>
#include <string.h>

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"

#define RENDER_OPTIONS (CMARK_OPT_UNSAFE | CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE)

static cmark_syntax_extension *strikethrough;
static cmark_syntax_extension *table;

/* While parsing inlines, cmark adds each extension's characters ("~" for
 * strikethrough) to a global table and clears them after
 * (cmark_manage_extensions_special_characters), so a concurrent parse could
 * lose its strikethrough. Renders run without the GIL, one at a time. */
static pthread_mutex_t render_lock = PTHREAD_MUTEX_INITIALIZER;

/* A fork while another thread holds render_lock would leave the child with a
 * lock nothing releases. Forking waits for the render and unlocks in both. */
static void lock_render(void) { pthread_mutex_lock(&render_lock); }
static void unlock_render(void) { pthread_mutex_unlock(&render_lock); }

static PyObject *render_html(PyObject *module, PyObject *arg) {
  if (!PyUnicode_Check(arg)) {
    PyErr_Format(PyExc_TypeError,
                 "render_html() argument must be str, not %.200s",
                 Py_TYPE(arg)->tp_name);
    return NULL;
  }
  Py_ssize_t length;
  const char *text = PyUnicode_AsUTF8AndSize(arg, &length);
  if (text == NULL) {
    return NULL;
  }

  /* text points into arg, which the caller keeps alive for the call. */
  char *html = NULL;
  Py_BEGIN_ALLOW_THREADS
  /* Taken without the GIL, so a thread waiting here can't block one that
   * holds the lock and needs the GIL back. */
  pthread_mutex_lock(&render_lock);
  cmark_parser *parser = cmark_parser_new(RENDER_OPTIONS);
  cmark_parser_attach_syntax_extension(parser, strikethrough);
  cmark_parser_attach_syntax_extension(parser, table);
  cmark_parser_feed(parser, text, (size_t)length);
  /* The parser hands the tree to the caller; cmark_parser_free leaves it. */
  cmark_node *document = cmark_parser_finish(parser);
  if (document != NULL) {
    html = cmark_render_html(document, RENDER_OPTIONS,
                             cmark_parser_get_syntax_extensions(parser));
    cmark_node_free(document);
  }
  cmark_parser_free(parser);
  pthread_mutex_unlock(&render_lock);
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
  strikethrough = cmark_find_syntax_extension("strikethrough");
  table = cmark_find_syntax_extension("table");
  if (strikethrough == NULL || table == NULL) {
    PyErr_SetString(PyExc_ImportError,
                    "cmark-gfm is missing the strikethrough or table extension");
    return -1;
  }
  return 0;
}

PyDoc_STRVAR(render_html_doc,
             "render_html($module, text, /)\n--\n\n"
             "Render markdown to HTML with the strikethrough and table "
             "extensions, passing raw HTML through.");

static PyMethodDef methods[] = {
    {"render_html", render_html, METH_O, render_html_doc},
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
