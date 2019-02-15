//Candice Sandefur
#define _XOPEN_SOURCE 600
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pty.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "readline.c"


#define PORT 4070
#define SECRET "cs407rembash"
#define LEN 1000

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n"; 
const char* error = "<error>\n";
const char* secret = "<" SECRET ">\n";
const char* exiting = "exit";

pid_t e_pid, t_pid;


void handle_client(int client_sockfd);
char* checkSocket(int client_sockfd);
int greeting(int client_sockfd);
int createPTY(int *slave_fd);
static void handle(int sigNum);
void daycare();

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
  
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  
  while(1) {    
    client_len = sizeof(client_address);
    if((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len)) < 0){
      perror("Accept has failed.");
      close(client_sockfd);
    }
    
    switch(pid = fork()){
    case -1:
      perror("Failed to fork");
      exit(EXIT_FAILURE);
      break;
    case 0:
      close(server_sockfd);
      greeting(client_sockfd);
      handle_client(client_sockfd);
      exit(EXIT_SUCCESS);
      break;
    default:
      close(client_sockfd);
    }
  }
  exit(EXIT_SUCCESS);
}

void handle_client(int client_sockfd)
{
  int master_fd, slave_fd, nread;
  char message[MAX_INPUT];
  
  if((master_fd = createPTY(&slave_fd)) == -1){
    perror("Failed to create PTY");
    exit(EXIT_FAILURE);
    
  }
  
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGCHLD);
  action.sa_flags = SA_RESTART;
  
  if(sigaction(SIGCHLD, &action, NULL) == -1){
    perror("Sigaction had failed");
  }
  
  switch(e_pid = fork()){
  case -1:
    perror("Fork has failed");
    exit(EXIT_FAILURE);
    break;
  case 0:
    setsid();
    close(client_sockfd);
    close(master_fd);
    if((dup2(slave_fd, STDIN_FILENO) | dup2(slave_fd, STDOUT_FILENO) |  dup2(slave_fd, STDERR_FILENO)) < 0) {
      perror("Dup has failed");
      exit(EXIT_FAILURE);
    }
    close(slave_fd);
    execlp("bash", "bash", NULL);
    perror("Bash failed to execute.\n");
    exit(EXIT_FAILURE);
    break;
  default:
    switch(t_pid = fork()){
    case -1:
      perror("Failed to fork");
      exit(EXIT_FAILURE);
      break;
    case 0:
      close(slave_fd);
      while((nread = read(master_fd, message, MAX_INPUT)) > 0){
        if(write(client_sockfd, message, nread) < 0){
	  perror("Failed to write");
	  exit(EXIT_FAILURE);
        }
      }
      exit(EXIT_SUCCESS);
      break;
    default:
      close(slave_fd);
      while((nread = read(client_sockfd, message, MAX_INPUT)) > 0){
        if(write(master_fd, message, nread) < 0){
	  perror("Failed to write");
	  daycare();
        }
      }
      daycare();
      exit(EXIT_SUCCESS);
      break;
    }
    
  }
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

int greeting(int client_sockfd){
  char *message_buffer;
  
  if(write(client_sockfd, rembash, strlen(rembash)) < 0){
    perror("Failed to write to the socket1");
    return -1;
  }
  
  message_buffer = checkSocket(client_sockfd);
  
  if(strcmp(message_buffer, secret) != 0){
    write(client_sockfd, error, strlen(error));
  }
  
  if(write(client_sockfd, ok, strlen(ok)) < 0){
    perror("Failed to write to the socket");
    return -1;
  }
  
  free(message_buffer);
  
  return 0;
}

