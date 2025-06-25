# p2p_server
---
## Description: This is a cross-platform peer to peer file sharing application written in C++.
##              This program allows multiple peers to connect and share files.

## How to run:
### 1. Start the first peer connection with: ./p2p_app 8080
###    (This will start a listening peer on port 8080)

### 2. Start a second peer and connect to the first peer: ./p2p_app 8081 192.X.X.1 8080
###                                                                       ^(IP addr of the 1st peer)

### 3. On the second peer's terminal to send any files type: send 0 my_files.zip
###    (You can send ANY type of file)

![image](https://github.com/user-attachments/assets/eca0d8e6-d974-4ce7-9513-b8a5cb6cb397)

