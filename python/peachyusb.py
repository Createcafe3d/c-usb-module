
import ctypes

class peachyusb_t(ctypes.Structure):
    pass

peachyusb_t_p = ctypes.POINTER(peachyusb_t)

peachyusb_read_callback = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_uint)

def _loadLibrary():
    try:
        dll = ctypes.CDLL("PeachyUSB.dll")
    except (OSError,), e:
        dll = ctypes.CDLL("./libPeachyUSB.so")
    dll.peachyusb_init.argtypes = [ctypes.c_uint]
    dll.peachyusb_init.restype = peachyusb_t_p
    
    dll.peachyusb_set_read_callback.argtypes = [peachyusb_t_p, peachyusb_read_callback]
    dll.peachyusb_set_read_callback.restype = None

    dll.peachyusb_write.argtypes = [peachyusb_t_p, ctypes.c_char_p, ctypes.c_uint]
    dll.peachyusb_write.restype = None
    return dll

lib = _loadLibrary()

class PeachyUSB(object):
    def __init__(self, capacity):
        self.context = lib.peachyusb_init(capacity)

    def __del__(self):
        print "Shutting down"
        lib.peachyusb_shutdown(self.context)
        self.context = None

    def write(self, buf):
        lib.peachyusb_write(self.context, buf, len(buf))
        
    def set_read_callback(self, func):
        callback = peachyusb_read_callback(func)
        lib.peachyusb_set_read_callback(self.context, callback)
        