int createPTY(int *slave_fd){
  int master_fd;
  char slaveName[LEN];
  char *tempSlaveName;
  
  if((master_fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1)
    return -1;
  
  if(unlockpt(master_fd) == -1) {
    close(master_fd);
    return -1;           
  }
  
  if((tempSlaveName = ptsname(master_fd)) == NULL){
    close(master_fd);
    return -1;
  }
  
  if(strlen(tempSlaveName) < LEN){
    strcpy(slaveName, tempSlaveName);
  }else{
    close(master_fd);
    return -1;
  }
  
  if((*slave_fd = open(slaveName, O_RDWR)) == -1)
    return -1;
  
  return master_fd;
}

static void handle(int sigNum){
  daycare();
}

void daycare(){
  int status;
  int exitCode;
  kill(t_pid, SIGKILL);
  kill(e_pid, SIGKILL);
  
  if(waitpid(-1, &status, WNOHANG) > 0){
    exitCode = !(!WEXITSTATUS(status) && WIFEXITED(status));
    exit(exitCode);
  }  
}

/*
 *                                                                                ::::::::::::::::                                
                                                                             ::::::             :::::::                           
                                                                          ::::                        ::::                        
                                                                       ::::                              ::::                     
                                                                     :::                                    :::                   
                                                                   :::                                        :::                 
      Why are PC's like air conditioners?                          ::                                           :::                
                                                                  ::                                              :::              
                                                                ::                                               ::::             
                                                              :::                                                  :::            
                                                             :::                                                    ::            
                                                             ::                                                      ::           
       They stop working properly if you open Windows.      ::                                                        ::          
                                                           ::                                                         :::         
                                                           ::                                                          ::         
                                                          ::    :::::.                                                  :         
                                                          ::          :                                                 ::        
                                                          :  :::::::                                                     ::        
                                                         :: :::::::::                                                    ::       
                                                         :::::::::::::                   :::..                           ::       
                                                         :::  ::::::::                        ::.                        ::       
                                                         :::  ::::::::              ::::::::    ::.                      ::       
                                                      :::: : :::::::::            :: ::::::::     ::.               :    ::       
                                                     ::    :  :::::::            :: ::::::::::      :                ::::::::     
                                                    ::::    :  :::::             :   ::::::::::                             ::    
                                                    :::            ..:           :   ::::::::::                    ::        ::   
                                                     ::         : .::::::        : :::::::::::                      :::      ::   
                                                     ::         :  :::::::       :  ::::::::::                               ::   
                                                      :         ::   :            :  ::::: ::                    :          ::    
                                                      ::::::::   :::::      .:         ::::                       ::       ::     
          ::::::::                                     ::   :::      ::::::::              :::                       ::::::::      
         ::      ::               ::::::             ::     :                               :::                     ::            
       .::       ::              ::::: :: :         :::    ::                                 ::                   ::             
     .::        .::             :::      ::        ::    ::::      :::::                       ::                  ::             
    .:         .:          :::::::        ::       ::       :::   ::: ::::                      ::                ::              
  .::        .:           :: ::  ::      ::::::::::::   :::::::::::::    ::::                    ::               ::              
  ::         :           :::     ::::::::::       ::::::::      :::         ::               :.  ::               ::              
 ::         .:           ::    :::      ::::                      ::         :::           .  ::::               .::              
 ::         :             ::  ::           ::                      ::          ::           :  :                .:                
::          ::            ::  :             ::                      :           ::          .::             .::::                 
::          ::             ::                ::                     ::           ::         ::            .::                     
::           ::.           ::                 ::                    ::            ::::::   ::        .:::::                       
::             ::.         ::                  :                   ::                  :::::::::::::::                            
 ::              ::::::::::::                  ::                :: :                     :::                                     
 ::                        :                    :             :::                       :::                                       
  ::                       :                    :        ..::::                        ::                                         
   ::                      :                    :  ::::::                            :::                                          
    ::                     :                     :                                 .::                                            
     :::                    :                                                   ..::                                              
      :::                                                                      .::                                                
        ::::                                                                ..::                                                  
           ::::                :                                          ..::                                                    
             :::::::            ::                                     ..::                                                       
                   :::::::::      ::                                ::::                                                          
                          :::::::::::::::                     :::::::                                                             
                                        ::::::::::    ::::::::::                                                                  
                                                ::::::::::
*/
