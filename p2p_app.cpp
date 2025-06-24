#include <iostream> 
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>

using namespace std;

        /*Platform Specific Networking*/
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // linking with Winsock
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define  SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

    /*4KB buffer for file transfer*/
const int BUFFER_SIZE = 4096;

    /*Global*/
struct Peer {
    SOCKET socket;
    string ip_address;
    int port; 
    int id;
};

vector<Peer> peers;
mutex peers_mutex;  // Using a mutex to prevent multiple process acessing one file
int next_peer_id = 0;


    /*Forward Declarations*/
void initialize_networking();
void cleanup_networking();
void handle_peer(SOCKET peer_socket, string peer_ip, int peer_port);
void send_file_to_peer(int peer_id, const string& filepath);
void listen_for_peers(int port);
void user_interface(const string& own_address, int own_part);
void connect_to_peer(const string& target_ip, int target_port);

    /*Main*/
int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        cerr << "Usage: " << argv[0] << " <listening_port> [peer_ip peer_port]"
        << endl;
        return 1;
    }

    int listening_port = stoi(argv[1]);
    initialize_networking();

    /*Starting a Listening thread to accept new connections*/
    thread listener_thread(listen_for_peers, listening_port);
    listener_thread.detach(); // Run in the background

    // If a peer address is recieved, then connect to it
    if (argc == 4)
    {
        string peer_ip = argv[2];
        int peer_port = stoi(argv[3]);

        // Letting the listener a second to start up
        this_thread::sleep_for(chrono::seconds(1));
        connect_to_peer(peer_ip, peer_port);
    }

    /*Starting the user interface on the main thread*/
    user_interface("127.0.0.1", listening_port);
    cleanup_networking();
    return 0;
}


    /*Network Initialization and Cleanup*/
//This initalizes the network library for each specific platform (Windows/Linux/MacOS) 

void initialize_networking()
{
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
        {
            cerr << "WSAStartup failed." << end;
            exit(1);
        }
    #endif
}

void cleanup_networking()
{
    #ifdef _WIN32
        WSACleanup();
    #endif
}



    /*Peer Connection and Management*/
// Listens for incoming peer connections on the specified port.
/*This is the passive part of the P2P client. Essentially it sets up a 
'welcome desk' (listen_socket) and waits. When a new peer arrives, it sends it
a 'manager' (handle_peer thread) and goes back to waiting for the next arrival. 

*/

void listen_for_peers(int port) 
{
    // Creating a socket
    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_socket == INVALID_SOCKET)
    {
        cerr << "Failed to create listening socket" << endl;
        return;
    }

    // Binding the socket to our IP address and chosen port
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr));

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        cerr << "Bind failed on port " << port << "." << endl;
        closesocket(listen_socket);
    }

    // Putting the socket into listening mode
    //listen(listen_socket, SOMAXCONN);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "Listen failed." << endl;
        closesocket(listen_socket);
        return;
    }         

    cout << "[Sever] Listening for incoming connections on port " << port << "..." << endl;

    // Accepting connections
    while (true) 
    {
        sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);
        SOCKET client_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_size);

        if (client_socket == INVALID_SOCKET)
        {
            cerr << "Accept failed." << endl;
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        cout << "[Server] Accepted connection from " << client_ip << ":" << client_port << endl;

        // Adding the new peer to the list and spawning a handler thread
        // When a peer connects, we 'accept' then return a new socket for that 
        // specific peer.
        thread peer_thread(handle_peer, client_socket, string(client_ip), client_port);
        peer_thread.detach();
    }

    closesocket(listen_socket);
}


    /*Connecting this client to another peer.
        target_ip - The IP address of the peer to connect to
        target_port - The port of the peer to connect to

        This is the active part. WHen you type 'connect 127.0.0.1 8080, 
        this function gets called to initiate the handshake. Both the listener
        and the connector end up calling the same handle_peer function.
    */

