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
#include <libgen.h>
#include <linux/limits.h>
#include <signal.h>
#include <termios.h>
#include "readline.c"

#define PORT 4070
#define SECRET "cs407rembash"

char* readSocket(int sockfd);
int greeting(int sockfd);
void setMode(void);
void resetMode(void);
static void handle(int sigNum);
void daycare();

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n";
const char* error = "<error>\n";
const char* secret = "<" SECRET ">\n";

struct termios att;
struct termios attSave;   

int status;
int exitCode;
pid_t pid;

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
  
  
  char termBuffer[MAX_INPUT];
  
  greeting(sockfd);
  setMode();
  
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGCHLD);
  action.sa_flags = SA_RESTART | SA_NOCLDWAIT;
  
  sigaction(SIGCHLD, &action, NULL);
  
  signal(SIGPIPE, SIG_IGN);
  
  int nread;
  switch(pid = fork()){
  case -1:
    perror("Fork failed");
    exit(EXIT_FAILURE);
    break;
  case 0:
    while((nread = read(STDIN_FILENO, termBuffer, MAX_INPUT)) > 0){
      if(write(sockfd, termBuffer, nread) < 0){
        perror("Write Failed");
        exit(EXIT_FAILURE);
      }
    }
    close(sockfd);
    exit(EXIT_SUCCESS);
    break;
  default:
    while(((nread = read(sockfd, termBuffer, MAX_INPUT)) > 0)){
      if(write(STDOUT_FILENO, termBuffer, nread) < 0){
	perror("Write Failed");
	daycare();
	exit(EXIT_FAILURE);	
      }
    }
    daycare();
    close(sockfd);
    exit(EXIT_SUCCESS);
    break;
  }	
  
  close(sockfd);
  exit(EXIT_SUCCESS);
}


char* readSocket(int sockfd){
  char * buff;
  
  if((buff = readline(sockfd)) == NULL){
    perror("Problem reading from socket\n");
    exit(EXIT_FAILURE);
  } 
  return buff;
}

int greeting(int sockfd){
  char *messageBuffer;
  
  messageBuffer = readSocket(sockfd);
  
  if(strcmp(messageBuffer, rembash) != 0){    
    perror("Problem with server response\n");
    return -1;
  }
  
  if(write(sockfd, secret, strlen(secret)) < 0){
    perror("Failed to write to the socket");
    return -1;
  }
  
  messageBuffer = readSocket(sockfd);
  
  if(strcmp(messageBuffer, ok) != 0){
    perror("Problem with server response\n");
    return -1;
  }
  
  return 0;
}


void setMode(void){
  if(!isatty (STDIN_FILENO)){
    perror("Not a terminal");
    exit(EXIT_FAILURE);
  }
  
  tcgetattr(STDIN_FILENO, &attSave);
  atexit(resetMode);
  
  tcgetattr (STDIN_FILENO, &att);
  att.c_lflag &= ~(ICANON | ECHO);
  att.c_cc[VMIN]=1;
  att.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &att);
}

void resetMode(void){
  tcsetattr(STDIN_FILENO, TCSANOW, &attSave);
}

static void handle(int sigNum){    
  daycare();
}

void daycare(){
  kill(pid, SIGKILL);
  if(waitpid(-1, &status, WNOHANG) > 0){
    exitCode = !(!WEXITSTATUS(status) && WIFEXITED(status));
    exit(exitCode);
  }
}

/*
 * 
                                                                   ::::::::::                       
                                                     ::::::::::::::::         :                     
                                        ::::::::::::::             ::::         :                   
What do houses and Microsoft         :::            :                 :::        :                  
Windows have in common?            :::                :                 :::       :                 
                                  ::                   :                  ::       :                
                                :::                     :                  :       ::              
                               :::                       :                  :       :               
                              :::                         :                         :               
Bugs come in through open    :::                                                    ::              
Windows.                     ::       ::::::::::                                     :              
                            ::      :::::::::::::::                                 ::              
                            ::     :::::      :::::::                                ::             
                           :::    :::::         ::::::                        :::       ::          
                           ::     ::::           ::::::                          :::      ::        
                           ::    : :::            ::::::                             ::     :       
                           ::    :::::           ::::::::                               :    ::     
                          :::    :::::           ::::::::                                 :    :..   
                          :::    ::::::         ::::::::::                                        :
                           ::    : ::::::    :::     :::::                                       ::
                           ::    : :::::::::::::      ::::                                        : 
                           ::     : :::    ::::       ::::                      ::::              : 
                            :     :::::     :::       :::                      :....:            :: 
                            :      :  ::    ::::    :::::                      :..::             :  
                             :   :  :  :::::::::::::::::                       :::              :   
                              :::     :: :::::::::::::                                         :    
                               ::       :::    :: ::                                          :     
                               :            :::::            :                  ::::         :      
                               :                             :      .....      :     :::    ::       
                                :.                           ::::::     :::::      :  ::::         
                                  ::                                              :                
                                    ::.                    ..:::::::::.........:::                  
                                       ::::::::::::::::::::::     ::::                              
                                        ::::::                      ::::                            
                                       ::                             : :                           
                                      ::                               :  :                         
                                     ::                                 :  :                        
                                    :                                    :  :  .:::::...             
                                    :                                    :   ::         :            
                                   :                                      :      ......  :           
                                   :                                       :    ::    :.:.:          
                                  :                                         ::          : :         
                                  :                                         :                       
                                   :      ::::::::...:::.                    :             ...:::   
                                   ::                    :.                  :           .:    :...    
                                    :::          ......    :                  ::.       .:    ::   .:   
                                  :::: :::::::::::     :.:.:                    :::.   :    ::   :  
                                 :::                     : :                        : :     :    :   
  ....::::...                 .:: ::                                                 :         :    
:::          ::::::::..    .:    :        :::              .                          :       :     
::                    ::. ::      :    .::                  :.                :              .:     
 :::                     :        :   .:                      :               :              :     
    :::                           :  :            :::::    :: ::              :              :      
       ::                         : :           ::     : ::     :             :              :      
         :                        :::    :::::  :       ::       :            :              :      
          :                       ::   :::    ::        :        :           ::              :      
            :                     ::   ::      ::                 :          :              ::      
             :                     :   ::       :                 :         ::::..          ::      
              :                    :   ::                         :        ::    :::...   .::       
               ::.                  :: ::                         :      :::          ::::          
                 :..                 :::::                       :     :::                          
                  :::.                 ::::                      :  ::::                            
                     :::...            :::::                    ::::::                              
                        :::::::::::::::::::::                  ::                                   
                                            :::               ::                      
                                             :::::         :::          
                                                :::::::::::::                      
                                                    ::::::

*/
