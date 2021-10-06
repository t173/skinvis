// skinmodule.c -*-C-*-
//
// Python interface for skin sensor prototypes

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include "util.h"
#include "skintalk.h"

typedef struct {
	PyObject_HEAD
	skin_t skin;
} SkinObject;

skin_t skin_default = {
	.num_patches = 1,
	.num_cells = 16,
	.device = "/dev/ttyUSB0",
	.history = 128
};

static struct PyModuleDef skin_module = {
	PyModuleDef_HEAD_INIT,
	.m_name="skin",
	.m_doc="Skin sensor prototype interface module",
	.m_size=-1,
};

//--------------------------------------------------------------------

static void
Skin_dealloc(SkinObject *self) {
	//Py_XDECREF(self->device);
	if ( self ) {
		skin_stop(&self->skin);
		skin_wait(&self->skin);
		skin_free(&self->skin);
	}
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
Skin_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
	DEBUGMSG("Skin_new()");
	SkinObject *self = (SkinObject *)type->tp_alloc(type, 0);
	memset(&self->skin, 0, sizeof(self->skin));
	return (PyObject *)self;
}

static int
Skin_init(SkinObject *self, PyObject *args, PyObject *kw) {
	DEBUGMSG("Skin_init()");
	static char *kwlist[] = {
		"device",
		"patches",
		"cells",
		"history",
		NULL
	};
	skin_t stage = skin_default;
	if ( !PyArg_ParseTupleAndKeywords(args, kw, "|siii", kwlist, &stage.device, &stage.num_patches, &stage.num_cells, &stage.history) ) {
		return -1;
	}
	skin_init(&self->skin, stage.num_patches, stage.num_cells, stage.device, stage.history);
	return 0;
}

static PyMemberDef Skin_members[] = {
	{ "device", T_STRING, offsetof(SkinObject, skin.device), 0, "interface device for skin sensor" },
	{ "patches", T_INT, offsetof(SkinObject, skin.num_patches), 0, "number of sensor patches" },
	{ "cells", T_INT, offsetof(SkinObject, skin.num_cells), 0, "number of cells per patch" },
	{ "history", T_INT, offsetof(SkinObject, skin.history), 0, "number of past cell values stored" },
	{ "total_bytes", T_LONGLONG, offsetof(SkinObject, skin.total_bytes), 0, "odometer of bytes read from device" },
	{ "total_records", T_LONGLONG, offsetof(SkinObject, skin.total_records), 0, "odometer of accepted records" },
	{ NULL }
};

