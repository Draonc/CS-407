//Candice Sandefur
#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 199309L
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
#include <sys/epoll.h>
#include <time.h>
#include <pthread.h>
#include <syscall.h>

#include "readline.c"


#define PORT 4070
#define SECRET "cs407rembash"
#define LEN 1000
#define READ 4096
#define MAX_NUM_CLIENTS 100
#define MAX_NUM_EVENTS 100

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n"; 
const char* error = "<error>\n";
const char* secret = "<" SECRET ">\n";


int global_efd;
pid_t e_pid, t_pid;
int client_fds[MAX_NUM_CLIENTS * 2 + 5];
pid_t subprocess_fds[MAX_NUM_CLIENTS * 2 +5];
int server_sockfd;


//Sets up information for the client
void * handle_client(void * client_pt);
//Preforms handshake
int greeting(int client_sockfd);
//Creates the PTY
int createPTY(char *slaveName);
//Handles the actions upon getting SIGALRM
static void handle(int sigNum);
//Creates the socket
int socketCreation();
//Epoll wait loop
void * startRoutine();

int main()
{  
  int client_sockfd;
  socklen_t client_len;
  struct sockaddr_in client_address;
  int *client_pt; 
  pthread_t pthread_id;
  
  if((global_efd = epoll_create(EPOLL_CLOEXEC)) == -1){
    perror("Epoll creation failed.\n");
    exit(EXIT_FAILURE);  
  }
 
  fcntl(global_efd,F_SETFD,FD_CLOEXEC);
  
  if((server_sockfd = socketCreation()) == -1){
    perror("Socket Creation failed.\n");
    exit(EXIT_FAILURE);
  }
  
  fcntl(server_sockfd,F_SETFD,FD_CLOEXEC);
  
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  
  if(pthread_create(&pthread_id, NULL, &startRoutine, NULL) != 0){
    perror("Failed to create start thread");
    exit(EXIT_FAILURE);
  }
  
  while(1) {    
    client_len = sizeof(client_address);
    if((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len)) < 0){
      perror("Accept has failed.");
      close(client_sockfd);
    }
    
    fcntl(client_sockfd,F_SETFD,FD_CLOEXEC);
    
    client_pt =  malloc(sizeof(int));
    *client_pt = client_sockfd;
    
    if(pthread_create(&pthread_id, NULL, &handle_client, client_pt)){
      perror("Failed to create handle client thread.\n");
    }
  }
}

//Sets up information for the client
void * handle_client(void *client_pt)
{
  int master_fd, slave_fd, client_sockfd;
  struct epoll_event eevents[2];
  char slaveName[LEN];
  
  client_sockfd = *(int*) client_pt;
  free(client_pt);
  pthread_detach(pthread_self());
  
  //Handshake
  if(greeting(client_sockfd) < 0){
    perror("Handshake has failed");
    close(client_sockfd);
    return NULL;
  }  
  
  //Create PTY
  if((master_fd = createPTY(slaveName)) == -1){
    perror("Failed to create PTY");
    exit(EXIT_FAILURE);
  }

  fcntl(master_fd,F_SETFD,FD_CLOEXEC);
  
  
  switch(e_pid = fork()){
  case -1:
    perror("failed to fork.\n");
    exit(EXIT_FAILURE);
    break;
  case 0:
    if(setsid() == -1){
      perror("Failed to start new session.");
      exit(EXIT_FAILURE);
    }
    if((slave_fd = open(slaveName, O_RDWR)) == -1){
      perror("Failed to open slave.");
      exit(EXIT_FAILURE);
    }
    fcntl(slave_fd,F_SETFD,FD_CLOEXEC);
    if((dup2(slave_fd, STDIN_FILENO) | dup2(slave_fd, STDOUT_FILENO) |  dup2(slave_fd, STDERR_FILENO)) < 0){
      perror("Dup has failed.\n");
      exit(EXIT_FAILURE);}
    execlp("bash", "bash", NULL);
    perror("Bash failed to execute.\n");
    exit(EXIT_FAILURE);
    break;
  default:
    client_fds[client_sockfd]= master_fd;
    client_fds[master_fd] = client_sockfd;
    subprocess_fds[client_sockfd] = e_pid;
    subprocess_fds[master_fd] = e_pid;
    
    eevents[0].events = EPOLLIN | EPOLLET;
    eevents[0].data.fd = client_sockfd;
    eevents[1].events = EPOLLIN | EPOLLET;
    eevents[1].data.fd = master_fd;
    
    epoll_ctl(global_efd, EPOLL_CTL_ADD, client_sockfd, eevents);
    epoll_ctl(global_efd, EPOLL_CTL_ADD, master_fd, eevents + 1);
    
    return NULL;
  }
}

