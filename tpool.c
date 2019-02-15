//Author Candice Sandefur

#include "tpool.h"

#define TASKS_PER_THREAD 10

static void* tpool_add_worker(void *ignore);

static void enqueue(int task);

static int dequeue();


//Stuct
typedef struct tpool{
  int *queue;
  pthread_mutex_t q_op_mtx;
  
  pthread_mutex_t q_avail_mtx;
  pthread_cond_t q_avail_cv;
  int q_avail_sem;
  
  pthread_mutex_t q_free_mtx;
  pthread_cond_t q_free_cv;
  int q_free_sem;
  
  int top;
  int bottom;
  int queueMax;  
  
  void (*process_task)(int);
  
}tpool;

//Create the tpool
tpool *tpool_t;


//=======Functions=======
int tpool_init(void (*process_task)(int)){
  long cores;
  tpool_t = malloc(sizeof(tpool));//Malloc memory for the thread pool	
  
  if(tpool_t == NULL){
	perror("Failed to malloc memory for struct");
	exit(EXIT_FAILURE);
  }

  cores = sysconf(_SC_NPROCESSORS_ONLN);//Get the number of cores
  tpool_t->queueMax = cores * TASKS_PER_THREAD; 
  
  tpool_t->queue = malloc(tpool_t->queueMax * sizeof(int));
  
  if(tpool_t->queue == NULL){
	perror("Failed to malloc memory for struct");
	exit(EXIT_FAILURE);
  }

  tpool_t->process_task = process_task;
  
  tpool_t->q_op_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  
  tpool_t->q_avail_mtx = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
  tpool_t->q_avail_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  tpool_t->q_avail_sem = 0;
  
  tpool_t->q_free_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  tpool_t->q_free_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  tpool_t->q_free_sem = tpool_t->queueMax;
  
  for(int i = 0; i < cores; i++){
    pthread_t dummy;
    pthread_create(&dummy, NULL, tpool_add_worker, NULL);
  }
  return 0;
}

//Consumer
static void* tpool_add_worker(void *ignore){
  int next;
  while(1){
    pthread_mutex_lock(&(tpool_t->q_avail_mtx));
    while(tpool_t->q_avail_sem == 0){
      pthread_cond_wait(&(tpool_t->q_avail_cv), &(tpool_t->q_avail_mtx));	
    }
    tpool_t->q_avail_sem--;
    pthread_mutex_unlock(&(tpool_t->q_avail_mtx));
    
    pthread_mutex_lock(&(tpool_t->q_op_mtx));
    next = dequeue();
    pthread_mutex_unlock(&(tpool_t->q_op_mtx));
    
    pthread_mutex_lock(&(tpool_t->q_free_mtx));
    tpool_t->q_free_sem++;
    pthread_mutex_unlock(&(tpool_t->q_free_mtx));
    pthread_cond_signal(&(tpool_t->q_free_cv));
    
    tpool_t->process_task(next);
  }

  return NULL;
}

//Producer
int tpool_add_task(int newtask){
  pthread_mutex_lock(&(tpool_t->q_free_mtx));
  while(tpool_t->q_free_sem == 0){
    pthread_cond_wait(&(tpool_t->q_free_cv), &(tpool_t->q_free_mtx));
  }
  tpool_t->q_free_sem--;
  pthread_mutex_unlock(&(tpool_t->q_free_mtx));
  
  pthread_mutex_lock(&(tpool_t->q_op_mtx));
  enqueue(newtask);
  pthread_mutex_unlock(&(tpool_t->q_op_mtx));     
  
  pthread_mutex_lock(&(tpool_t->q_avail_mtx));
  tpool_t->q_avail_sem++;
  pthread_mutex_unlock(&(tpool_t->q_avail_mtx));
  pthread_cond_signal(&(tpool_t->q_avail_cv));
  
  return 0;
}

