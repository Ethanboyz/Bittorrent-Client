## Component: Argument Parser

Relevant Files: arg_parser.h, arg_parser.c

Usage Notes: The main client application (btclient.c) should call arg_parseopt() at startup

**TODO Notes**:

- Port Validation: The current code uses atoi() to parse the port number but lacks validation to ensure the input is a valid number and within the acceptable range for a port (0-65535).

- Torrent File Validation: The code stores the filename but doesn't validate if the file exists, is readable, or is a valid torrent file format before attempting to use it later.