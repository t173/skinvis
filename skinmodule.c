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
	struct skin skin;
} SkinObject;

struct skin skin_default = {
	.num_patches = 1,
	.num_cells = 16,
	.device = "/dev/ttyUSB0"
};

static struct PyModuleDef skin_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "skin",
	.m_doc = "Skin sensor prototype interface module",
	.m_size = -1
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
		NULL
	};
	struct skin stage = skin_default;
	if ( !PyArg_ParseTupleAndKeywords(args, kw, "|sii", kwlist, &stage.device, &stage.num_patches, &stage.num_cells) ) {
		return -1;
	}
	skin_init(&self->skin, stage.device, stage.num_patches, stage.num_cells);
	return 0;
}

static PyMemberDef Skin_members[] = {
	{ "device", T_STRING, offsetof(SkinObject, skin.device), 0, "interface device for skin sensor" },
	{ "patches", T_INT, offsetof(SkinObject, skin.num_patches), 0, "number of sensor patches" },
	{ "cells", T_INT, offsetof(SkinObject, skin.num_cells), 0, "number of cells per patch" },
	{ "total_bytes", T_LONGLONG, offsetof(SkinObject, skin.total_bytes), 0, "odometer of bytes read from device" },
	{ "total_records", T_LONGLONG, offsetof(SkinObject, skin.total_records), 0, "odometer of parsed records" },
	{ "dropped_records", T_LONGLONG, offsetof(SkinObject, skin.dropped_records), 0, "count of dropped records" },
	{ "misalignments", T_LONGLONG, offsetof(SkinObject, skin.misalignments), 0, "count of misalignment adjustments" },
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

static PyObject *
Skin_get_state(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	//DEBUGMSG("Skin_get_state()");
	skincell_t *state;
	const int num_patches = self->skin.num_patches;
	const int num_cells = self->skin.num_cells;
	const int count = num_patches*num_cells;
	ALLOCN(state, count);
	skin_get_state(&self->skin, state);
	PyObject* ret = PyList_New(num_patches);
	for ( int p=0; p<num_patches; p++ ) {
		PyObject* patch_list = PyList_New(num_cells);
		for ( int c=0; c<num_cells; c++ ) {
			PyObject* cell_value = Py_BuildValue("d", state[p*num_cells + c]);
			PyList_SetItem(patch_list, c, cell_value);
		}
		PyList_SetItem(ret, p, patch_list);
	}
	return ret;
}

static PyMethodDef Skin_methods[] = {
//	{ "get_device", (PyCFunction)Skin_get_device, METH_NOARGS, "gets the associated device" },
	{ "start", (PyCFunction)Skin_start, METH_NOARGS, "Starts reading from the skin sensor device" },
	{ "stop", (PyCFunction)Skin_stop, METH_NOARGS, "Stops reading from the skin sensor device" },
	{ "set_alpha", (PyCFunction)Skin_set_alpha, METH_VARARGS, "Sets alpha for exponential averaging" },
	{ "calibrate_start", (PyCFunction)Skin_calibrate_start, METH_NOARGS, "Start baseline calibration" },
	{ "calibrate_stop", (PyCFunction)Skin_calibrate_stop, METH_NOARGS, "Stop baseline calibration" },
	{ "get_calib", (PyCFunction)Skin_get_calibration, METH_VARARGS, "Gets a baseline calibration value" },
	{ "log", (PyCFunction)Skin_log, METH_VARARGS, "Logs stream to file" },
	{ "debuglog", (PyCFunction)Skin_debuglog, METH_VARARGS, "Logs debugging information to file" },
	{ "read_profile", (PyCFunction)Skin_read_profile, METH_VARARGS, "Read dynamic range calibration profile from CSV file" },
	{ "get_state", (PyCFunction)Skin_get_state, METH_NOARGS, "Gets current state of the entire octocan" },
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