void connect_to_peer(const string& target_ip, int target_port)
{
    // Create a socket
    SOCKET peer_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_socket == INVALID_SOCKET)
    {
        cerr << "Failed to create socket for connection." << endl;
        return;
    }
    // Setting up the address of the peer we want to connect to
    sockaddr_in peer_addr;
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(target_port);
    inet_pton(AF_INET, target_ip.c_str(), &peer_addr.sin_addr);

    // Then we connect
    if (connect(peer_socket, (sockaddr*)&peer_addr, sizeof(peer_addr)) == SOCKET_ERROR)
    {
        cerr << "Failed to connect to peer at " << target_ip << ":" << target_port << endl;
        closesocket(peer_socket);
        return;
    }

    cout << "[Client] Successfully connected to " << target_ip << ":" << target_port << endl;
    
    //Once connection is successful, we start a handler 'manager' thread for this new user
    thread peer_thread(handle_peer, peer_socket, target_ip, target_port);
    peer_thread.detach();
}


    /*Handler [Manager]
    
        This function manages a single peer connection: adds peer to the list and listens for data
        This function runs its own thread for each peer.
            peer_socket - The socket for the connected peer
            peer_ip     - The IP address of the peer.
            peer_port   - The port of the peer.    
    */

void handle_peer(SOCKET peer_socket, string peer_ip, int peer_port)
{
    int current_peer_id;
    {
        lock_guard<mutex> lock(peers_mutex);
        current_peer_id = next_peer_id++;
        peers.push_back({peer_socket, peer_ip, peer_port, current_peer_id});
    }

    char buffer[BUFFER_SIZE];
    // We loop constantly waiting for data from a specific peer
    while (true) 
    {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(peer_socket, buffer, BUFFER_SIZE, 0);

        if (bytes_received <= 0)
        {
            cout << "[Peer " << current_peer_id << "] " << peer_ip << " disconnected." << endl;
            break;
        }

            /* File Receiving Logic*/
        if (bytes_received >= sizeof(uint64_t) + 256)
        {
            uint64_t file_size;
            char filename[256];

            memcpy(&file_size, buffer, sizeof(uint64_t));
            memcpy(filename, buffer + sizeof(uint64_t), 256);

            filename[255] = '\0';
            string safe_filename(filename);

            cout << "[Peer " << current_peer_id << "] Receiving file " << safe_filename << " of size " << file_size << " bytes." << endl;
            
            // Now that we know the filename and how much data to expect
            // We create the file and start writing the data into it
            ofstream outfile("received_" + safe_filename, ios::binary);
            if (!outfile)
            {
                 cerr << "Error: Could not create file " << "received_" << safe_filename << endl;
                 continue;
            }

            uint64_t total_bytes_written = 0;
            // Writing the part of the file that was already in the initial buffer
            int initial_data_size = bytes_received - (sizeof(uint64_t) + 256);
            if(initial_data_size > 0)
            {
                outfile.write(buffer + sizeof(uint64_t) + 256, initial_data_size);
                total_bytes_written += initial_data_size;
            }

            // Looping to receive the rest of the file until we've saved 'file_size' bytes
            while (total_bytes_written < file_size)
            {
                int bytes = recv(peer_socket, buffer, BUFFER_SIZE, 0);
                if (bytes <= 0)
                {
                    cerr << "Peer disconneted during file transfer." << endl;
                    break;
                }
                outfile.write(buffer, bytes);
                total_bytes_written += bytes;
            }


            outfile.close();
            if (total_bytes_written == file_size)
            {
                cout << "[Peer " << current_peer_id << "] File " << safe_filename << " received successfully." << endl;
            }
            else 
            {
                cerr << "[Peer " << current_peer_id << "] File transfer for " << safe_filename << " was incomplete." << endl;

            }
        }
    }

    /*Cleanup*/
    {
        lock_guard<mutex> lock(peers_mutex);
        peers.erase(remove_if(peers.begin(), peers.end(),
            [current_peer_id](const Peer& p){
                return p.id == current_peer_id;
            }), peers.end());
    }
    closesocket(peer_socket);
}


    /*File Sending
        This sends a file to a specific conneted peer.
        peer_id - The ID of the peer to send the file to.
        filepath - the path to the file to be sent*/
