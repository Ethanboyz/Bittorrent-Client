#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "arg_parser.h"

// Parse and validate each command-line option
// Stores an extracted value into its correct field inside args
error_t arg_parser(int key, char *arg, struct argp_state *state) {
	struct run_arguments *args = state->input;
	error_t ret = 0;
	int len;

	switch(key) {
	// Debug flag -d
    case 'd': {
		args->debug_mode = 1;
		break;
	}
	case 'p': {
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		if (0 /* port is invalid */) {
			argp_error(state, "Invalid option for a port, must be a number");
		}
		break;
	}
	case 'f': {
		/* validate torrent file */
		len = strlen(arg);
		args->filename = malloc(len + 1);
		strcpy(args->filename, arg);
		break;
	}
	case 'A': {
		args->peer_ip = arg;
		break;
	}
	case 'P': {
		args->peer_port = atoi(arg);
		break;
	}
	case 's': {
		args->seed_after = true;
		break;
	}
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

// Defines all the available options
// Calls arg_parse() to actually parse argc/argv
// After parsing, maybe do arg validation, if needed
struct run_arguments arg_parseopt(int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "debug", 'd', NULL, 0, "Enable debug mode for extra output", 0},
		{ "port", 'p', "port", 0, "The port that is being used by client", 0},
		{ "file", 'f', "filename", 0, "The torrent file", 0},
		{ "peer-ip", 'A', "address", 0, "Only download from this peer", 0},
		{ "peer-port", 'P', "peer port", 0, "Peer's port if --peer-ip is specified", 0},
		{ "seed-after", 's', NULL, 0, "Seed after download complete", 0},
		{0}
	};

	struct argp argp_settings = { options, arg_parser, 0, 0, 0, 0, 0 };

	struct run_arguments args;
	memset(&args, 0, sizeof(args));

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0) {
		fprintf(stderr, "Error while parsing\n");
        exit(1);
	}
    // Argument validation here, add if needed

    return args;
}
