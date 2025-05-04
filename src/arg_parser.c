#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "arg_parser.h"

//Parse and validate each command-line option
//Stores an extracted value into its correct field inside args
error_t arg_parser(int key, char *arg, struct argp_state *state) {
	struct run_arguments *args = state->input;
	error_t ret = 0;

	switch(key) {

	// Debug flag -d
    case 'd': {
		args->debug_mode = 1;
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
struct run_arguments parseopt(int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "debug", 'd', NULL, 0, "Enable debug mode for extra output", 0},
		{0}
	};

	struct argp argp_settings = {options, arg_parser, 0, 0, 0};

	struct run_arguments args;
	memset(&args, 0, sizeof(args));
	args.debug_mode = 0;  // Default to false

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0) {
		fprintf(stderr, "Error while parsing\n");
        exit(1);
	}
    // Argument validation here, add if needed

    return args;
}
