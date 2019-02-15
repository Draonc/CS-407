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
#include <sys/timerfd.h>

#include "readline.c"
#include "tpool.h"

#define PORT 4070
#define SECRET "cs407rembash"
#define LEN 1000
#define READ 512
#define MAX_NUM_CLIENTS 100
#define MAX_NUM_EVENTS 100

const char* rembash = "<rembash>\n";
const char* ok = "<ok>\n";
const char* error = "<error>\n";
const char* secret = "<"SECRET">\n";

int epoll_fd;
int epoll_timerfd;
int client_fds[MAX_NUM_CLIENTS * 2 + 5];
int timer_fds[MAX_NUM_CLIENTS + 5];
int server_sockfd;
pthread_mutex_t slab_op;

struct epoll_event wait_events[MAX_NUM_EVENTS];

enum state{new, created, unwritten, terminate};

typedef struct client{
    enum state state;
    char unwritten_buff[READ + 1];
    int unwritten;
    int timerfd;
    int sockfd;
    struct client *next;
    struct client *prev;
}client;

client *slab;

//Sets up client stuff
void * handle_client(int client_sockfd);
//First half of protocol
int nod(int client_sockfd);
//Second half of protocol
int whisper(int client_sockfd);
//Creates the PTY
int createPTY(char *slaveName);
//Creates the socket
int socketCreation();
//Epoll wait loop
void *loop();
//Performs the task of the thread
void task(int fd);
//Places the client into the slab
void allocation(int fd);
//Removes the client from the slab
void closeClient(int fd);

void timerloop();

int main(){
    if(tpool_init(task) < 0)
        perror("Failed to initalize the thread pool");
    
    slab = malloc(sizeof(client) * (MAX_NUM_CLIENTS * 2 + 5));
    
    //Ignore pipe and children
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    
    pthread_mutex_init(&slab_op, NULL);
    
    //Creates epoll
    if((epoll_fd = epoll_create(EPOLL_CLOEXEC)) == -1){
        perror("Epoll creation failed.\n");
        exit(EXIT_FAILURE);
    }
    
    fcntl(epoll_fd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
    
    //Create timer epoll
    if((epoll_timerfd = epoll_create(EPOLL_CLOEXEC)) == -1){
        perror("Epoll creation failed.\n");
        exit(EXIT_FAILURE);
    }
    
    fcntl(epoll_timerfd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
    
    struct epoll_event timerevent;
    timerevent.events = EPOLLIN | EPOLLONESHOT;
    timerevent.data.fd = epoll_timerfd;
    
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, epoll_timerfd, &timerevent);
    
    //Create socket
    if(socketCreation() == -1){
        perror("Socket Creation failed.\n");
        exit(EXIT_FAILURE);
    }
    
    fcntl(server_sockfd,F_SETFD , FD_CLOEXEC | O_NONBLOCK);
    
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = server_sockfd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sockfd, &event);
    
       //Starts the epoll wait loop   
    loop();
    exit(EXIT_FAILURE);
}

