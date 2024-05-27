### ASP-final-project
A Client-Server file sharing project developed for the Advanced System Programming course.

- **Server Components**:
  - `serverw24` | `mirror1` | `mirror2`
    - Receive commands from clients and send relevant responses (or errors in case of any error)
    - Multiple clients can connect to `serverw24` or any one of the mirrors
    - Load balancing is done at the server side based on the connection count of each server

- **Client Components**:
  - `clientw24`
    - Clients can connect to the server and request different commands
    - List of Commands:
      - `dirlist -a`: List directories alphabetically
      - `dirlist -t`: List directories by creation time
      - `w24fn <filename>`: Search for files with the given name
      - `w24fz <size1> <size2>`: Search for files within the given size range and receive them as tar
      - `w24ft <ext1> [<ext2> ...]`: Search for files with specified extensions and receive them as tar
      - `w24fdb <date>`: Search for files created before or on the given date and receive them as tar
      - `w24fda <date>`: Search for files created after or on the given date and receive them as tar
      - `quitc`: Disconnect from the server

- **Technologies Used**: C, Dynamic Memory Allocation (malloc), Socket Communication, Load Balancing, Directory Traversal (nftw), File Manipulation (tar), Signal Handling, File I/O, Date and Time Manipulation (ctime, strptime), Process Management (fork, exec)
