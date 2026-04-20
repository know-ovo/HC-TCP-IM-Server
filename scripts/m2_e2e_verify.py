#!/usr/bin/env python3

import json
import os
import socket
import struct
import subprocess
import sys
import time


MAGIC = 0xABCD1234
VERSION = 1
HEADER_LEN = 44

CMD_LOGIN_REQ = 0x0001
CMD_LOGIN_RESP = 0x0002
CMD_P2P_REQ = 0x0005
CMD_P2P_RESP = 0x0006
CMD_P2P_NOTIFY = 0x0007
CMD_ACK_REQ = 0x000C
CMD_ACK_RESP = 0x000D
CMD_PULL_REQ = 0x000E
CMD_PULL_RESP = 0x000F

FLAG_REQUEST = 0x0001
FLAG_RESPONSE = 0x0002

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8888
MYSQL_HOST = "127.0.0.1"
MYSQL_PORT = 3306
MYSQL_USER = "root"
MYSQL_PASSWORD = "123456"
MYSQL_DB = "im_server"


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    polynomial = 0x1021
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ polynomial) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def pack_packet(command: int,
                request_id: int,
                payload: str,
                flags: int = FLAG_REQUEST,
                client_seq: int = 0,
                server_seq: int = 0,
                error_code: int = 0) -> bytes:
    body = payload.encode("utf-8")
    total_len = HEADER_LEN + len(body)
    checksum = crc16(body)
    header = struct.pack(
        "!IHHIHHIQQII",
        MAGIC,
        VERSION,
        HEADER_LEN,
        total_len,
        command,
        flags,
        request_id,
        client_seq,
        server_seq,
        error_code,
        checksum,
    )
    return header + body


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    received = 0
    while received < size:
        chunk = sock.recv(size - received)
        if not chunk:
            raise RuntimeError("socket closed unexpectedly")
        chunks.append(chunk)
        received += len(chunk)
    return b"".join(chunks)


def recv_packet(sock: socket.socket, timeout: float = 5.0) -> dict:
    sock.settimeout(timeout)
    header = recv_exact(sock, HEADER_LEN)
    magic, version, header_len, total_len, command, flags, request_id, client_seq, server_seq, error_code, checksum = struct.unpack(
        "!IHHIHHIQQII", header
    )
    if magic != MAGIC:
        raise RuntimeError(f"invalid magic: 0x{magic:08X}")
    payload_len = total_len - header_len
    payload = recv_exact(sock, payload_len).decode("utf-8") if payload_len > 0 else ""
    return {
        "command": command,
        "flags": flags,
        "request_id": request_id,
        "client_seq": client_seq,
        "server_seq": server_seq,
        "error_code": error_code,
        "payload": payload,
    }


def mysql_query(sql: str) -> str:
    env = os.environ.copy()
    env["MYSQL_PWD"] = MYSQL_PASSWORD
    result = subprocess.run(
        [
            "mysql",
            "-h",
            MYSQL_HOST,
            "-P",
            str(MYSQL_PORT),
            "-u",
            MYSQL_USER,
            "-D",
            MYSQL_DB,
            "-Nse",
            sql,
        ],
        capture_output=True,
        text=True,
        env=env,
        check=True,
    )
    return result.stdout.strip()


def reset_tables() -> None:
    mysql_query("DELETE FROM im_message_ack_log;")
    mysql_query("DELETE FROM im_message_delivery;")
    mysql_query("DELETE FROM im_message;")


class TestClient:
    def __init__(self, user_id: str):
        self.user_id = user_id
        self.sock = socket.create_connection((SERVER_HOST, SERVER_PORT), timeout=5)
        self.sock.settimeout(5)
        self.request_id = 1

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def send_json(self, command: int, payload_obj: dict, client_seq: int = None, server_seq: int = 0) -> int:
        request_id = self.request_id
        self.request_id += 1
        payload = json.dumps(payload_obj, separators=(",", ":"))
        seq = request_id if client_seq is None else client_seq
        packet = pack_packet(command, request_id, payload, FLAG_REQUEST, seq, server_seq, 0)
        self.sock.sendall(packet)
        return request_id

    def login(self) -> dict:
        req_id = self.send_json(
            CMD_LOGIN_REQ,
            {
                "user_id": self.user_id,
                "token": f"valid_token_{self.user_id}",
                "device_id": f"{self.user_id}_device",
            },
        )
        packet = recv_packet(self.sock)
        if packet["command"] != CMD_LOGIN_RESP or packet["request_id"] != req_id:
            raise RuntimeError(f"{self.user_id} login response mismatch: {packet}")
        payload = json.loads(packet["payload"])
        if payload["result_code"] != 0:
            raise RuntimeError(f"{self.user_id} login failed: {payload}")
        return payload

    def send_p2p(self, to_user_id: str, client_msg_id: str, content: str) -> dict:
        req_id = self.send_json(
            CMD_P2P_REQ,
            {
                "from_user_id": self.user_id,
                "to_user_id": to_user_id,
                "client_msg_id": client_msg_id,
                "content": content,
            },
        )
        packet = recv_packet(self.sock)
        if packet["command"] != CMD_P2P_RESP or packet["request_id"] != req_id:
            raise RuntimeError(f"{self.user_id} p2p response mismatch: {packet}")
        return json.loads(packet["payload"])

    def wait_for_notify(self, timeout: float = 5.0) -> dict:
        packet = recv_packet(self.sock, timeout=timeout)
        if packet["command"] != CMD_P2P_NOTIFY:
            raise RuntimeError(f"{self.user_id} expected P2P notify, got: {packet}")
        return json.loads(packet["payload"])

    def send_ack(self, msg_id: int, server_seq: int, ack_code: int = 0) -> dict:
        req_id = self.send_json(
            CMD_ACK_REQ,
            {
                "msg_id": msg_id,
                "server_seq": server_seq,
                "ack_code": ack_code,
            },
            client_seq=msg_id,
            server_seq=server_seq,
        )
        packet = recv_packet(self.sock)
        if packet["command"] != CMD_ACK_RESP or packet["request_id"] != req_id:
            raise RuntimeError(f"{self.user_id} ack response mismatch: {packet}")
        return json.loads(packet["payload"])

    def send_pull(self, last_acked_seq: int, limit: int = 100) -> dict:
        req_id = self.send_json(
            CMD_PULL_REQ,
            {
                "user_id": self.user_id,
                "last_acked_seq": last_acked_seq,
                "limit": limit,
            },
        )
        while True:
            packet = recv_packet(self.sock)
            if packet["command"] == CMD_PULL_RESP and packet["request_id"] == req_id:
                return json.loads(packet["payload"])