//Sets up client stuff
void *handle_client(int client_sockfd){
    int master_fd, slave_fd;
    char slaveName[LEN];
    
    //Create PTY
    if((master_fd = createPTY(slaveName)) == -1){
        perror("Failed to create PTY");
        exit(EXIT_FAILURE);
    }
    
    //Set master to nonblockint and close on exec.
    fcntl(master_fd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
      
    switch(fork()){
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
        fcntl(slave_fd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
        if((dup2(slave_fd, STDIN_FILENO) | dup2(slave_fd, STDOUT_FILENO) |  dup2(slave_fd, STDERR_FILENO)) < 0){
            perror("Dup has failed.\n");
            exit(EXIT_FAILURE);}
        execlp("bash", "bash", NULL);
        perror("Bash failed to execute.\n");
        exit(EXIT_FAILURE);
        break;
    default:
        //Places fds into fd array
        client_fds[client_sockfd]= master_fd;
        client_fds[master_fd] = client_sockfd;
        
        //Add pty to the epoll
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLONESHOT;
        event.data.fd = master_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, master_fd, &event);
    
        return NULL;
    }     
}

//Let the client know you see it with a quick nod.
int nod(int client_sockfd){
    struct sigevent sevent;
    struct itimerspec limit = {{0,0}, {10, 0}};
  
    memset(&sevent, 0, sizeof(sevent));
    sevent.sigev_notify = SIGEV_THREAD_ID;
    sevent._sigev_un._tid = syscall(SYS_gettid);
    sevent.sigev_signo = SIGALRM;

    //Send rembash to the client
    if(write(client_sockfd, rembash, strlen(rembash)) < 0){
        perror("Failed to write to the socket");
        close(client_sockfd);
        return -1;
    }
    
    //Create timer fd set its time and add it to the timer epoll
    slab[client_sockfd].timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    timerfd_settime(slab[client_sockfd].timerfd, 0, &limit, NULL);
    
    //Sets the timerfd to nonblocking and to close on exec
    fcntl(slab[client_sockfd].timerfd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
    
    //Assigns the client fd to loctiaon in timer_fds
    timer_fds[slab[client_sockfd].timerfd] = client_sockfd;
    
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = slab[client_sockfd].timerfd;
    epoll_ctl(epoll_timerfd, EPOLL_CTL_ADD, slab[client_sockfd].timerfd, &event);
    
    return 0;
}
    
//Listen for the clients secret to know you can give them the info.
int whisper(int client_sockfd){
    char *message_buffer;
    long long timer_exp = 0;
    
    //Check the timer to see if it expired right before the message
    read(slab[client_sockfd].timerfd, &timer_exp, 8);
    
    //If timer is expired close the timerfd and the client fd
    if(timer_exp != 0){
        close(slab[client_sockfd].timerfd);
        close(client_sockfd);
        return -1;
    }
    
    //Listen
    if((message_buffer = readline(client_sockfd)) == NULL){
        perror("Failed to read from socket");
        close(slab[client_sockfd].timerfd);
        close(client_sockfd);
        return -1;
    }
    
    //Compare
    if(strcmp(message_buffer, secret) != 0){
        write(client_sockfd, error, strlen(error));
    }
    
    
    //Ok
    if(write(client_sockfd, ok, strlen(ok)) < 0){
        perror("Failed to write to the socket");
        close(slab[client_sockfd].timerfd);
        close(client_sockfd);
        return -1;
    }
    
    //Set state to created because I know you now
    slab[client_sockfd].state = created;
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

//Creates the Socket
int socketCreation(){
    socklen_t server_len;
  
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
       return 0;
}

//Epoll wait loop
void *loop(){
    int eventsReady;
    while((eventsReady = epoll_wait(epoll_fd, wait_events, MAX_NUM_EVENTS, -1)) >= 0){
        for (int i=0; i < eventsReady; i++){
            if (wait_events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)){
                //Welp something went wrong so we need to close
                close(client_fds[wait_events[i].data.fd]);
                close(wait_events[i].data.fd);
            }else if (wait_events[i].events & (EPOLLIN)){
                //Add fd to queue    
                tpool_add_task(wait_events[i].data.fd);
            }
        }
    }
    exit(EXIT_FAILURE);
}

//Performs the task of the thread
void task(int fd){
    struct epoll_event event; 
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = fd;
    int client_sockfd;
    
    if(fd == server_sockfd){
        //If you are the server we need to accept clients
        errno = 0;
        socklen_t client_len;
        struct sockaddr_in client_address;
        
                
        client_len = sizeof(client_address);
        
        //While loop to make sure if many clients try to connect at same time I accept them all
        while((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len)) != -1){
            //Set client fd to nonblocking and close on exec
            fcntl(client_sockfd,F_SETFD,FD_CLOEXEC | O_NONBLOCK);
            
            //Set the state of the client to new so we know we need to check secret
            slab[client_sockfd].state = new;
            
            //Set the client fd in the client object
            slab[client_sockfd].sockfd = client_sockfd;
            
            //Add the client to the main epoll
            struct epoll_event clientEvent;
            clientEvent.events = EPOLLIN | EPOLLONESHOT;
            clientEvent.data.fd = client_sockfd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sockfd, &clientEvent);
        
            //Let the client know you see it
            if(nod(client_sockfd) < 0){
                perror("Handshake has failed1");
            }
        }
        
        //If not EAGAIN then there is a problem and client needs to close
        if(!(errno == EAGAIN) && !(errno == 0) ){
           perror("Accept has failed.");
           close(client_sockfd);
        }
        
        //Readd the server to the epoll so it can wait for more clients
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, server_sockfd, &event);
        return;
    }else if(fd == epoll_timerfd){      
        //If the timer epoll is going off a timer is ringing      
            int timersReady;
            struct epoll_event timer_events[MAX_NUM_EVENTS];
            long long timer_exp = 0;
            
            //Goes through the timers to see which one or ones are ringing and close the timerfd and the clientfd
            timersReady = epoll_wait(epoll_timerfd, timer_events, MAX_NUM_EVENTS, -1);
            for (int i=0; i < timersReady; i++){
                read(timer_events[i].data.fd, &timer_exp, 8);
                if ((timer_events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) || (timer_exp != 0)){                    
                    close(timer_fds[timer_events[i].data.fd]);
                    close(timer_events[i].data.fd);
                    
                }
            }
            
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    }else if(slab[fd].sockfd == fd && slab[fd].state == new){
        //If the fd is a client and new then we need to check the secret
        
        
        //See if they know the secret
        if(whisper(slab[fd].sockfd) < 0){
           perror("Handshake has failed2");
        }
        
        //close the timer because client shared secret in time
        close(slab[fd].timerfd);
        
        //Place the client on the slab
        allocation(slab[fd].sockfd);
        
        //Preform client stuuf
        handle_client(slab[fd].sockfd);
        
        //Modify the fd in epoll
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
        return;
    }else if(slab[fd].state == unwritten){
        //If partial writes happened
        int targetfd = client_fds[fd];
        
        //Write what is in the client buffer to target fd
        int written = write(targetfd, slab[fd].unwritten_buff, slab[fd].unwritten);
        slab[fd].unwritten -= written;
        slab[targetfd].unwritten -= written;
        
        
        //If there is nothing left to write in the buffer change the state
        if(slab[fd].unwritten == 0){
            slab[fd].state = created;
            slab[targetfd].state = created;
        }
        
        //Modify the fd in epoll
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
       return;
        
    }else{
        //If not new and not partial write
        errno = 0;
        int nread;
        int targetfd = client_fds[fd];
        char buff[READ + 1];
        buff[READ] = '\0';
        int written;        
        int clientClosed = 0;
        
        //Read from the fd
        fflush(stdout);
        if(((nread = read(fd, buff, READ)) == 0) && errno != EAGAIN){
            closeClient(fd);
            clientClosed = 1;
            close(fd);
            close(targetfd);
        }
        
        //If client not closed write to the target fd
        if(!clientClosed && ((written = write(targetfd, buff, nread) <= 0) && errno != EAGAIN)){
            closeClient(fd);
            close(fd);
            close(targetfd);
                    
        }else if(errno == EAGAIN){
            //Partial write place the unwritten data in the client buffer and set the state to unwritten
            strncpy(slab[fd].unwritten_buff, &buff[written], nread - written);
            strncpy(slab[fd].unwritten_buff, slab[targetfd].unwritten_buff, nread - written);
            slab[fd].unwritten = nread - written;
            slab[targetfd].unwritten = nread - written;
            slab[fd].state = unwritten;
            slab[targetfd].state = unwritten;
        }
        //Modify the fd in epoll
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
        return;
    }
}

