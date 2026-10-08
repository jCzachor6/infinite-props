"""Observe actual DLL loads in a child through the Windows debug API.

The frozen executable is neither relinked nor injected. LOAD_DLL_DEBUG_EVENT
provides the loaded file handle; GetFinalPathNameByHandleW records its path.
Only imported on Windows. stdout/stderr are ordinary files, avoiding pipe
backpressure while the debugger must continue the process.
"""
import ctypes
from ctypes import wintypes as w
import subprocess
import time


# Win32 creation flag; the subprocess module does not export the debug flags.
DEBUG_ONLY_THIS_PROCESS = 0x00000002


def run_observed(command, *, cwd, env, stdout, stderr, timeout=90):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    pointer = ctypes.c_void_p

    class ExceptionRecord(ctypes.Structure):
        _fields_ = [("code", w.DWORD), ("flags", w.DWORD),
                    ("record", pointer), ("address", pointer),
                    ("count", w.DWORD), ("information", ctypes.c_size_t * 15)]

    class ExceptionInfo(ctypes.Structure):
        _fields_ = [("record", ExceptionRecord), ("first_chance", w.DWORD)]

    class CreateProcessInfo(ctypes.Structure):
        _fields_ = [("file", w.HANDLE), ("process", w.HANDLE), ("thread", w.HANDLE),
                    ("base", pointer), ("offset", w.DWORD), ("size", w.DWORD),
                    ("local_base", pointer), ("start", pointer),
                    ("image_name", pointer), ("unicode", w.WORD)]

    class LoadDllInfo(ctypes.Structure):
        _fields_ = [("file", w.HANDLE), ("base", pointer),
                    ("offset", w.DWORD), ("size", w.DWORD),
                    ("image_name", pointer), ("unicode", w.WORD)]

    class EventPayload(ctypes.Union):
        _fields_ = [("exception", ExceptionInfo), ("create", CreateProcessInfo),
                    ("thread", w.HANDLE), ("dll", LoadDllInfo), ("exit_code", w.DWORD)]

    class DebugEvent(ctypes.Structure):
        _fields_ = [("code", w.DWORD), ("pid", w.DWORD), ("tid", w.DWORD),
                    ("payload", EventPayload)]

    kernel.WaitForDebugEvent.argtypes = [ctypes.POINTER(DebugEvent), w.DWORD]
    kernel.WaitForDebugEvent.restype = w.BOOL
    kernel.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
    kernel.ContinueDebugEvent.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    kernel.GetFinalPathNameByHandleW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD]
    kernel.GetFinalPathNameByHandleW.restype = w.DWORD
    # These workflows support Windows x64; reject an accidental 32-bit observer.
    if ctypes.sizeof(pointer) != 8 or ctypes.sizeof(DebugEvent) != 176:
        raise RuntimeError("Windows loader witness requires the default x64 ABI")

    def file_path(handle):
        size = kernel.GetFinalPathNameByHandleW(handle, None, 0, 0)
        if not size:
            raise ctypes.WinError(ctypes.get_last_error())
        buffer = ctypes.create_unicode_buffer(size + 1)
        actual = kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 0)
        if not actual or actual >= len(buffer):
            raise ctypes.WinError(ctypes.get_last_error())
        value = buffer.value
        if value.startswith("\\\\?\\UNC\\"):
            return "\\\\" + value[8:]
        return value[4:] if value.startswith("\\\\?\\") else value

    loaded = []
    began = time.monotonic()
    with open(stdout, "wb") as out, open(stderr, "wb") as err:
        proc = subprocess.Popen([str(arg) for arg in command], cwd=cwd, env=env,
                                stdout=out, stderr=err,
                                creationflags=DEBUG_ONLY_THIS_PROCESS)
        try:
            while True:
                if time.monotonic() - began > timeout:
                    raise TimeoutError("frozen client loader observation timed out")
                event = DebugEvent()
                if not kernel.WaitForDebugEvent(ctypes.byref(event), 1000):
                    if ctypes.get_last_error() == 121:  # ERROR_SEM_TIMEOUT
                        continue
                    raise ctypes.WinError(ctypes.get_last_error())
                continuation = 0x00010002  # DBG_CONTINUE
                exited = event.code == 5  # EXIT_PROCESS_DEBUG_EVENT
                try:
                    # The debugger owns only the image file handles; the system
                    # closes process and thread handles as their exit events continue.
                    if event.code == 3:  # CREATE_PROCESS_DEBUG_EVENT
                        if event.payload.create.file:
                            kernel.CloseHandle(event.payload.create.file)
                    elif event.code == 6:  # LOAD_DLL_DEBUG_EVENT
                        handle = event.payload.dll.file
                        if handle:
                            try:
                                loaded.append(file_path(handle))
                            finally:
                                kernel.CloseHandle(handle)
                    elif event.code == 1:  # EXCEPTION_DEBUG_EVENT
                        # The loader breakpoint must continue. C++ exceptions and
                        # access violations must follow their normal handler path.
                        if event.payload.exception.record.code != 0x80000003:
                            continuation = 0x80010001  # DBG_EXCEPTION_NOT_HANDLED
                finally:
                    if not kernel.ContinueDebugEvent(event.pid, event.tid, continuation):
                        raise ctypes.WinError(ctypes.get_last_error())
                if exited:
                    break
            return proc.wait(timeout=5), loaded
        finally:
            if proc.poll() is None:
                proc.kill()
