# The Python side of lib_python.of: loads the shared library given as the
# first argument with ctypes.
import ctypes
import sys


class Stats(ctypes.Structure):
    _fields_ = [("count", ctypes.c_int64), ("total", ctypes.c_int64)]


lib = ctypes.CDLL(sys.argv[1])
lib.stats_version.restype = ctypes.c_int32
lib.stats_init.argtypes = [ctypes.POINTER(Stats)]
lib.stats_add.argtypes = [ctypes.POINTER(Stats), ctypes.c_int64]
CALLBACK = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_int32)
lib.stats_fill.argtypes = [ctypes.POINTER(Stats), ctypes.c_int32, CALLBACK]
lib.stats_label.argtypes = [ctypes.POINTER(Stats), ctypes.c_char_p, ctypes.c_int64]

if lib.stats_version() != 1:
    sys.exit(f"unexpected version {lib.stats_version()}")
stats = Stats()
lib.stats_init(ctypes.byref(stats))
for value in (4, 5, 6):
    lib.stats_add(ctypes.byref(stats), value)
square = CALLBACK(lambda i: i * i)   # kept alive while the library calls it
lib.stats_fill(ctypes.byref(stats), 4, square)
print(f"Python sees {stats.count} values", flush=True)
name = "ação".encode()
lib.stats_label(ctypes.byref(stats), name, len(name))
lib.ofir_stdout_flush()
