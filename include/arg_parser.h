#include <inttypes.h>
#include <arpa/inet.h>
#include <argp.h>

// Holds parsed run arguments for the client
struct run_arguments {
    int debug_mode;             // If debug mode should be enabled (writes output to log file)
    int port;
    char *filename;             // Torrent file
};

/**
 * @brief Parse the CLI arguments
 * 
 * @param key CLI arg key
 * @param arg CLI arg value
 * @param state State of arg parsing
 * @return error_t If an error occurs while parsing
 */
error_t arg_parser(int key, char *arg, struct argp_state *state);

/**
 * @brief CLI argument parsing
 * 
 * @param argc Number of CLI arguments
 * @param argv Values of the CLI arguments
 * @return the resolved CLI values
 */
struct run_arguments arg_parseopt(int argc, char *argv[]);
