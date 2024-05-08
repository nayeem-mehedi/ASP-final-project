# ASP-final-project
A Client Server file sharing project developed for Advanced System Programming course

## serverw24 | mirror1 | mirror2
- Receive commands from client and send relevent responses (or errors in case of any error)
- Multiple clients can connect to serverw24 or any one of the mirrors
- Load balancing is done at the server side

## clientw24
- Clients can connect to the server and request different commands
- List of Commands
  - search for files with different criteria and receive them as tar
  - list home directory
  - receive load balancing message when server/mirror wants the client to connect to a different server/mirror