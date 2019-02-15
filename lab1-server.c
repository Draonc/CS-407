//Candice Sandefur

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include "readline.c"

#define PORT 4070

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n"; 
const char* error = "<error>\n";
const char* secret = "<cs407rembash>\n";

void handle_client(int client_sockfd);
char* checkSocket(int client_sockfd);

int main()
{
  int server_sockfd, client_sockfd;
  pid_t pid;
  socklen_t server_len, client_len;
  struct sockaddr_in server_address;
  struct sockaddr_in client_address;
  
  
  if((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    perror("Failed to create socket");
    exit(EXIT_FAILURE);
  }
  
  int i=1;
  setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
  
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(PORT);
  server_len = sizeof(server_address);
  
  if(bind(server_sockfd, (struct sockaddr *)&server_address, server_len) == -1){
    perror("Failed to bind socket");
    exit(EXIT_FAILURE);
  }
  
  listen(server_sockfd, 5);
  
  while(1) {    
    client_len = sizeof(client_address);
    if((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len)) < 0){
      perror("Accept has failed.");
    }
    
    
    switch(pid = fork()){
    case -1:
      perror("Failed to fork");
      break;
    case 0:
      close(server_sockfd);
      handle_client(client_sockfd);
      exit(EXIT_SUCCESS);
      break;
    default:
      close(client_sockfd);
      while(waitpid(-1, NULL, WNOHANG) > 0);
    }
  }
  exit(EXIT_SUCCESS);
}



void handle_client(int client_sockfd)
{
  
  char *message_buffer;
  setsid();
  
  
  if(write(client_sockfd, rembash, strlen(rembash)) < 0){
    perror("Failed to write to the socket1");
    exit(EXIT_FAILURE);
  }
  
  message_buffer = checkSocket(client_sockfd);
  
  if(strcmp(message_buffer, secret) != 0){
    write(client_sockfd, error, strlen(error));
  }
  
  if(write(client_sockfd, ok, strlen(ok)) < 0){
    perror("Failed to write to the socket");
    exit(EXIT_FAILURE);
  }
  
  free(message_buffer);
 
  if((dup2(client_sockfd, STDIN_FILENO) | dup2(client_sockfd, STDOUT_FILENO) |  dup2(client_sockfd, STDERR_FILENO)) < 0) {
    perror("Dup has failed");
    exit(EXIT_FAILURE);
  }
  
  execlp("bash","bash","--noediting", "-i", NULL);
  
  if(errno){
    exit(EXIT_FAILURE);
  }
  
  close(client_sockfd);
  return;
}

char* checkSocket(int client_sockfd){
  char *buff;
  
  if((buff = readline(client_sockfd)) == NULL){
    perror("Failed to read from socket");
    exit(EXIT_FAILURE);
  }
  
  return buff;
}