def assert_true(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def verify_online_delivery_and_ack() -> None:
    sender = TestClient("m2_user_sender")
    receiver = TestClient("m2_user_receiver")
    try:
        sender.login()
        receiver.login()

        send_resp = sender.send_p2p("m2_user_receiver", "m2-online-001", "hello_online_ack")
        assert_true(send_resp["result_code"] == 0, f"online send failed: {send_resp}")

        notify = receiver.wait_for_notify()
        assert_true(notify["client_msg_id"] == "m2-online-001", f"unexpected notify: {notify}")

        ack_resp = receiver.send_ack(notify["msg_id"], notify["server_seq"], 0)
        assert_true(ack_resp["result_code"] == 0, f"ack failed: {ack_resp}")

        message_row = mysql_query(
            "SELECT id, client_msg_id, from_user_id, to_user_id, content FROM im_message "
            "WHERE client_msg_id='m2-online-001' LIMIT 1;"
        )
        delivery_row = mysql_query(
            "SELECT message_id, receiver_user_id, server_seq, status, acked_at_ms "
            "FROM im_message_delivery WHERE message_id=(SELECT id FROM im_message WHERE client_msg_id='m2-online-001' LIMIT 1);"
        )
        ack_log_row = mysql_query(
            "SELECT message_id, receiver_user_id, ack_code FROM im_message_ack_log "
            "WHERE message_id=(SELECT id FROM im_message WHERE client_msg_id='m2-online-001' LIMIT 1) LIMIT 1;"
        )

        assert_true(bool(message_row), "online message not found in im_message")
        assert_true(bool(delivery_row), "online delivery not found in im_message_delivery")
        assert_true("\t3\t" in f"\t{delivery_row}\t" or delivery_row.endswith("\t3") or "\t3\t" in delivery_row,
                    f"delivery status is not DELIVERED: {delivery_row}")
        assert_true(bool(ack_log_row), "ack log not found")

        print("[PASS] 在线投递 + ACK 链路已验证")
        print("  im_message:", message_row)
        print("  im_message_delivery:", delivery_row)
        print("  im_message_ack_log:", ack_log_row)
    finally:
        sender.close()
        receiver.close()


def verify_offline_pull_and_recover() -> None:
    sender = TestClient("m2_offline_sender")
    receiver = None
    try:
        sender.login()
        send_resp = sender.send_p2p("m2_offline_receiver", "m2-offline-001", "hello_offline_pull")
        assert_true(send_resp["result_code"] == 0, f"offline send failed: {send_resp}")
        assert_true(send_resp["result_msg"] in ("persisted and queued offline", "persisted and delivered"),
                    f"unexpected offline send result: {send_resp}")

        receiver = TestClient("m2_offline_receiver")
        receiver.login()
        notify = receiver.wait_for_notify()
        assert_true(notify["client_msg_id"] == "m2-offline-001", f"unexpected offline notify: {notify}")

        pull_resp = receiver.send_pull(0, 10)
        assert_true(pull_resp["result_code"] == 0, f"pull failed: {pull_resp}")
        assert_true(len(pull_resp["messages"]) >= 1, f"pull returned no messages: {pull_resp}")

        offline_msg = pull_resp["messages"][0]
        assert_true(offline_msg["client_msg_id"] == "m2-offline-001", f"unexpected pull message: {offline_msg}")

        ack_resp = receiver.send_ack(offline_msg["msg_id"], offline_msg["server_seq"], 0)
        assert_true(ack_resp["result_code"] == 0, f"offline ack failed: {ack_resp}")

        delivery_row = mysql_query(
            "SELECT message_id, receiver_user_id, server_seq, status, acked_at_ms "
            "FROM im_message_delivery WHERE message_id=(SELECT id FROM im_message WHERE client_msg_id='m2-offline-001' LIMIT 1);"
        )
        assert_true("\t3\t" in f"\t{delivery_row}\t" or delivery_row.endswith("\t3") or "\t3\t" in delivery_row,
                    f"offline delivery status is not DELIVERED: {delivery_row}")

        print("[PASS] 离线补发 + 主动补拉 + ACK 链路已验证")
        print("  im_message_delivery:", delivery_row)
    finally:
        sender.close()
        if receiver:
            receiver.close()


def main() -> int:
    try:
        reset_tables()
        verify_online_delivery_and_ack()
        verify_offline_pull_and_recover()
        print("[PASS] M2 端到端验证完成")
        return 0
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
