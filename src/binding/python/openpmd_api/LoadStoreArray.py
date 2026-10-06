"""
This file is part of the openPMD-api.

Copyright 2026 openPMD contributors
Authors: Franz Poeschel
License: LGPLv3+
"""

import numpy as np


def copy_into(source, target):
    """Copy the contents of ``source`` into the writable buffer ``target``.

    ``target`` is any writable PEP 3118 buffer (numpy array, memoryview,
    ``array.array``, ...). Its shape must match ``source``'s shape; a flat
    buffer covering the same number of elements is accepted as well. Returns
    ``target`` so the call can be chained.
    """
    src = np.asarray(source)
    dst = np.asarray(target)
    if dst.shape != src.shape:
        if dst.size != src.size:
            raise IndexError(
                "size of target buffer ("
                + str(dst.size)
                + ") does not match size of selection ("
                + str(src.size)
                + ")"
            )
        dst = dst.reshape(src.shape)
    np.copyto(dst, src)
    return target


class LoadStoreArray(np.ndarray):
    """An owning ``numpy.ndarray`` that additionally exposes ``.into()``.

    Returned by ``Record_Component.load_chunk()`` (and by ``Record_Component``
    slicing) when the Series is opened with immediate flushing, which is the
    default in the Python API. In that mode the data is loaded eagerly, but the
    result should still offer the same in-place ``.into(buffer)`` entry point as
    the deferred ``Load_Store_Chunk`` handle.

    Being an ``ndarray`` subclass, the object is fully transparent to numpy:
    arithmetic, ``.copy()``, reductions, comparisons and ``np.asarray()`` all
    behave as usual (and the subclass propagates through most operations).
    """

    def __new__(cls, array):
        return np.asarray(array).view(cls)

    def __array_finalize__(self, obj):
        # No extra state is carried; views/slices simply stay LoadStoreArrays
        # (or plain ndarrays, which is fine) with the standard ndarray behavior.
        pass

    def into(self, target):
        """Load this chunk's data into ``target``.

        In immediate-flushing mode the data has already been loaded, so this
        simply copies it into ``target`` (see :func:`copy_into`).
        """
        return copy_into(self, target)


def as_load_store_array(array):
    """Wrap a numpy array as a :class:`LoadStoreArray`.

    Falls back to the plain array (e.g. when the argument is already such a
    wrapper) so this can be applied unconditionally in the bindings.
    """
    if isinstance(array, LoadStoreArray):
        return array
    return LoadStoreArray(array)