//Handshake
int greeting(int client_sockfd){
  timer_t alarm;
  struct sigevent sevent;
  struct itimerspec limit = {{0,0}, {10, 0}};         
  
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGALRM);
  
  if(sigaction(SIGALRM, &action, NULL) == -1){
    perror("Sigaction had failed");
  }
  
  memset(&sevent, 0, sizeof(sevent));
  sevent.sigev_notify = SIGEV_THREAD_ID;
  sevent._sigev_un._tid = syscall(SYS_gettid);
  sevent.sigev_signo = SIGALRM;
  
  char *message_buffer;
  
  if(write(client_sockfd, rembash, strlen(rembash)) < 0){
    perror("Failed to write to the socket1");
    return -1;
  }
  
  //Create timer and set time to go off
  timer_create(CLOCK_REALTIME, &sevent, &alarm);
  timer_settime(alarm, 0, &limit, NULL);
  
  if((message_buffer = readline(client_sockfd)) == NULL){
    perror("Failed to read from socket");
    return -1;
  }
  
  //Delete the timer
  timer_delete(alarm);
  
  if(strcmp(message_buffer, secret) != 0){
    write(client_sockfd, error, strlen(error));
  }
  
  if(write(client_sockfd, ok, strlen(ok)) < 0){
    perror("Failed to write to the socket");
    return -1;
  }
  
  return 0;
}

//Create the PTY
int createPTY(char *slaveName){
  int master_fd;
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
  
  return master_fd;
}


//Handling
static void handle(int sigNum){
}

//Creates the Socket
int socketCreation(){
  socklen_t server_len;
  int server_sockfd;
  
  struct sockaddr_in server_address;
  
  if((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    perror("Failed to create socket");
    return -1;
  }
  int i=1;
  setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
  
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(PORT);
  server_len = sizeof(server_address);
  
  if(bind(server_sockfd, (struct sockaddr *)&server_address, server_len) == -1){
    perror("Failed to bind socket");
    return -1;
  }
  
  if(listen(server_sockfd, 5) == -1){
    perror("Socket failed to listen.\n");
    return -1;
  }
  
  return server_sockfd;
}


//Epoll wait loop
void * startRoutine(){
  struct epoll_event events[MAX_NUM_EVENTS];
  int eventsReady;
  
  while (1){
    eventsReady = epoll_wait(global_efd, events, MAX_NUM_EVENTS, -1);
    if (eventsReady == -1){
      perror("Failure in Epoll wait.\n");
      exit(EXIT_FAILURE);
    }
    
    for (int i=0; i < eventsReady; i++){
        if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)){
	close(client_fds[events[i].data.fd]);
	close(events[i].data.fd);
        } else if (events[i].events & (EPOLLIN)){
	static char messageReady[READ];
	static int nread;
	if((nread = read(events[i].data.fd, messageReady, READ)) < 0){
	  perror("Failed to read");
	}
	if(write(client_fds[events[i].data.fd], messageReady, nread) < 0){
	  perror("Failed to write");
	}	
      }
    }
  }  
}

