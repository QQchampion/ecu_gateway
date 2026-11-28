import json
import os
import select
import socket
import struct
import subprocess
import sys
import time


def send_message(sock, src, dst, kind, seq=1, payload=None):
    body = json.dumps({"src_id": src, "dst_id": dst, "msg_type": kind,
                       "seq": seq, "payload": {} if payload is None else payload}).encode()
    sock.sendall(struct.pack("!I", len(body)) + body)


def connect():
    sock = socket.create_connection(("127.0.0.1", 5000), timeout=2)
    sock.settimeout(0.3)
    return sock


def closed(sock):
    try:
        return sock.recv(1) == b""
    except ConnectionResetError:
        return True
    except socket.timeout:
        return False


def status(sock, src=1):
    send_message(sock, src, 3, 1, 2, {"speed_kmh": 60.0, "rpm": 2000,
                                     "coolant_temp_c": 85.0, "engine_status": "RUNNING"})


gateway = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
sockets = []
log = ""


def expect_log(message, count=1):
    global log
    deadline = time.monotonic() + 2
    while log.count(message) < count and time.monotonic() < deadline:
        ready, _, _ = select.select([gateway.stdout], [], [], 0.1)
        if ready:
            log += os.read(gateway.stdout.fileno(), 4096).decode()
    assert log.count(message) >= count, f"Gateway did not log {message!r} {count} times; got {log!r}"


try:
    for _ in range(50):
        try:
            probe = connect()
            probe.close()
            break
        except OSError:
            time.sleep(0.02)
    else:
        raise AssertionError("Gateway did not start")

    power = connect(); sockets.append(power)
    send_message(power, 1, 4, 4)
    expect_log("Registered: Power ECU")
    send_message(power, 1, 4, 4, 2)
    expect_log("Registered: Power ECU", 2)
    status(power)

    cluster = connect(); sockets.append(cluster)
    send_message(cluster, 3, 4, 4)
    expect_log("Registered: Cluster ECU")

    duplicate = connect(); sockets.append(duplicate)
    send_message(duplicate, 1, 4, 4)
    assert closed(duplicate), "duplicate identity accepted"

    unregistered = connect(); sockets.append(unregistered)
    status(unregistered)
    assert closed(unregistered), "unregistered STATUS accepted"

    forged = connect(); sockets.append(forged)
    send_message(forged, 5, 4, 4)
    status(forged, 1)
    assert closed(forged), "forged src_id accepted"

    status(power)
    assert not closed(power), "POWER_ECU disconnected after repeat heartbeat"
    assert not closed(cluster), "CLUSTER_ECU disconnected"
    power.close()
    time.sleep(0.1)
    replacement = connect(); sockets.append(replacement)
    send_message(replacement, 1, 4, 4)
    expect_log("Registered: Power ECU", 3)
    status(replacement)
    assert not closed(replacement), "re-registered POWER_ECU disconnected"
    print("PASS: registration, repeat heartbeat, duplicate, unregistered and forged source")
    if len(sys.argv) == 4:
        replacement.close()
        cluster.close()
        time.sleep(0.1)
        clients = [subprocess.Popen([path], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    text=True) for path in sys.argv[2:]]
        try:
            expect_log("Registered: Power ECU", 4)
            expect_log("Registered: Cluster ECU", 2)
            time.sleep(2.2)
            assert all(client.poll() is None for client in clients), "ECU client exited early"
            print("PASS: Power ECU and Cluster ECU remain connected")
        finally:
            for client in clients:
                client.terminate()
                try:
                    out, err = client.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    client.kill()
                    out, err = client.communicate()
                print(out, end="")
                print(err, end="", file=sys.stderr)
finally:
    for sock in sockets:
        sock.close()
    gateway.terminate()
    try:
        out, err = gateway.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        gateway.kill()
        out, err = gateway.communicate()
    print(out, end="")
    print(err, end="", file=sys.stderr)
