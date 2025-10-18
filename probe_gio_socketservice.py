import gi
import sys

gi.require_version("Gio", "2.0")
from gi.repository import Gio

_orig_new = Gio.SocketService.new

def _on_incoming(service, connection, source_object):
    try:
        socket = connection.get_socket()
        address = socket.get_remote_address()
    except Exception as err:
        address = err
    print(f"[probe] incoming connection: service={service!r} remote={address!r}", file=sys.stderr)
    return False

def new_wrapper(*args, **kwargs):
    print("[probe] Gio.SocketService.new invoked", file=sys.stderr)
    service = _orig_new(*args, **kwargs)
    print(f"[probe] service created: {service!r}", file=sys.stderr)
    service.connect("incoming", _on_incoming)
    return service

Gio.SocketService.new = staticmethod(new_wrapper)
print("[probe] Gio.SocketService.new hook installed", file=sys.stderr)
