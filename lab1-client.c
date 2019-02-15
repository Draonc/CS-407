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
#include "readline.c"
#include <libgen.h>
#include <linux/limits.h>

#define PORT 4070

char* readSocket(int sockfd);
void parricide(int sockfd);

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n";
const char* error = "<error>\n";
const char* secret = "<cs407rembash>\n";

int main(int argc, char *argv[])
{
  if(argc != 2){
    perror("Invalid number of arguments\n");
    exit(EXIT_FAILURE);
  }
  
  int sockfd;
  int len;
  struct sockaddr_in address;
  
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(argv[1]);
  address.sin_port = htons(PORT);
  len = sizeof(address);
  
  if(connect(sockfd, (struct sockaddr *)&address, len) == -1){
    perror("Failed to establish connection with server");
    exit(EXIT_FAILURE);
  }
  
  char *buffer;
  char messageBuffer[512];
  pid_t pid;
  
  buffer = readSocket(sockfd);
  
  if(strcmp(buffer, rembash) != 0){
    perror("Problem with server response\n");
    exit(EXIT_FAILURE);
  }
  
  if(write(sockfd, secret, strlen(secret)) < 0){
    perror("Failed to write to the socket");
    exit(EXIT_FAILURE);
  }
  
  buffer = readSocket(sockfd);
  
  if(strcmp(buffer, ok) != 0){
    perror("Problem with server response\n");
    exit(EXIT_FAILURE);
  }
  
  int nread;
  switch(pid = fork()){
  case -1:
    perror("Fork failed");
    exit(EXIT_FAILURE);
    break;
  case 0:
    while((nread = read(STDIN_FILENO, messageBuffer, 512)) > 0){
      if(write(sockfd, messageBuffer, nread) < 0){
	perror("Write Failed");
	parricide(sockfd);
	exit(EXIT_FAILURE);
      }
    }
    if(nread == -1){
      perror("Failed to read from terminal");
      exit(EXIT_FAILURE);
    }
    parricide(sockfd);
    exit(EXIT_SUCCESS);
    break;
  default:
    while((nread = read(sockfd, messageBuffer, 512)) > 0){
      if(write(STDIN_FILENO, messageBuffer, nread) < 0){
	perror("Write Failed");
	kill(pid, SIGKILL);
	exit(EXIT_FAILURE);	
      }
    }
    
    if(nread == -1){
      perror("Failed to read from terminal");
      exit(EXIT_FAILURE);	
    }
    
    kill(pid, SIGKILL);
    while(waitpid(-1, NULL, WNOHANG) > 0);
    exit(EXIT_SUCCESS);
    break;
  }	
  
  
  //  \    /\  ******************************************************  /\    /
  //   )  ( ')  LOOK CARVER I DID NOT DELETE OLD CODE. #ForTheFuture  (' )  (
  //  (  /  )  ******************************************************  (  \  )
  //   \(__)|                   (THE CATS DEMAND IT)                   |(__)/
  //write(sockfd, "ls -l; exit\n", 12);
  //  while(1){
  //      buffer = readSocket(sockfd);
  //      printf("%s\n", buffer);
  //      while(1){} 
  //}
  
  close(sockfd);
  exit(EXIT_SUCCESS);
}


void parricide(int sockfd){
  if(getppid() != 1){
    kill(getppid(), SIGKILL);
    close(sockfd);
  }
}

char* readSocket(int sockfd){
  char * buff;
  
  if((buff = readline(sockfd)) == NULL){
    perror("Problem reading from socket\n");
    exit(EXIT_FAILURE);
  } 
  return buff;
}
