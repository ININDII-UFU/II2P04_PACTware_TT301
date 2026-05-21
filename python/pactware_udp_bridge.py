import argparse
import configparser
import socket
import sys
import threading
import time
from pathlib import Path

import serial


DEFAULT_ESP_UDP_PORT = 47268
DEFAULT_BAUDRATE = 1200
READ_SIZE = 256


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_kit_id(root: Path) -> str:
    config = configparser.ConfigParser()
    ini_path = root / "platformio.ini"
    if not config.read(ini_path, encoding="utf-8"):
        raise FileNotFoundError(f"Nao consegui ler {ini_path}")
    return config.get("data", "kitId", fallback="0").strip()


def local_ip_for_remote(host: str, port: int) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((host, port))
        return probe.getsockname()[0]
    finally:
        probe.close()


def hex_dump(data: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def open_serial(port: str, baudrate: int) -> serial.Serial:
    return serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_ODD,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.05,
        write_timeout=1,
    )


def udp_to_serial(sock: socket.socket, ser: serial.Serial, stop: threading.Event, verbose: bool) -> None:
    while not stop.is_set():
        try:
            data, _addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        except OSError:
            break

        if data.startswith(b"CONNECT:") or data.startswith(b"DISCONNECT:"):
            if verbose:
                print(data.decode(errors="replace").strip())
            continue

        if data:
            ser.write(data)
            if verbose:
                print(f"UDP -> SERIAL ({len(data)}): {hex_dump(data)}")


def serial_to_udp(
    sock: socket.socket,
    ser: serial.Serial,
    esp_addr: tuple[str, int],
    stop: threading.Event,
    verbose: bool,
) -> None:
    while not stop.is_set():
        try:
            data = ser.read(READ_SIZE)
        except serial.SerialException as exc:
            print(f"Erro na serial: {exc}")
            stop.set()
            break

        if data:
            sock.sendto(data, esp_addr)
            if verbose:
                print(f"SERIAL -> UDP ({len(data)}): {hex_dump(data)}")


def parse_args() -> argparse.Namespace:
    root = project_root()
    kit_id = read_kit_id(root)
    default_serial = f"CNCA{kit_id}"
    default_host = f"iikit{kit_id}.local"

    parser = argparse.ArgumentParser(
        description="Ponte HART entre a porta serial virtual do PACTware e o UDP do ESP32."
    )
    parser.add_argument("--serial", default=default_serial, help=f"Porta serial virtual. Padrao: {default_serial}")
    parser.add_argument("--host", default=default_host, help=f"Host/IP do ESP32. Padrao: {default_host}")
    parser.add_argument("--udp-port", type=int, default=DEFAULT_ESP_UDP_PORT, help="Porta UDP do ESP32.")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE, help="Baudrate serial.")
    parser.add_argument("--listen-port", type=int, default=0, help="Porta UDP local. 0 escolhe automaticamente.")
    parser.add_argument("-v", "--verbose", action="store_true", help="Mostra bytes trafegando em hexadecimal.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    esp_addr = (args.host, args.udp_port)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.1)
    sock.bind(("", args.listen_port))

    local_ip = local_ip_for_remote(args.host, args.udp_port)
    local_port = sock.getsockname()[1]
    connect_msg = f"CONNECT:{local_ip}:{local_port}".encode("ascii")

    try:
        ser = open_serial(args.serial, args.baudrate)
    except serial.SerialException as exc:
        print(f"Nao consegui abrir {args.serial} em 1200 SERIAL_8O1: {exc}")
        print("Confira se a porta virtual existe e se o PACTware esta usando a outra ponta do par.")
        return 1

    stop = threading.Event()
    rx_thread = threading.Thread(target=udp_to_serial, args=(sock, ser, stop, args.verbose), daemon=True)
    tx_thread = threading.Thread(target=serial_to_udp, args=(sock, ser, esp_addr, stop, args.verbose), daemon=True)

    print(f"Serial aberta: {args.serial} @ {args.baudrate} SERIAL_8O1")
    print(f"UDP local: {local_ip}:{local_port}")
    print(f"ESP32: {args.host}:{args.udp_port}")
    print("Ponte ativa. Pressione Ctrl+C para sair.")

    sock.sendto(connect_msg, esp_addr)
    rx_thread.start()
    tx_thread.start()

    try:
        while not stop.is_set():
            time.sleep(1)
            sock.sendto(connect_msg, esp_addr)
    except KeyboardInterrupt:
        print("\nEncerrando...")
    finally:
        stop.set()
        try:
            sock.sendto(f"DISCONNECT:{local_ip}:{local_port}".encode("ascii"), esp_addr)
        except OSError:
            pass
        ser.close()
        sock.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