//=======Queue=======
static void enqueue(int task){
  tpool_t->queue[tpool_t->bottom] = task;
  
  if(tpool_t->bottom++ == tpool_t->queueMax){
    tpool_t->bottom = 0;
  }
  
}

static int dequeue(){
  int value;
  value = tpool_t->queue[tpool_t->top];
  tpool_t->queue[tpool_t->top] = 0;
  if(tpool_t->top++ == tpool_t->queueMax){
    tpool_t->top = 0;
  }
  
  return value;
}

/*
                                                                                                                        
                                                                                                                        
                                                                                                                        
                                              ::::::::                           :::::::::::                            
                                           :::::::::::::                        :::::::::::::::                         
                                         ::::::::   ::::                         ::::      ::::::                       
                                       :::::::       ::::                         ::::        ::::                      
                                     ::::::          ::::                          :::          :::                     
                                   ::::::            ::::                          :::           ::: ::::               
                                 ::::::             ::::                           :::            ::::::::::            
                                :::::               ::::                           :::             :::::::::::          
                              :::::                 ::::                           :::                    ::::::        
                             :::::                  ::::::                         :::                      ::::::      
                         :::::::                    :::::::::::                    :::       :                ::::::::  
                      :::::::::                     :::::::::::::::                ::::    :::                  ::::::: 
                   :::::: ::::                        :::   ::::::::::::::          :::::::::                       ::: 
                ::::::    ::                                    :::::::::::::::::::::::::::                 ::::::::::  
              :::::      ::                                           :::::::::::::::::                 :::::::::::::   
            :::::        :                                                                      ::    ::::::            
          :::::                                                                                 ::: :::::               
         ::::      ::::                                                                        ::::::::                 
       :::::      :  :                                                                         ::: ::                   
      ::::       ::::                                                                         ::::                      
     ::::                                                                                     ::::                      
    ::::                                                                                     ::::                       
   ::::                    ::::::::::::                                                      ::::                       
   ::::                 :::::::::::::::::                                                   ::::                        
  :::: ::             ::::  ::: :::::::::::                                                :::::                        
  ::::::::::         ::::  ::  ::::::::::::::                                             :::::                         
 :::::   ::::        :::  ::  ::::::::::::::::                                           :::::                          
 ::::: :::::::      :::  ::  ::::::::::::::::::                                         :::::                           
 :::: :::::::::     ::   ::  :::::::::::::::::::                                       :::::                            
 ::::::::::::::     ::   :   :::::::::::::::::::                                      :::::                             
 :::::::::::::::    ::   :    ::::::::::::::::::                                     :::::                              
 :::::::::::::::    ::   :    ::::::::::::::::::                                    :::::                               
 ::::: ::::::::::   :::  ::    :::::::::::::::::                 :::              ::::::                                
 :::::: :::::::::    ::  ::     ::::::::::::::::                   ::::          ::::::                                 
 ::::::: ::::::::     ::  ::     :::::::::::::::                     :::        ::::::                                  
  :::: :: :::::::     ::   ::      :::::::: :::                       :::     ::::::                                    
   :::: ::  :: ::      ::   :::           :::::                        :::   ::::::                                     
   :::::  ::  :::       ::    :::::    ::::::                           :::::::::                                       
    :::::   ::::                  ::::::::::                            ::::::::                                        
     ::::  :::::::::            :::::::::        ::                     ::::::                                          
      :::                                        :::                     :::                                            
     ::::                                         ::                     :::                                            
    ::::                                          :::                    :::                                            
    ::::                                           ::::                  :::                                            
    ::::                 ::                        ::::                  ::                                             
     :::::              :::                         :::::               :::                                             
      ::::::::       :::::                         ::::::::            ::::                                             
        :::::::::::::::                    :::::::::::::::::::       :::::                                              
           :::::::::::::::::::::::::::::::::::::::::::   ::::::::::::::::                                               
               ::::::::::::::::::::::::::::::::::          ::::::::::::                                                 
                      ::::::::::::::::::: :::::                                                                                                                                             
*/



