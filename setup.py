#!/usr/bin/env python3

from setuptools import setup, Extension

import numpy

DEBUG = True

skinmodule = Extension(
    'skin',
    sources=['skinmodule.c', 'skintalk.c', 'profile.c', 'layout.c'],
    libraries=['pthread'],
    include_dirs=[numpy.get_include()],
    extra_compile_args=['-DDEBUG'] if DEBUG else [],
    language='c11',
)

setup(
    name = 'skin',
    version = '0',
    description = 'Skin sensor prototype interface',
    ext_modules = [skinmodule]
)
