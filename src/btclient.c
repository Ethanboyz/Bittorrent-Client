/*
* Main function here so entry point into rest of our code
* Executable runs this
* Reads command line arguments needed for parsing torrent and connecting to tracker
* TODO: need to decide how to connect this to the rest of our pieces
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arg_parser.h"
#include "torrent_parser.h"

int main(int argc, char *argv[]) {
    struct run_arguments args = arg_parseopt(argc, argv);

}