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
	.total_cells = 16,
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
		"layout",
		NULL
	};
	const char *device;
	const char *layout;
	if ( !PyArg_ParseTupleAndKeywords(args, kw, "|ss", kwlist, &device, &layout) ) {
		return -1;
	}
	if ( !skin_from_layout(&self->skin, device, layout) ) {
		return -1;
	}
	return 0;
}

static PyMemberDef Skin_members[] = {
	{ "device", T_STRING, offsetof(SkinObject, skin.device), 0, "interface device for skin sensor" },
	{ "patches", T_INT, offsetof(SkinObject, skin.num_patches), 0, "number of sensor patches" },
	{ "total_cells", T_INT, offsetof(SkinObject, skin.total_cells), 0, "total number of cells on device" },
	{ "total_bytes", T_LONGLONG, offsetof(SkinObject, skin.total_bytes), 0, "odometer of bytes read from device" },
	{ "total_records", T_LONGLONG, offsetof(SkinObject, skin.addr_tally[ADDR_VALID]), 0, "odometer of correctly parsed records" },
	//{ "dropped_records", T_LONGLONG, offsetof(SkinObject, skin.dropped_records), 0, "count of dropped records" },
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
Skin_set_pressure_alpha(SkinObject *self, PyObject *args) {
	DEBUGMSG("Skin_set_pressure_alpha()");
	double alpha;
	if ( !self || !PyArg_ParseTuple(args, "d", &alpha) ) {
		return NULL;
	}
	if ( !skin_set_pressure_alpha(&self->skin, alpha) ) {
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
	enum addr_check chk = address_check(&self->skin, patch, cell);
	switch ( chk ) {
	case ADDR_PATCH_OOR:
		PyErr_Format(PyExc_ValueError, "Patch number %d is out of range", patch);
		return NULL;
	case ADDR_PATCH_INV:
		PyErr_Format(PyExc_ValueError, "Patch number %d is invalid", patch);
		return NULL;
	case ADDR_CELL_OOR:
		PyErr_Format(PyExc_ValueError, "Cell number %d for patch %d is out of range", cell, patch);
		return NULL;
	case ADDR_CELL_INV:
		PyErr_Format(PyExc_ValueError, "Cell number %d for patch %d is invalid", cell, patch);
		return NULL;
	case ADDR_VALID:
	default:
		return PyLong_FromLong((long)skin_get_calibration(&self->skin, patch, cell));
	}
	return NULL;
}

static PyObject *
Skin_get_calib_patch(SkinObject *self, PyObject *args) {
	int patch;
	if ( !self || !PyArg_ParseTuple(args, "i", &patch) ) {
		return NULL;
	}
	enum addr_check chk = address_check(&self->skin, patch, 0);
	if ( chk == ADDR_PATCH_OOR ) {
		PyErr_Format(PyExc_ValueError, "Patch number %d is out of range", patch);
		return NULL;
	} else if ( chk == ADDR_PATCH_INV ) {
		PyErr_Format(PyExc_ValueError, "Patch number %d is invalid", patch);
		return NULL;
	}

	const struct layout *lo = &self->skin.layout;
	const struct patch_layout *pl = &lo->patch[lo->patch_idx[patch]];
	const struct patch_profile *pp = skin_get_patch_profile(&self->skin, patch);
	
	PyObject *ret = PyList_New(pl->num_cells);
	for ( int c=0; c < pl->num_cells; c++ ) {
		int cell_id = pl->cell_id[c];
		PyList_SetItem(ret, c, PyLong_FromLong((long)pp_baseline(pp, cell_id)));
	}
	return ret;
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
Skin_get_patch_profile(SkinObject *self, PyObject *args) {
	int patch;
	if ( !self || !PyArg_ParseTuple(args, "i", &patch) ) {
		WARNING("Skin_get_patch_profile() could not parse argument");
		return NULL;
	}
	struct patch_profile *prof = skin_get_patch_profile(&self->skin, patch);
	const int num_cells = prof->num_cells;

	PyObject *ret = PyDict_New();
	PyDict_SetItemString(ret, "id", PyLong_FromLong((long)prof->patch_id));

	PyObject *baseline = PyList_New(num_cells);
	PyObject *c0 = PyList_New(num_cells);
	PyObject *c1 = PyList_New(num_cells);
	PyObject *c2 = PyList_New(num_cells);
	for ( int c=0; c<num_cells; c++ ) {
		PyList_SetItem(baseline, c, Py_BuildValue("d", prof->baseline[c]));
		PyList_SetItem(c0, c, Py_BuildValue("d", prof->c0[c]));
		PyList_SetItem(c1, c, Py_BuildValue("d", prof->c1[c]));
		PyList_SetItem(c2, c, Py_BuildValue("d", prof->c2[c]));
	}
	PyDict_SetItemString(ret, "baseline", baseline);
	PyDict_SetItemString(ret, "c0", c0);
	PyDict_SetItemString(ret, "c1", c1);
	PyDict_SetItemString(ret, "c2", c2);
	return ret;
}

static PyObject *
Skin_get_cell_ids(SkinObject *self, PyObject *args) {
	int patch;
	if ( !self || !PyArg_ParseTuple(args, "i", &patch) ) {
		WARNING("Skin_get_cell_ids() could not parse argument");
		return NULL;
	}
	const struct patch_layout *pl = &self->skin.layout.patch[self->skin.layout.patch_idx[patch]];
	const int num_cells = pl->num_cells;
	PyObject *ret = PyList_New(num_cells);
	for ( int i=0; i < num_cells; i++ ) {
		PyList_SetItem(ret, i, PyLong_FromLong((long)pl->cell_id[i]));
	}
	return ret;
}

/* static PyObject * */
/* Skin_get_state(SkinObject *self, PyObject *Py_UNUSED(ignored)) { */
/* 	//DEBUGMSG("Skin_get_state()"); */
/* 	skincell_t *state; */
/* 	const int num_patches = self->skin.num_patches; */
/* 	const int num_cells = self->skin.num_cells; */
/* 	const int count = num_patches*num_cells; */
/* 	ALLOCN(state, count); */
/* 	skin_get_state(&self->skin, state); */
/* 	PyObject *ret = PyList_New(num_patches); */
/* 	for ( int p=0; p<num_patches; p++ ) { */
/* 		PyObject *patch_list = PyList_New(num_cells); */
/* 		for ( int c=0; c<num_cells; c++ ) { */
/* 			PyObject *cell_value = Py_BuildValue("d", state[p*num_cells + c]); */
/* 			PyList_SetItem(patch_list, c, cell_value); */
/* 		} */
/* 		PyList_SetItem(ret, p, patch_list); */
/* 	} */
/* 	free(state); */
/* 	return ret; */
/* } */

static PyObject *
Skin_get_patch_state(SkinObject *self, PyObject *args) {
	int patch;
	if ( !self || !PyArg_ParseTuple(args, "i", &patch) ) {
		WARNING("Skin_get_patch_state() could not parse argument");
		return NULL;
	}

	const int num_cells = self->skin.layout.patch[self->skin.layout.patch_idx[patch]].num_cells;
	skincell_t *state;
	ALLOCN(state, num_cells);
	skin_get_patch_state(&self->skin, patch, state);
	PyObject *ret = PyList_New(num_cells);
	for ( int c=0; c<num_cells; c++ ) {
		PyList_SetItem(ret, c, Py_BuildValue("d", state[c]));
	}
	free(state);
	return ret;
}

static PyObject *
Skin_get_patch_pressure(SkinObject *self, PyObject *args) {
	//DEBUGMSG("Skin_get_patch_pressure()");
	int patch;
	struct skin_pressure pressure;
	if ( !self || !PyArg_ParseTuple(args, "i", &patch) ) {
		WARNING("Skin_get_patch_pressure() could not parse argument");
		return NULL;
	}
	skin_get_patch_pressure(&self->skin, patch, &pressure);
	PyObject *ret = PyList_New(3);
	PyList_SetItem(ret, 0, Py_BuildValue("d", pressure.magnitude));
	PyList_SetItem(ret, 1, Py_BuildValue("d", pressure.x));
	PyList_SetItem(ret, 2, Py_BuildValue("d", pressure.y));
	return ret;
}

static PyObject *
Skin_get_layout(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	struct layout *lo = &self->skin.layout;
	const int num_patches = lo->num_patches;

	PyObject *ret = PyDict_New();
	for ( int p=0; p < num_patches; p++ ) {
		struct patch_layout *pl = &lo->patch[p];

		PyObject *cells = PyDict_New();
		for ( int c=0; c < pl->num_cells; c++ ) {
			PyObject *id = PyLong_FromLong(pl->cell_id[c]);
			PyObject *pos = PyTuple_Pack(2, Py_BuildValue("d", pl->x[c]), Py_BuildValue("d", pl->y[c]));
			PyDict_SetItem(cells, id, pos);
		}
		PyDict_SetItem(ret, PyLong_FromLong((long)pl->patch_id), cells);
	}
	return ret;
}

static PyObject *
Skin_get_record_tally(SkinObject *self, PyObject *Py_UNUSED(ignored)) {
	PyObject *ret = PyDict_New();
	PyDict_SetItemString(ret, "valid", PyLong_FromLong((long)self->skin.addr_tally[ADDR_VALID]));
	PyDict_SetItemString(ret, "patch_outofrange", PyLong_FromLong((long)self->skin.addr_tally[ADDR_PATCH_OOR]));
	PyDict_SetItemString(ret, "invalid_patch", PyLong_FromLong((long)self->skin.addr_tally[ADDR_PATCH_INV]));
	PyDict_SetItemString(ret, "cell_outofrange", PyLong_FromLong((long)self->skin.addr_tally[ADDR_CELL_OOR]));
	PyDict_SetItemString(ret, "invalid_cell", PyLong_FromLong((long)self->skin.addr_tally[ADDR_CELL_INV]));
	return ret;
}

static PyMethodDef Skin_methods[] = {
//	{ "get_device", (PyCFunction)Skin_get_device, METH_NOARGS, "gets the associated device" },
	{ "start", (PyCFunction)Skin_start, METH_NOARGS, "Starts reading from the skin sensor device" },
	{ "stop", (PyCFunction)Skin_stop, METH_NOARGS, "Stops reading from the skin sensor device" },
	{ "set_alpha", (PyCFunction)Skin_set_alpha, METH_VARARGS, "Sets alpha for exponential averaging" },
	{ "set_pressure_alpha", (PyCFunction)Skin_set_pressure_alpha, METH_VARARGS, "Sets alpha for pressure smoothing" },
	{ "calibrate_start", (PyCFunction)Skin_calibrate_start, METH_NOARGS, "Start baseline calibration" },
	{ "calibrate_stop", (PyCFunction)Skin_calibrate_stop, METH_NOARGS, "Stop baseline calibration" },
	{ "get_calib", (PyCFunction)Skin_get_calibration, METH_VARARGS, "Gets a baseline calibration value" },
	{ "get_calib_patch", (PyCFunction)Skin_get_calib_patch, METH_VARARGS, "Gets all baseline calibration values for a single patch (order of get_cell_ids)" },
	{ "log", (PyCFunction)Skin_log, METH_VARARGS, "Logs stream to file" },
	{ "debuglog", (PyCFunction)Skin_debuglog, METH_VARARGS, "Logs debugging information to file" },
	{ "read_profile", (PyCFunction)Skin_read_profile, METH_VARARGS, "Read dynamic range calibration profile from CSV file" },
	{ "get_patch_profile", (PyCFunction)Skin_get_patch_profile, METH_VARARGS, "Gets calibration settings for a specific patch" },
	//{ "get_state", (PyCFunction)Skin_get_state, METH_NOARGS, "Gets current state of all patches" },
	{ "get_patch_state", (PyCFunction)Skin_get_patch_state, METH_VARARGS, "Gets current state of a specific patch" },
	{ "get_cell_ids", (PyCFunction)Skin_get_cell_ids, METH_VARARGS, "Gets cell ID numbers in a common order as other reporting methods (get_patch_state, etc.)" },
	{ "get_patch_pressure", (PyCFunction)Skin_get_patch_pressure, METH_VARARGS, "Gets pressure for a single patch" },
	{ "get_layout", (PyCFunction)Skin_get_layout, METH_NOARGS, "Gets skin device layout of patches and cells" },
	{ "get_record_tally", (PyCFunction)Skin_get_record_tally, METH_NOARGS, "Gets tallies of valid and invalid records, based on error" },
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