/*

                                                     :::::::::                                     
                                                    ::        ::                                    
                                                    ::::::::: :::                                  
                                                      :  :       ::                                 
                                                       :   :      :                                 
                                          ::::::::::     :   :   ::                                 
                                     ::::::       :::::::::  :  ::                                  
                                  :::                    :   : :::                                  
                                 ::                   ::::::::::: ::                                
                               ::::::...           ::            :::::                              
                              ::        :.                          ::::                            
                               :::::      :.                          : ::                          
                                 :mm::::    :.                            ::        ;;              
                                ::mmmmm:::    :...                          ::   ::::;              
                                :mmmmmmmmm:::     :....                        ::::mm:               
                               ::mmmmmmmmmmm:::       ::::..                   ::mmm:               
                               :::mmmmmmmmmmmm:::           :...                ::mm:               
                               ::mmmmmmm::::::mm:::::::: :     ::..              :mm:               
                               :mmmmmm::#####:::mmmmm:::::::       ::::........  ::::               
                              ::mmmmm::########::mmmmmmmmmm:::                 ::::::               
What animals make the best    :mmmmm:::###### ##::mmmmmmmmmmmm::::::              : ::               
pets?                         :mmmmm: :#####  ###:mmmmmmmmmmmmm:::::::::::::::::::::                
                              :mmmmm: :#########:mmmmmmmmmmmmmm::#####::mmmmmmmmmm::                
Cats they are purrr-fect.     ::mmmm:: :#######:mmmmmmmmmmmmm::########::mmmmmmmmm:       ::::      
                              ::mmmmm:: ::####:mmmmmmmmmmmmmm:### ######::mmmmmmm:::   :::::::::::   
                               ::mmmmm:::::::mmmmmmmmmmmmmmmmm:##  ####:::mmmmmm::: :::mmmmmmmmmm::  
                               ::mmmmmmmmmmmmmmmmm:mmmm:mmmmmm:#######: ::mmmmm::::::mmmmmmmmmmmm:: 
                                :::mmmmmmmmmmm::mmm:mm:mmmmmmmm:#####:  :mmmmmm:::mmmmmmmmmmmmmmm:: 
                                  ::mmmmmmmmmmm:::mm:mmmm::mmmmmm:::   :mmmmmm::mmmmmmmmmmmmmmmmm:: 
                               ::::::::mmmmmmmmmmmmmmm::::mmmmmmmm:::::mmmmmm::mmmmmmmmm::::::mmm:: 
                           :::::::::: ::::mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm::mmmmmmmmm:::  ::mmm::  
                         :::: :::        ::::mmmmmmmmmmmmmmmmmmmmmmmmmmmm:::mmmmmmm:::     ::m:::   
                       :::   ::          ::mm:::::mmmmmmmmmmmmmmmmmmm:::::mmmmmm::::      :::       
                      :::   ::          ::mmmmmmmm:::::::::::::::::::mmmmmmmm::::::::               
                     ::    :            ::mmmmmmmmmmm::mmmmmmmmmm:::::::::::::::::::::::            
                    ::    :             ::mmmmmmmmm::: :mmmmmmmmm::    :::         ::  ::::         
                   ::                   ::mmm:mmm:::: :mmmm: mmm::       :::              :::       
                  ::                     :::m:m:::::::mmmm:mmmm::         :::              :::      
                  ::                  :::  :::::    :::mmm:mm:::           :::               ::     
                 ::                  ://::           :::::::::     :        ::                :::   
                 ::                 :////::                       :::        :                 ::   
                ::                 :://////::                    :///::      :                  ::  
                ::                ::///:////:                   ://///::                        ::  
                ::               ::///:::///::                 :://////::                        :: 
                ::               ::://: :////::     ::        ::///::///::                       :: 
                ::                 ::::  ::::::    ::::      ::///:::////:                       :: 
                ::                                :://::     :::::: ::////:                      :: 
                 ::               ::             ::////::             :::::                      :: 
                 :::               : :::        ::::::::::                                       :: 
                  ::               ::///::                                                      ::  
                   ::      :        :://::   ::..                                               ::  
                    :::     :        ::/:::  :///:::::::                                 :     ::   
                      :::   :::        :://:::////////::    ::::::::::::                :     :::   
                        :::  :::         ::://////////:::::::///////:::                ::    ::     
                          ::::::::         :::://////////////////::::                 ::   :::      
                               :::::           ::::::::::::::::::         :         :::  ::::       
                                  ::::                                  ::        :::::::::         
                                     :::::                          ::::       :::::::::            
                                        ::::::                  ::::::   :::::::                    
                                            :::::::::::::::::::::::::::::::                         
*/
