from enum import Enum
import argparse
import socket
import threading
import os
import sys

class client:

    # **** TYPES ****
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum):
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # **** ATTRIBUTES ****
    _server = None
    _port = -1
    _connected_user = None
    _listen_socket = None
    _listen_port = None
    _listen_thread = None
    _running = False

    # **** METHODS ****

    @staticmethod
    def register(user):
        try:
            # Create socket and connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send REGISTER operation
            s.sendall("REGISTER".encode() + b'\0')

            # Send username
            s.sendall(user.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Process response
            if response == 0:
                print("REGISTER OK")
                return client.RC.OK
            elif response == 1:
                print("USERNAME IN USE")
                return client.RC.USER_ERROR
            else:
                print("REGISTER FAIL")
                return client.RC.ERROR

        except Exception as e:
            print("REGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def unregister(user):
        try:
            # Create socket and connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send UNREGISTER operation
            s.sendall("UNREGISTER".encode() + b'\0')

            # Send username
            s.sendall(user.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Process response
            if response == 0:
                print("UNREGISTER OK")
                return client.RC.OK
            elif response == 1:
                print("USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                print("UNREGISTER FAIL")
                return client.RC.ERROR

        except Exception as e:
            print("UNREGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def connect(user):
        try:
            # Check if already connected
            if client._connected_user is not None:
                # Disconnect current user first
                client.disconnect(client._connected_user)

            # Find a free port
            listen_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listen_socket.bind(('', 0))  # Bind to any available port
            listen_port = listen_socket.getsockname()[1]
            listen_socket.listen(5)

            # Save socket and port
            client._listen_socket = listen_socket
            client._listen_port = listen_port

            # Start listener thread
            client._running = True
            client._listen_thread = threading.Thread(target=client._file_listener)
            client._listen_thread.daemon = True
            client._listen_thread.start()

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send CONNECT operation
            s.sendall("CONNECT".encode() + b'\0')

            # Send username
            s.sendall(user.encode() + b'\0')

            # Send port
            s.sendall(str(listen_port).encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Process response
            if response == 0:
                print("CONNECT OK")
                client._connected_user = user
                return client.RC.OK
            elif response == 1:
                print("CONNECT FAIL, USER DOES NOT EXIST")
                client._stop_listener()
                return client.RC.USER_ERROR
            elif response == 2:
                print("USER ALREADY CONNECTED")
                client._stop_listener()
                return client.RC.USER_ERROR
            else:
                print("CONNECT FAIL")
                client._stop_listener()
                return client.RC.ERROR

        except Exception as e:
            print("CONNECT FAIL")
            client._stop_listener()
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        try:
            # Check if user is connected
            if client._connected_user != user:
                print("DISCONNECT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send DISCONNECT operation
            s.sendall("DISCONNECT".encode() + b'\0')

            # Send username
            s.sendall(user.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Stop listener thread regardless of server response
            client._stop_listener()

            # Process response
            if response == 0:
                print("DISCONNECT OK")
                client._connected_user = None
                return client.RC.OK
            elif response == 1:
                print("DISCONNECT FAIL, USER DOES NOT EXIST")
                client._connected_user = None
                return client.RC.USER_ERROR
            elif response == 2:
                print("DISCONNECT FAIL, USER NOT CONNECTED")
                client._connected_user = None
                return client.RC.USER_ERROR
            else:
                print("DISCONNECT FAIL")
                client._connected_user = None
                return client.RC.ERROR

        except Exception as e:
            print("DISCONNECT FAIL")
            client._stop_listener()
            client._connected_user = None
            return client.RC.ERROR

    @staticmethod
    def publish(fileName, description):
        try:
            # Check if user is connected
            if client._connected_user is None:
                print("PUBLISH FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send PUBLISH operation
            s.sendall("PUBLISH".encode() + b'\0')

            # Send username
            s.sendall(client._connected_user.encode() + b'\0')

            # Send filename
            s.sendall(fileName.encode() + b'\0')

            # Send description
            s.sendall(description.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Process response
            if response == 0:
                print("PUBLISH OK")
                return client.RC.OK
            elif response == 1:
                print("PUBLISH FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif response == 2:
                print("PUBLISH FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif response == 3:
                print("PUBLISH FAIL, CONTENT ALREADY PUBLISHED")
                return client.RC.USER_ERROR
            else:
                print("PUBLISH FAIL")
                return client.RC.ERROR

        except Exception as e:
            print("PUBLISH FAIL")
            return client.RC.ERROR

    @staticmethod
    def delete(fileName):
        try:
            # Check if user is connected
            if client._connected_user is None:
                print("DELETE FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send DELETE operation
            s.sendall("DELETE".encode() + b'\0')

            # Send username
            s.sendall(client._connected_user.encode() + b'\0')

            # Send filename
            s.sendall(fileName.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Close connection
            s.close()

            # Process response
            if response == 0:
                print("DELETE OK")
                return client.RC.OK
            elif response == 1:
                print("DELETE FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif response == 2:
                print("DELETE FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif response == 3:
                print("DELETE FAIL, CONTENT NOT PUBLISHED")
                return client.RC.USER_ERROR
            else:
                print("DELETE FAIL")
                return client.RC.ERROR

        except Exception as e:
            print("DELETE FAIL")
            return client.RC.ERROR

    @staticmethod
    def listusers():
        try:
            # Check if user is connected
            if client._connected_user is None:
                print("LIST_USERS FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send LIST_USERS operation
            s.sendall("LIST USERS".encode() + b'\0')

            # Send username
            s.sendall(client._connected_user.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Process response
            if response == 0:
                # Read number of users
                num_users_str = client._read_string(s)
                num_users = int(num_users_str)

                print("LIST_USERS OK")

                # Read user information
                for i in range(num_users):
                    username = client._read_string(s)
                    ip = client._read_string(s)
                    port = client._read_string(s)
                    print(f"{username}\n{ip}\n{port}")

                # Close connection
                s.close()
                return client.RC.OK
            elif response == 1:
                print("LIST_USERS FAIL, USER DOES NOT EXIST")
                s.close()
                return client.RC.USER_ERROR
            elif response == 2:
                print("LIST_USERS FAIL, USER NOT CONNECTED")
                s.close()
                return client.RC.USER_ERROR
            else:
                print("LIST_USERS FAIL")
                s.close()
                return client.RC.ERROR

        except Exception as e:
            print("LIST_USERS FAIL")
            return client.RC.ERROR

    @staticmethod
    def listcontent(user):
        try:
            # Check if user is connected
            if client._connected_user is None:
                print("LIST_CONTENT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Connect to server
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Send LIST_CONTENT operation
            s.sendall("LIST CONTENT".encode() + b'\0')

            # Send username
            s.sendall(client._connected_user.encode() + b'\0')

            # Send target username
            s.sendall(user.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Process response
            if response == 0:
                # Read number of files
                num_files_str = client._read_string(s)
                num_files = int(num_files_str)

                print("LIST_CONTENT OK")

                # Read file information
                for i in range(num_files):
                    filename = client._read_string(s)
                    print(filename)

                # Close connection
                s.close()
                return client.RC.OK
            elif response == 1:
                print("LIST_CONTENT FAIL, USER DOES NOT EXIST")
                s.close()
                return client.RC.USER_ERROR
            elif response == 2:
                print("LIST_CONTENT FAIL, USER NOT CONNECTED")
                s.close()
                return client.RC.USER_ERROR
            elif response == 3:
                print("LIST_CONTENT FAIL, REMOTE USER DOES NOT EXIST")
                s.close()
                return client.RC.USER_ERROR
            else:
                print("LIST_CONTENT FAIL")
                s.close()
                return client.RC.ERROR

        except Exception as e:
            print("LIST_CONTENT FAIL")
            return client.RC.ERROR

    @staticmethod
    def getfile(user, remote_FileName, local_FileName):
        try:
            # Check if user is connected
            if client._connected_user is None:
                print("GET_FILE FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR

            # Get user IP and port
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            s.sendall("LIST USERS".encode() + b'\0')
            s.sendall(client._connected_user.encode() + b'\0')

            response = s.recv(1)[0]
            if response != 0:
                s.close()
                print("GET_FILE FAIL")
                return client.RC.ERROR

            # Read number of users
            num_users_str = client._read_string(s)
            num_users = int(num_users_str)

            # Find target user
            target_ip = None
            target_port = None
            for i in range(num_users):
                username = client._read_string(s)
                ip = client._read_string(s)
                port = client._read_string(s)

                if username == user:
                    target_ip = ip
                    target_port = int(port)
                    break

            s.close()

            if target_ip is None or target_port is None:
                print("GET_FILE FAIL")
                return client.RC.ERROR

            # Connect to target user
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((target_ip, target_port))

            # Send GET_FILE operation
            s.sendall("GET FILE".encode() + b'\0')

            # Send filename
            s.sendall(remote_FileName.encode() + b'\0')

            # Receive response code
            response = s.recv(1)[0]

            # Process response
            if response == 0:
                # Read file size
                file_size_str = client._read_string(s)
                file_size = int(file_size_str)

                # Create local file
                try:
                    with open(local_FileName, 'wb') as f:
                        # Read file content
                        bytes_received = 0
                        while bytes_received < file_size:
                            chunk = s.recv(min(4096, file_size - bytes_received))
                            if not chunk:
                                break
                            f.write(chunk)
                            bytes_received += len(chunk)

                        # Check if all bytes were received
                        if bytes_received == file_size:
                            print("GET_FILE OK")
                            s.close()
                            return client.RC.OK
                        else:
                            os.remove(local_FileName)
                            print("GET_FILE FAIL")
                            s.close()
                            return client.RC.ERROR
                except:
                    # Remove incomplete file
                    if os.path.exists(local_FileName):
                        os.remove(local_FileName)
                    print("GET_FILE FAIL")
                    s.close()
                    return client.RC.ERROR
            elif response == 1:
                print("GET_FILE FAIL, FILE NOT EXIST")
                s.close()
                return client.RC.USER_ERROR
            else:
                print("GET_FILE FAIL")
                s.close()
                return client.RC.ERROR

        except Exception as e:
            # Remove incomplete file
            if os.path.exists(local_FileName):
                os.remove(local_FileName)
            print("GET_FILE FAIL")
            return client.RC.ERROR

    # Helper methods
    @staticmethod
    def _read_string(sock):
        """Read a null-terminated string from a socket"""
        result = bytearray()
        while True:
            char = sock.recv(1)
            if char == b'\0' or not char:
                break
            result.extend(char)
        return result.decode()

    @staticmethod
    def _stop_listener():
        """Stop the file listener thread"""
        if client._running:
            client._running = False
            if client._listen_socket:
                client._listen_socket.close()
                client._listen_socket = None
            if client._listen_thread:
                client._listen_thread = None
            client._listen_port = None

    @staticmethod
    def _file_listener():
        """Thread function to listen for file requests"""
        while client._running:
            try:
                # Accept connection
                conn, addr = client._listen_socket.accept()

                # Handle connection in a new thread
                handler = threading.Thread(target=client._handle_file_request, args=(conn,))
                handler.daemon = True
                handler.start()
            except:
                # Socket closed or error
                break

    @staticmethod
    def _handle_file_request(conn):
        """Handle a file request from another client"""
        try:
            # Read operation
            operation = client._read_string(conn)

            if operation == "GET FILE":
                # Read filename
                filename = client._read_string(conn)

                # Check if file exists
                if os.path.isfile(filename):
                    # Send success code
                    conn.sendall(bytes([0]))

                    # Get file size
                    file_size = os.path.getsize(filename)

                    # Send file size
                    conn.sendall(str(file_size).encode() + b'\0')

                    # Send file content
                    with open(filename, 'rb') as f:
                        while True:
                            chunk = f.read(4096)
                            if not chunk:
                                break
                            conn.sendall(chunk)
                else:
                    # Send file not found code
                    conn.sendall(bytes([1]))
            else:
                # Send error code
                conn.sendall(bytes([2]))
        except:
            # Send error code
            try:
                conn.sendall(bytes([2]))
            except:
                pass
        finally:
            # Close connection
            conn.close()

if __name__ == "__main__":
    # Parse command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--server', required=True, help='Server IP address')
    parser.add_argument('-p', '--port', required=True, type=int, help='Server port')
    args = parser.parse_args()

    # Set server address and port
    client._server = args.server
    client._port = args.port

    print("Cliente iniciado. Conectado a {}:{}".format(args.server, args.port))
    print("Comandos disponibles:")
    print("  REGISTER <username>")
    print("  UNREGISTER <username>")
    print("  CONNECT <username>")
    print("  DISCONNECT <username>")
    print("  PUBLISH <filename> <description>")
    print("  DELETE <filename>")
    print("  LIST USERS")
    print("  LIST CONTENT <username>")
    print("  GET FILE <username> <remote_file> <local_file>")
    print("  QUIT")

    # Main loop
    while True:
        try:
            command = input(">> ")
            parts = command.split()

            if not parts:
                continue

            if parts[0] == "REGISTER" and len(parts) == 2:
                client.register(parts[1])
            elif parts[0] == "UNREGISTER" and len(parts) == 2:
                client.unregister(parts[1])
            elif parts[0] == "CONNECT" and len(parts) == 2:
                client.connect(parts[1])
            elif parts[0] == "DISCONNECT" and len(parts) == 2:
                client.disconnect(parts[1])
            elif parts[0] == "PUBLISH" and len(parts) >= 3:
                filename = parts[1]
                description = " ".join(parts[2:])
                client.publish(filename, description)
            elif parts[0] == "DELETE" and len(parts) == 2:
                client.delete(parts[1])
            elif parts[0] == "LIST":
                if len(parts) == 2 and parts[1] == "USERS":
                    client.listusers()
                elif len(parts) == 3 and parts[1] == "CONTENT":
                    client.listcontent(parts[2])
                else:
                    print("Comando LIST inválido")
            elif parts[0] == "GET" and parts[1] == "FILE" and len(parts) == 5:
                client.getfile(parts[2], parts[3], parts[4])
            elif parts[0] == "QUIT":
                if client._connected_user:
                    client.disconnect(client._connected_user)
                break
            else:
                print("Comando no reconocido")

        except KeyboardInterrupt:
            if client._connected_user:
                client.disconnect(client._connected_user)
            break
        except Exception as e:
            print(f"Error: {e}")

    print("Cliente terminado")