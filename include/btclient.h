

#ifndef BTCLIENT_H
#define BTCLIENT_H

#include "arg_parser.h"

/**
 * @return run arguments
 */
struct run_arguments get_args(void);

/* Getters */
struct pollfd *get_fds(void);   // Return "authentic" fds array
int *get_num_fds(void);         // Return number of fds in fds array
Peer *get_peers(void);          // Return "authentic" peers array
int *get_num_peers(void);       // Return number of peers

/**
 * @brief Start client listening for incoming connections.
 * @return 0 if successful, -1 otherwise.
 */
int client_listen(int port);

#endif