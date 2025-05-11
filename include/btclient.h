#ifndef BTCLIENT_H
#define BTCLIENT_H

#include "arg_parser.h"
// MOD: Forward declare Peer struct to avoid circular dependency if peer_manager.h includes btclient.h for get_args?????
struct Peer; 
struct pollfd;

/**
* @return run arguments
*/
struct run_arguments get_args(void);

/* Getters */
struct pollfd *get_fds(void); // Return "authentic" fds array
int *get_num_fds(void); // Return number of fds in fds array
struct Peer *get_peers(void); // Return "authentic" peers array // MOD: Changed Peer to struct Peer
int *get_num_peers(void); // Return number of peers

/**
* @brief Start client listening for incoming connections.
* @return 0 if successful, -1 otherwise.
*/
int client_listen(int port);

#endif