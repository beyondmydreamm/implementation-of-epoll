#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/poll.h>
#include <sys/epoll.h>


#include <pthread.h>

#define MAXLNE 4096

#define BUFFER_LENGTH 1024
#define POLL_SIZE 1024
#define MAX_EPOOL_EVENT 1024

#define NOSET_CB    0
#define READ_CB     1
#define WRITE_CB    2
#define ACCEPT_CB   3


typedef int NCALLBACK(int fd, int event, void *arg);

struct reactor *instance =NULL;
struct reactor *get_instance(void );
int read_callback(int fd,int event,void *arg);
int accpet_callback(int fd,int event,void *arg);
int write_callback(int fd,int event,void *arg);
int init_server(int port);
int set_event(int fd,NCALLBACK cb,int event,void *arg );
int reactor_loop(int listenfd);
int init_reactor(struct reactor *r);
int init_server(int port);


struct single_event{
    int fd;
    void *arg;
    int events;

    NCALLBACK *readcb;
    NCALLBACK *writecb;
    NCALLBACK *acceptcb;

    unsigned char sbuffer[BUFFER_LENGTH];
    int slength;

    unsigned char rbuffer[BUFFER_LENGTH];
    int rlength;
};

// struct event_block{
//     struct event_block *next;
//     struct single_event *events;
// };

struct reactor{
    int epfd;  
    struct single_event *events;
};

struct reactor *get_instance(void ){
   
    if(instance==NULL){
        
        instance = (struct reactor *)malloc(sizeof(struct reactor));
        if(instance==NULL){
            return NULL;
        }
         
        memset(instance,0,sizeof(struct reactor));
        int ret=init_reactor(instance);
        printf("ret===%d\n",ret);
        if(0 > ret){
            free(instance);
            return NULL;
        }
    }
    
    return instance;
}


int set_event(int fd,NCALLBACK cb,int event,void *arg ){
    
    struct reactor*r= get_instance();
    struct epoll_event ev={0};
    
    if(event==READ_CB){
        printf("testint READ_CB here%d\n",fd);
        r->events[fd].fd=fd;
        r->events[fd].readcb=cb;
        r->events[fd].arg=arg;

        ev.events=EPOLLIN;
    }else if(event==WRITE_CB){
        r->events[fd].fd=fd;
        r->events[fd].writecb=cb;
        r->events[fd].arg=arg;

        ev.events=EPOLLOUT;
        printf("testint WRITE_CB here%d\n",fd);
    }else if(event==ACCEPT_CB){
        r->events[fd].fd=fd;
        r->events[fd].acceptcb=cb;
        r->events[fd].arg=arg;

        ev.events=EPOLLIN;
        printf("testint ACCEPT_CB here%d\n",fd);
    }
    
    ev.data.ptr=&r->events[fd];
    
    if(r->events[fd].events==NOSET_CB){
        if (epoll_ctl(r->epfd,EPOLL_CTL_ADD,fd,&ev )<0 )
        {
            printf("epoll_ctl EPOLL_CTL_ADD failed,%s\n",errno);
            return -1;
        }
        r->events[fd].events = event;
    }else if (r->events[fd].events!=event)
    {
        if (epoll_ctl(r->epfd,EPOLL_CTL_MOD,fd,&ev)<0 )
        {
            printf("epoll_ctl EPOLL_CTL_MOD failed,%d\n",errno);
        }
        r->events[fd].events=event;
    }
    
    return 0;
}

int del_event(int fd, NCALLBACK cb, int event, void *arg) {

	struct reactor *r = get_instance();
	
	struct epoll_event ev = {0};
	ev.data.ptr = arg;

	epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, &ev);
	r->events[fd].events = 0;

	return 0;
}

int read_callback(int fd,int event,void *arg){
    struct reactor * R=get_instance();

    unsigned char *rbuffer=R->events[fd].rbuffer;
    int length=R->events[fd].rlength;

    int ret=recv(fd,rbuffer,BUFSIZ,0);
    if (ret==0)
    {
        del_event(fd,NULL,0,NULL);
        close(fd);
    }else if(ret>0){
        unsigned char *sbuffer=R->events[fd].sbuffer;
        memcpy(sbuffer,rbuffer,ret);
        R->events[fd].slength=ret;

        printf("readcb:%s\n",sbuffer);
        
        set_event(fd,write_callback,WRITE_CB,NULL);
        
    }
    

}
int accpet_callback(int fd,int event,void *arg){
    int connfd;
    struct sockaddr_in client;
    socklen_t len=sizeof(client);
    if((connfd=accept(fd,(struct sockaddr *)&client,&len))==-1){
        printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
	    return 0; 
    }

    set_event(connfd,read_callback,READ_CB,NULL);
}
int write_callback(int fd,int event,void *arg){
    struct reactor *R=get_instance();
    
    unsigned char *sbuffer=R->events[fd].sbuffer;
    int length=R->events[fd].slength;

    int ret=send(fd,sbuffer,length,0);
    
    if(ret<length){
        set_event(fd,write_callback,WRITE_CB,NULL);
        printf("testint write_callback ret<length here%d\n",fd);
    }else{
        set_event(fd,read_callback,READ_CB,NULL);
        printf("testint write_callback ret>length here%d\n",fd);
    }
    return 0;
}


int init_server(int port){
    int listenfd;
    struct sockaddr_in servaddr;
    char buff[MAXLNE];

    if((listenfd=socket(AF_INET,SOCK_STREAM,0))==-1){
        printf("create socket error:%s(errno%d)\n",strerror(errno),errno);
        return 0;
    }

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(port);

    if(bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr))==-1){
        printf("bind socket error:%s(errno%d)\n",strerror(errno),errno);
        return 0;
    }

    if(listen(listenfd,10)==-1){
        printf("listen socket error:%s(errno%d)\n",strerror(errno),errno);
        return 0;
    }

    return listenfd;
}



int init_reactor(struct reactor *r){
    
    if(r==NULL) return -1;
    int epfd=epoll_create(1);
    r->epfd=epfd;
    // r=(struct reactor*)malloc(sizeof(struct reactor));
    // if(r==NULL){
    //     close(epfd);
    //     return -2;
    // }
    // memset(r,0,sizeof(struct reactor));

    r->events=(struct single_event*)malloc(MAX_EPOOL_EVENT*sizeof(struct single_event));
    if(r->events==NULL){
        
        free(r);
        close(epfd);
        return -2;
    }
    memset(r->events,0,(MAX_EPOOL_EVENT*sizeof(struct single_event)));
    return 0;
}



int reactor_loop(int listenfd){
    struct reactor *R =get_instance();

    struct epoll_event events[POLL_SIZE]={0};
    while (1)
    {
        int nready=epoll_wait(R->epfd,events,POLL_SIZE,1);
        if(nready==-1){
            continue;
        }
        for(int i=0;i<nready;++i){
            struct single_event *event=(struct single_event*)events[i].data.ptr;
            int connfd=event->fd;

            if(connfd==listenfd){
                event->acceptcb(listenfd,0,NULL);
            }else{
                if(events[i].events&EPOLLIN){
                    event->readcb(connfd,0,NULL);
                }
                if(events[i].events&EPOLLOUT){
                    event->writecb(connfd,0,NULL);
                }
            }
        }
    }
    return 0;
}

int main(){
    int connfd,n;
    int listenfd = init_server(7777);
    set_event(listenfd,accpet_callback,ACCEPT_CB,NULL);
    reactor_loop(listenfd);
    close(listenfd);
    return 0;
}