void send_file_to_peer(int peer_id, const string& filepath)
{
    ifstream file(filepath, ios::binary | ios::ate);
    if (!file)
    {
        cerr << "Error: Cannot open file " << filepath << endl;
        return;
    }

    streamsize size = file.tellg();
    file.seekg(0, ios::beg);

    SOCKET target_socket = INVALID_SOCKET;
    {
        lock_guard<mutex> lock(peers_mutex);
        auto it = find_if(peers.begin(), peers.end(), [peer_id](const Peer& p)
        {
            return p.id == peer_id;
        });

        if (it == peers.end())
        {
            cerr << "Error: Peer with ID " << peer_id << " not found." << endl;
            return;
        }
        target_socket = it->socket;
    }

    /*File Sending Logic*/

    //  First we send a header with a file size and filename.
    uint64_t file_size = size;
    char header[sizeof(uint64_t) + 256];
    memset(header, 0, sizeof(header));

    // Copying the file size to header
    memcpy(header, &file_size, sizeof(uint64_t));

    // Copying filename to header
    string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    strncpy(header + sizeof(uint64_t), filename.c_str(), 255);
    header[sizeof(uint64_t) + 255] = '\0'; // Null-terminate

    // We send the header first
    if (send(target_socket, header, sizeof(header), 0) == SOCKET_ERROR)
    {
        cerr << "Failed to send file header." << endl;
        return;
    }

    cout << "Sending file " << filename << " (" << file_size << " bytes) to Peer " << peer_id << "..." << endl;

    // Sending the file content in chunks
    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        if (send(target_socket, buffer, file.gcount(), 0) == SOCKET_ERROR) 
        {
            cerr << "Failed to send file chunk." << endl;
            return;
        }
    }
    cout << "File sent successfully" << endl;
}

        /*USER INTERFACE*/

void user_interface(const string& own_address, int own_port)
{
    string line;
    cout << "\n --- P2P File Sharing CLI ---" << endl;
    cout << "Your address: " << own_address << ":" << own_port << endl;
    cout << "Commands:" << endl;
    cout << " connect <ip> <port>   - Connect to a new peer." << endl;
    cout << " peers                 - List connected peers."  << endl;
    cout << " send <peer_id> <path> - Send a file to a peer." << endl;
    cout << " exit                  - Close the application." << endl;
    cout << "=======================================" << endl;
    
    while (true)
    {
        cout << "> ";
        getline(cin, line);

        if (line.rfind("connect", 0) == 0)
        {
            string ip;
            int port;
            char cmd[10];
            if (sscanf(line.c_str(), "%s %s %d", cmd, &ip[0], &port) == 3)
            {
                connect_to_peer(ip, port);
            }
            else
            {
                cerr << "Usage: connect <ip> <port>" << endl;
            }
        }
        else if (line == "peers")
        {
            lock_guard<mutex> lock(peers_mutex);

            if (peers.empty())
            {
                cout << "No peers connected." << endl;
            }
            else 
            {
                cout << "Connected Peers:" << endl;
                for (const auto& peer : peers)
                {
                    cout << " ID: " << peer.id << " -> " << peer.ip_address << ":" << peer.port << endl;
                }
            }
        }
        else if (line.rfind("send", 0) == 0)
        {
                int peer_id;
                string filepath;
                char cmd[10];
                char path_buffer[256];

                if(sscanf(line.c_str(), "%s %d %s", cmd, &peer_id, path_buffer) == 3)
                {
                    filepath = path_buffer;
                    send_file_to_peer(peer_id, filepath);
                }
                else 
                {
                    cerr << "Usage: send <peer_id> <filepath>" << endl;
                }
        }
        else if (line == "exit")
        {
            break;
        }
        else if (!line.empty())
        {
            cerr << "Unknown command." << endl;
        }
    }

    lock_guard<mutex> lock(peers_mutex);
    for (const auto& peer : peers)
    {
        closesocket(peer.socket);
    }
    peers.clear();
    exit(0);
}
    






