static PyObject *
Skin_start(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	DEBUGMSG("Skin_start()");
	if ( self ) {
		 if ( !skin_start(&self->skin) ) {
			 DEBUGMSG("skin_start() failed");
		 }
	}
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_stop(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	DEBUGMSG("Skin_stop()");
	if ( self ) {
		 skin_stop(&self->skin);
	}
	Py_INCREF(Py_None);
	return Py_None;
}

/* static PyObject * */
/* Skin_get_device(SkinObject *self, PyObject *Py_UNUSED(ignored)) { */
/* 	DEBUGMSG("Skin_get_device()"); */
/* 	if ( self->skin.device == NULL ) { */
/* 		PyErr_SetString(PyExc_AttributeError, "device"); */
/* 		return NULL; */
/* 	} */
/* 	return PyUnicode_FromString(self->skin.device); */
/* } */

static PyObject *
Skin_get_history(SkinObject *self, PyObject *args) {
	int patch, cell;
	if ( !self || !PyArg_ParseTuple(args, "ii", &patch, &cell) ) {
		return NULL;
	}
	// Patch numbers start at 1
	if ( patch <= 0 || patch > self->skin.num_patches ) {
		PyErr_SetString(PyExc_ValueError, "patch number out of range");
		return NULL;
	}
	// Cell numbers start at 0
	if ( cell < 0 || cell >= self->skin.num_cells ) {
		PyErr_SetString(PyExc_ValueError, "cell number out of range");
		return NULL;
	}

	const int len = self->skin.history;
	int32_t *buf;
	if ( !(buf = malloc(len*sizeof(*buf))) ) {
		WARNING("Cannot allocate buffer");
		return NULL;
	}
	skin_get_history(&self->skin, buf, patch, cell);
	npy_intp dim[1] = { len };
	PyArrayObject *array = (PyArrayObject *)PyArray_SimpleNewFromData(1, dim, NPY_INT32, buf);
	PyArray_ENABLEFLAGS(array, NPY_ARRAY_OWNDATA);
	return (PyObject *)array;
}

static PyObject *
Skin_get_expavg(SkinObject *self, PyObject *args) {
	int patch, cell;
	if ( !self || !PyArg_ParseTuple(args, "ii", &patch, &cell) ) {
		return NULL;
	}
	// Patch numbers start at 1
	if ( patch <= 0 || patch > self->skin.num_patches ) {
		PyErr_SetString(PyExc_ValueError, "patch number out of range");
		return NULL;
	}
	// Cell numbers start at 0
	if ( cell < 0 || cell >= self->skin.num_cells ) {
		PyErr_SetString(PyExc_ValueError, "cell number out of range");
		return NULL;
	}
	return PyFloat_FromDouble(skin_get_expavg(&self->skin, patch, cell));
}

static PyObject *
Skin_set_alpha(SkinObject *self, PyObject *args) {
	DEBUGMSG("Skin_set_alpha()");
	double alpha;
	if ( !self || !PyArg_ParseTuple(args, "d", &alpha) ) {
		return NULL;
	}
	if ( !skin_set_alpha(&self->skin, alpha) ) {
		PyErr_SetString(PyExc_ValueError, "Invalid alpha value (0, 1]");
		return NULL;
	}
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_calibrate_start(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	DEBUGMSG("Skin_calibrate_start()");
	skin_calibrate_start(&self->skin);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_calibrate_stop(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	DEBUGMSG("Skin_calibrate_stop()");
	skin_calibrate_stop(&self->skin);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_get_calibration(SkinObject *self, PyObject *args) {
	int patch, cell;
	if ( !self || !PyArg_ParseTuple(args, "ii", &patch, &cell) ) {
		return NULL;
	}
	// Patch numbers start at 1
	if ( patch <= 0 || patch > self->skin.num_patches ) {
		PyErr_SetString(PyExc_ValueError, "patch number out of range");
		return NULL;
	}
	// Cell numbers start at 0
	if ( cell < 0 || cell >= self->skin.num_cells ) {
		PyErr_SetString(PyExc_ValueError, "cell number out of range");
		return NULL;
	}
	return PyLong_FromLong((long)skin_get_calibration(&self->skin, patch, cell));
}

static PyObject *
Skin_log(SkinObject *self, PyObject *args) {
	DEBUGMSG("Skin_log()");
	char *filename;
	if ( !self || !PyArg_ParseTuple(args, "s", &filename) ) {
		WARNING("Skin_log() could not parse argument");
		return NULL;
	}
	skin_log_stream(&self->skin, filename);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_debuglog(SkinObject *self, PyObject *args) {
	DEBUGMSG("Skin_debuglog()");
	char *filename;
	if ( !self || !PyArg_ParseTuple(args, "s", &filename) ) {
		WARNING("Skin_debuglog() could not parse argument");
		return NULL;
	}
	skin_debuglog_stream(&self->skin, filename);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
Skin_read_profile(SkinObject *self, PyObject *args) {
	DEBUGMSG("Skin_read_profile()");
	char *filename;
	if ( !self || !PyArg_ParseTuple(args, "s", &filename) ) {
		WARNING("Skin_read_profile() could not parse argument");
		return NULL;
	}
	skin_read_profile(&self->skin, filename);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef Skin_methods[] = {
//	{ "get_device", (PyCFunction)Skin_get_device, METH_NOARGS, "gets the associated device" },
	{ "start", (PyCFunction)Skin_start, METH_NOARGS, "Starts reading from the skin sensor device" },
	{ "stop", (PyCFunction)Skin_stop, METH_NOARGS, "Stops reading from the skin sensor device" },
	{ "get_history", (PyCFunction)Skin_get_history, METH_VARARGS, "Copies cell history" },
	{ "get_expavg", (PyCFunction)Skin_get_expavg, METH_VARARGS, "Get exponential average of cell" },
	{ "set_alpha", (PyCFunction)Skin_set_alpha, METH_VARARGS, "Sets alpha for exponential averaging" },
	{ "calibrate_start", (PyCFunction)Skin_calibrate_start, METH_NOARGS, "Sets alpha for exponential averaging" },
	{ "calibrate_stop", (PyCFunction)Skin_calibrate_stop, METH_NOARGS, "Sets alpha for exponential averaging" },
	{ "get_calib", (PyCFunction)Skin_get_calibration, METH_VARARGS, "Gets a baseline calibration value" },
	{ "log", (PyCFunction)Skin_log, METH_VARARGS, "Logs stream to file" },
	{ "debuglog", (PyCFunction)Skin_debuglog, METH_VARARGS, "Logs debugging information to file" },
	{ "read_profile", (PyCFunction)Skin_read_profile, METH_VARARGS, "Read dynamic range calibration profile from CSV file" },
	{ NULL }
};

static PyTypeObject SkinType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "skin.Skin",
	.tp_doc = "Skin sensor interface object",
	.tp_basicsize = sizeof(SkinObject),
	.tp_itemsize = 0,
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_new = Skin_new,
	.tp_init = (initproc)Skin_init,
	.tp_dealloc = (destructor)Skin_dealloc,
	.tp_members = Skin_members,
	.tp_methods = Skin_methods
};

//--------------------------------------------------------------------

PyMODINIT_FUNC
PyInit_skin(void)
{
	if ( PyType_Ready(&SkinType) < 0 ) {
		return NULL;
	}
	PyObject *module = PyModule_Create(&skin_module);
	if ( module == NULL ) {
		return NULL;
	}
	Py_INCREF(&SkinType);
	if ( PyModule_AddObject(module, "Skin", (PyObject *)&SkinType) < 0 ) {
		Py_DECREF(&SkinType);
		Py_DECREF(module);
		return NULL;
	}
	import_array();
	return module;
}

//EOF
