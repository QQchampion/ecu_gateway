import json
import os
import select
import socket
import struct
import subprocess
import sys
import time


def frame(src, dst, kind, seq, payload):
    body = json.dumps({"src_id": src, "dst_id": dst, "msg_type": kind,
                       "seq": seq, "payload": payload}).encode()
    return struct.pack("!I", len(body)) + body


def connect():
    sock = socket.create_connection(("127.0.0.1", 5000), timeout=2)
    sock.settimeout(1)
    return sock


def receive(sock):
    def exact(size):
        result = b""
        while len(result) < size:
            chunk = sock.recv(size - len(result))
            assert chunk, "connection closed before full frame"
            result += chunk
        return result
    length = struct.unpack("!I", exact(4))[0]
    return json.loads(exact(length))


gateway = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
sockets = []
log = ""


def expect_log(text, count=1):
    global log
    deadline = time.monotonic() + 2
    while log.count(text) < count and time.monotonic() < deadline:
        ready, _, _ = select.select([gateway.stdout], [], [], 0.1)
        if ready:
            log += os.read(gateway.stdout.fileno(), 4096).decode()
    assert log.count(text) >= count, f"missing {text!r}: {log!r}"


try:
    expect_log("Gateway listening")
    power = connect(); sockets.append(power)
    power.sendall(frame(1, 4, 4, 1, {}))
    expect_log("Registered: Power ECU")
    payload = {"speed_kmh": 60.0, "rpm": 2000,
               "coolant_temp_c": 85.0, "engine_status": "RUNNING"}
    power.sendall(frame(1, 3, 1, 2, payload))
    time.sleep(0.1)
    assert gateway.poll() is None, "Gateway failed when target was unavailable"

    cluster = connect(); sockets.append(cluster)
    cluster.sendall(frame(3, 4, 4, 1, {}))
    expect_log("Registered: Cluster ECU")
    for seq in range(3, 8):
        power.sendall(frame(1, 3, 1, seq, payload))
    for seq in range(3, 8):
        assert receive(cluster) == {"src_id": 1, "dst_id": 3, "msg_type": 1,
                                    "seq": seq, "payload": payload}
    print("PASS: destination unavailable, full STATUS forwarding and FIFO")

    cluster.close()
    power.sendall(frame(1, 3, 1, 8, payload))
    time.sleep(0.1)
    power.sendall(frame(1, 3, 1, 9, payload))
    assert gateway.poll() is None, "Gateway failed after target disconnect"
    replacement = connect(); sockets.append(replacement)
    replacement.sendall(frame(3, 4, 4, 1, {}))
    expect_log("Registered: Cluster ECU", 2)
    power.sendall(frame(1, 3, 1, 10, payload))
    assert receive(replacement)["seq"] == 10
    print("PASS: target disconnect does not affect sender; target can re-register")
finally:
    for sock in sockets:
        sock.close()
    gateway.terminate()
    try:
        out, err = gateway.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        gateway.kill()
        out, err = gateway.communicate()
    print(log + out, end="")
    print(err, end="", file=sys.stderr)
