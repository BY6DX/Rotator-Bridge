import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_TCP)
sock.connect(('192.168.3.136', 4196))

def set_preset(idx: int):
    cmd = bytearray(b'\xFF\x00\x00\x00\x00\x00\x00')
    cmd[3] = 0x03
    cmd[5] = idx
    cmd[6] = cmd[3] + cmd[5]
    sock.send(cmd)

def clear_preset(idx: int):
    cmd = bytearray(b'\xFF\x00\x00\x00\x00\x00\x00')
    cmd[3] = 0x05
    cmd[5] = idx
    cmd[6] = cmd[3] + cmd[5]
    sock.send(cmd)

def call_preset(idx: int):
    cmd = bytearray(b'\xFF\x00\x00\x00\x00\x00\x00')
    cmd[3] = 0x07
    cmd[5] = idx
    cmd[6] = cmd[3] + cmd[5]
    sock.send(cmd)

call_preset(156)