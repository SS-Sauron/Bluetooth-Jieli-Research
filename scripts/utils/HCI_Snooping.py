import socket

# Create a raw HCI socket
try:
    hci_sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
    hci_sock.bind((0,))  # Bind to device 0 (hci0)
except Exception as e:
    print(f"Error creating HCI socket: {e}")
    exit()

print("Listening for HCI packets...")
while True:
    data = hci_sock.recv(1024)
    print(f"Captured HCI packet: {data.hex()}")
