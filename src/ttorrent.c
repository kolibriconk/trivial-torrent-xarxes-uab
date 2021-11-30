// Trivial Torrent

#include "file_io.h"
#include "logger.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define BUFFER_SIZE 65535

/**
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3231; // = htonl(0x31321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };

int serverMode(char** argv);
int clientMode(char** argv);

/**
 * Main function.
 */
int main(int argc, char **argv) {

	set_log_level(LOG_DEBUG);

	log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "Jose Antonio Ramos Andrades");
	int result = 0;
	if (argc == 2) 			// If there is only 2 args then we have a client initialization (clientMode)
		result = clientMode(argv);
	else if (argc == 4) 		// If there is only 4 args we have a server initialization (serverMode)
		result = serverMode(argv);
	else { 					// Otherwise the arguments are incorrect
		perror("There are missing or too much arguments"); 
		result = -1;
	}

    if (result != 0)
        result = 1;

    return result;
}

/**
* This function starts the program using client mode
* asking for blocks
* @param argv gives the values passed by argument on calling
* @return the value of parameter.
*/
int clientMode(char** argv) {
	struct torrent_t torrent;

	//char pathFile[strlen(argv[1])-9];
	char pathFile[strlen(argv[1])-9+1];
	strncpy(pathFile, argv[1], strlen(argv[1])-9); //Copying everything but the last 9 chars to remove .ttorrent from name
	pathFile[strlen(argv[1])-9] = '\0';

	if (create_torrent_from_metainfo_file(argv[1], &torrent, pathFile)) {
      		perror("Error while creating torrent ");
		return -1;
  	}

	int sock;
	uint8_t sendMessage[RAW_MESSAGE_SIZE], receiveMessage[MAX_BLOCK_SIZE];//Declaring the buffers from send and receive
	sock = socket(AF_INET, SOCK_STREAM, 0); //Initializing socket to AF_INET and SOCK_STREAM so it can manage with TCP.

	if (sock < 0) { //Checking if socket is ok after initializing
		perror("\nError while creating socket ");
		return -1;
	}

	uint64_t correctBlocks = 0;
	//Iterating over all peers to reach connection
	for (uint64_t i = 0; i < torrent.peer_count; i++) {
		if (correctBlocks == torrent.block_count) // If the number of blocks equals to the number of correctBLocks we are done
			break;
		struct sockaddr_in serverAddr;
		serverAddr.sin_family = AF_INET;
		char aux[20];

		//Code just next to this comment is a way to parse the array to an string.
		snprintf(aux, sizeof(aux), "%d.%d.%d.%d", torrent.peers[i].peer_address[0]
		, torrent.peers[i].peer_address[1], torrent.peers[i].peer_address[2], torrent.peers[i].peer_address[3]);

		serverAddr.sin_addr.s_addr = inet_addr(aux); //Setting inet_addr of the actual peer from torrent structure
		serverAddr.sin_port = torrent.peers[i].peer_port; //Setting the port of the actual peer from torrent structure

		log_printf(LOG_INFO, "\nConnectant a server %s:%d ..."
			, aux, ntohs(torrent.peers[i].peer_port));

		if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) { //Trying to connect to the specified peer
			log_printf(LOG_INFO, "\nServer no disponible");
			continue;
    	}

		log_printf(LOG_INFO, "\nConnectat correctament!");
		// Iterating over the number of blocks until we get it or we encounteer an error.
		for (uint64_t block_number = 0; block_number < torrent.block_count; block_number++) {
			if (torrent.block_map[block_number] == 0) { // If there is no block at the memory we request for it
				log_printf(LOG_INFO, "\nBlock %d incorrecte, el demanem ...", block_number);
				uint64_t blockSize = get_block_size(&torrent, block_number);
				// Code just next this comment is a parsing byte form to addequate the info into the wire format specified in documentation
				// The 4 first is fore the magic_number, the 5 is for the request type and the others are for number of block to request
				sendMessage[0]  = (uint8_t)(MAGIC_NUMBER >> 0) & 0xff;
				sendMessage[1]  = (uint8_t)(MAGIC_NUMBER >> 8) & 0xff;
				sendMessage[2]  = (uint8_t)(MAGIC_NUMBER >> 16) & 0xff;
				sendMessage[3]  = (uint8_t)(MAGIC_NUMBER >> 24) & 0xff;
				sendMessage[4]  = MSG_REQUEST;
				sendMessage[5]  = (uint8_t)(block_number >> 56)  & 0xff;
				sendMessage[6]  = (uint8_t)(block_number >> 48)  & 0xff;
				sendMessage[7]  = (uint8_t)(block_number >> 40) & 0xff;
				sendMessage[8]  = (uint8_t)(block_number >> 32) & 0xff;
				sendMessage[9]  = (uint8_t)(block_number >> 24) & 0xff;
				sendMessage[10] = (uint8_t)(block_number >> 16) & 0xff;
				sendMessage[11] = (uint8_t)(block_number >> 8) & 0xff;
				sendMessage[12] = (uint8_t)(block_number >> 0) & 0xff;

				if (send(sock, sendMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Trying to send the message to the server
		            perror("Petition cannot be sended ");
					continue;
                }
				if (recv(sock, receiveMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Receiving only the 13 first bytes to check if servar can provide the block requested
                	perror("\nNo communication receibed or cannot read it properly ");
                    continue;
				}

				uint8_t typeRcv = receiveMessage[4];
				//Next lines are for mounting the magic number
				uint32_t magicNum = ((uint32_t)receiveMessage[0] << 0)
				| ((uint32_t)receiveMessage[1] << 8)
				| ((uint32_t)receiveMessage[2] << 16)
				| ((uint32_t)receiveMessage[3] << 24);

				if (magicNum != MAGIC_NUMBER) { // Checking if the magic number match
					log_printf(LOG_INFO,"\nEl magic number no coincideix, abortem!");
					return -1;
				}
				if (typeRcv == MSG_RESPONSE_OK) { // If the response is OK we ask for the block
					recv(sock, receiveMessage, blockSize, MSG_WAITALL);
					struct block_t blck;
					blck.size = blockSize; // Setting the block size to struct block
					memcpy(blck.data, receiveMessage, blockSize); // Copying from buffer to memory (to block data field)
					int storeResult = store_block(&torrent, block_number, &blck); // Storing the result on disk
					if (storeResult == 0) {
						log_printf(LOG_INFO, "Block #%d emmagatzemat amb èxit!", block_number);
						correctBlocks++;
					}
				}

				if (typeRcv == MSG_RESPONSE_NA) {} // do nothing
			}
		}
		if (close(sock) == -1) // Closing the socket after every peer connection so we can connect to the next one if it is necessary
            perror("Error while closing socket ");
	}
	return 0;
}

/**
* This function starts the program using server mode
* serving asked blocks
* @param argv gives the values passed by argument on calling
* @return the value of parameter.
*/
int serverMode(char** argv) {
	struct torrent_t torrent;

	//char pathFile[strlen(argv[3])-9];
	char pathFile[strlen(argv[3])-9+1];
	strncpy(pathFile, argv[3], strlen(argv[3])-9); //Copying everything but the last 9 chars to remove .ttorrent from name
	pathFile[strlen(argv[3])-9] = '\0';

	if (create_torrent_from_metainfo_file(argv[3], &torrent, pathFile)) {
      	perror("Error while creating torrent file ");
		return -1;
  	}

	int sock, clientSock;
	//uint8_t sendMessage[RAW_MESSAGE_SIZE], receiveMessage[MAX_BLOCK_SIZE]; // Declaring the buffers from send and receive
	uint8_t receiveMessage[MAX_BLOCK_SIZE]; // Declaring the buffers from send and receive
	sock = socket(AF_INET, SOCK_STREAM, 0); // Initializing socket to AF_INET and SOCK_STREAM so it can manage with TCP.

	if (sock < 0) { // Checking if socket is ok after initializing
		perror("\nError while openning socket ");
		return -1;
	}

	struct sockaddr_in serverAddr, client_addr;
	serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((uint16_t)atoi(argv[2])); // Setting port to network byte order from argv[2]
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Setting ip addr to localhost

	if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr))<0) { // Binding address and port to process
        perror("Error while binding to port ");
        return -1;
    }

    if (listen(sock, 5) < 0) { // Setting up the socket to listen incoming connections
        perror("Error while trying to listen for connections ");
        return -1;
    }

	socklen_t socklenClient = sizeof(client_addr);
	for (;;) { // Forever listen for connections
		clientSock = accept(sock, (struct sockaddr*)&client_addr, &socklenClient); // Accepting sockets from clients
		if (clientSock > 0) {
			if (fork() == 0) { // Creating new child from main process to attend the accepted client socket.
				if (close(sock) == -1) // Closing parent socket to avoid collisions with others clients
                    perror("Error while closing socket ");

				for (;;) // Forever serving packets requested
				{
					if (recv(clientSock, receiveMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Receiving only the 13 to see the block number to check
						perror("Cannot read incoming transmission from client ");
						return -1;
					}

					//Next lines are for mounting the magic number
					uint32_t magicNum = ((uint32_t)receiveMessage[0] << 0)
					| ((uint32_t)receiveMessage[1] << 8)
					| ((uint32_t)receiveMessage[2] << 16)
					| ((uint32_t)receiveMessage[3] << 24);

					if (magicNum != MAGIC_NUMBER) { // Checking if the magic number match (check if client and server speaks the same "language")
						log_printf(LOG_INFO,"\nEl magic number no coincideix, abortem!");
						if (destroy_torrent (&torrent) == -1)
							perror("Error while destroying torrent ");
						return -1;
					}
					log_printf(LOG_INFO, "\nEl magic number coincideix, continuem l'execució");
					// Next lines are for mounting the number of the block requested
					uint64_t requestedBlock = ((uint64_t)receiveMessage[12] << 0)
					| ((uint64_t)receiveMessage[11] << 8)
					| ((uint64_t)receiveMessage[10] << 16)
					| ((uint64_t)receiveMessage[9] << 24)
					| ((uint64_t)receiveMessage[8] << 32)
					| ((uint64_t)receiveMessage[7] << 40)
					| ((uint64_t)receiveMessage[6] << 48)
					| ((uint64_t)receiveMessage[5] << 56);

					log_printf(LOG_INFO, "Es demana el block: %d", requestedBlock);
					//memcpy(sendMessage, receiveMessage, sizeof(receiveMessage));
					if (requestedBlock > torrent.block_count) { // Checking requested block consistency
						perror("Block number requested is greather than requested ");
						return -1;
					}

					if (torrent.block_map[requestedBlock] == 1) { // Checking if the requested number is mapped
						//sendMessage[4]  = MSG_RESPONSE_OK; // If the block is mapped set OK 
						receiveMessage[4]  = MSG_RESPONSE_OK; // If the block is mapped set OK 
						//if (send(clientSock, sendMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Trying to send the message to the client
						if (send(clientSock, receiveMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Trying to send the message to the client
							perror("Cannot send petition ");
							return -1;
						}

						struct block_t block;
						uint64_t blockSize = get_block_size(&torrent, requestedBlock);
						log_printf(LOG_INFO, "El block enviat es de: %d bytes", blockSize);
						load_block (&torrent, requestedBlock, &block); // Loadding block to memory

						if (send(clientSock, block.data, block.size, 0) < 0) { // Trying to send the message to the client
							perror("Cannot send petition ");
							return -1;
						}
					} else {
						//sendMessage[4]  = MSG_RESPONSE_NA; // If the block is not accessible set NA to response
						receiveMessage[4]  = MSG_RESPONSE_NA; // If the block is not accessible set NA to response
						//if (send(clientSock, sendMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Trying to send the message to the client
						if (send(clientSock, receiveMessage, RAW_MESSAGE_SIZE, 0) < 0) { // Trying to send the message to the client
							perror("Cannot send petition ");
							return -1;
						}
					}
				}
			} else {
                if (close(clientSock) == -1) // Closing parent socket to avoid collisions with others clients
                    perror("Error while closing socket ");
			}

		}
	}
	if (destroy_torrent (&torrent) == -1)
		perror("Error while destroying torrent ");

	if (close(sock) == -1) //Ending life of main socket
        perror("Error while closing socket ");

	return 0;
}