//Places the client object into the slab
void allocation(int fd){
        //Locks so only one object can be added at a time
        pthread_mutex_lock(&slab_op);
        static client *prevLocation = NULL;
        
        //Connects the clients like a linked list
        client *location = &slab[fd];        
        location->prev = prevLocation;
        
        if(prevLocation != NULL)
            prevLocation->next = location;
        
        prevLocation = location;
        location->next = NULL;
        
        //unlocks the mutex
        pthread_mutex_unlock(&slab_op);
}


//Removes the client object from the slab
void closeClient(int fd){
    //Locks so only one object can be removed at a timer
    pthread_mutex_lock(&slab_op);
    
    //set state to terminate
    slab[fd].state = terminate;
    
    //Points the previous client to the next removing itself from the list
    if(slab[fd].prev != NULL)    
        slab[fd].prev->next = slab[fd].next;
    
    if(slab[fd].next != NULL)
        slab[fd].next->prev = slab[fd].prev;   
        
    //Unlocks the mutex      
    pthread_mutex_unlock(&slab_op);
}

/*
                                                                                                                        
                                     :::::                                                                              
                                    ::  : :::                                                                           
                                    :::     ::::                                                                        
                                    :::        :::                                                                      
                                   ::::          :::                                                                    
                                   :::::           :::                                                                  
                                  ::::::             ::                                                                 
                                  :::::::             ::                                                                
                                  :::::::               ::                                                              
                                ::::::                   ::::                                                           
                              :::                           :::                                                         
                           ::::                               ::                                                        
                      ::::: :                                   ::                                                      
                   ::::                                           ::                                                    
                ::::                                               ::                                                   
              :::                                                   ::                                                  
           :::                                                       ::                                                 
         :::                                                          ::                                                
        ::                                                             :                                                
      ::                                                                ::                                              
    ::                                                                   :                                              
     ::                                                      ::::        ::                                             
      :::                                                      :::        ::                                            
       ::::                                                  :::::        ::                                            
        ::::::                                                ::::       :::::                                          
          ::::::                                              ::::   ::   ::                                            
            :::::::: :                 :::                               :::::                                          
             :::::::::                ::::::                              ::                                            
               :::::::                 :::::                              ::::                                          
                  :::::              :::::::                              : ::                                          
                   :::::              :::::             ::               ::  ::                                         
                   :::::                                :               ::  :::                                         
                    :::::                             ::               ::  :::::                                        
                    :::::                : :                          :: :::::::::::                                    
                      ::::                                          ::::::::        :::                                 
                       ::::                                       ::                   ::                               
                     :::::::::                                  ::                     :::                              
                   :::    :::  :                              ::                          :::::::                       
                 :::     :: :::: :                         :::                                 :::::::                  
               :::          :: :::::  ::::::::         ::::                                   ::::::::::::              
              ::                    ::::    :::::::::::                                       :::::::::::::::           
             ::                     :         :::                                             ::::::::::::::::::        
            :::::                              ::                                             ::::::::::::::::::        
          ::::::::                             ::                                              ::::::::::::::::         
          ::::::::::                          ::                                                ::::::::::::::          
         ::::::::::::                       :::                                                  ::::::::::::           
         ::        :::                   :::                                                      ::::::::::            
         ::        : :                 ::::                                                        :::::::::            
         ::                           :: :                                                           :::::::            
         ::  :::::::::::             :::::                                                      ::  :::: ::::           
         :::::::::::::                  ::                                                      ::::::    :::           
         :::::::::::                    :         :::::                                         ::::      ::::          
         :::::::::                      :           ::::                                        ::         :::          
          :::::::                       :        ::::::::                                       ::         :::          
          ::::::                        ::       ::::::::                      ::::             ::         :::          
          ::::                  ::::::: ::        ::::::                        ::::            ::         ::::         
          :::::               ::::::::::::                                   : ::::::           ::         :::::        
          :: ::             :::::::::::::::   :::                            ::::::::          ::          ::::::       
          ::  :::          :::::::::::::::::                                  :::::::         :::          :: ::::      
          ::    ::::    ::::           ::::::                                               ::::          :: : :::      
          ::       ::::::                :::::                  :             :: :        : :::           :     ::      
           :                              ::: :::           :  ::                          :::          ::      :::     
           ::                              ::  ::                :::                      :::::::::::: ::       :::     
            ::                              :    ::                                     ::::::        ::        :::     
             :                             ::      :::                           :::::::::::            :      ::::     
              ::                          :::   ::::::::::                    ::::    :::::             ::    ::::      
                ::                       :: ::::::::::::::::::::            :::         ::                    ::::      
                  ::                   ::: ::::::::::::::       :::::::::::::           ::                  ::::::      
                    ::::             :::      :::::::::::                  ::           ::                :::::::       
                        :::::::::::::            :::::::                    :::::  :::::::             :::::::::        
                                                                             :::::::::  :::::::::::::::::::::::         
                                                                                         :::::::::::::::::::::          
                                                                                           ::::::::::::::::::           
                                                                                             :::::::::::::          */