## Changes to be made before running:

### simDNSClient.c:
- Change the `LOCAL_IP` and `DESTINATION_IP` macros to the client's and server's IP respectively.
- Change the `INTERFACE` macro accordingly.

### simDNSServer.c:
- Change the `INTERFACE` macro accordingly.
- Change the `SOURCE_MAC` macro accordingly.
- Change the `LOCAL_IP_ADDRESS` macros to the corresponding IP address being used.

## Run the programs:

Run the `make all` command to create the server and client executables.

- To run the server:
    - Run the executable as `sudo ./server`.

- To run the client:
    - When running the executable file, provide the Source MAC and Destination MAC as command line arguments (in the same order as written in the readme).
    - Run the executable as `sudo ./client <source_MAC_address> <dest_MAC_address>`.
    - Provide the query in the format given in the prompt message that is displayed.
    - Also, make sure that the server is running before sending the query.
