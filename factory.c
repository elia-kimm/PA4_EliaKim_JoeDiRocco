//---------------------------------------------------------------------
// Assignment : PA-03 UDP Single-Threaded Server

// Date       : Nov 12th 2025
// Author     : Joe DiRocco - Elia Kim
// File Name  : factory.c
//---------------------------------------------------------------------

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>

#include "wrappers.h"
#include "message.h"

#define MAXSTR     200
#define IPSTRLEN    50

typedef struct sockaddr SA ;

typedef struct {
    int factoryID, capacity, duration;
    int *partsMadePtr;
    int *iterationsPtr;
} factory_params;

int minimum( int a , int b)
{
    return ( a <= b ? a : b ) ;
}

void* subFactory(void *srg) ;

void factLog( char *str )
{
    printf( "%s" , str );
    fflush( stdout ) ;
}

/*-------------------------------------------------------*/

// Global Variable for Future Thread to Shared
int   remainsToMake , // Must be protected by a Mutex
      actuallyMade ;  // Actually manufactured items

pthread_mutex_t activeMutex = PTHREAD_MUTEX_INITIALIZER;
// int   numActiveFactories = 1 , orderSize ; dont need anymore in p4

int   sd ;      // Server socket descriptor
struct sockaddr_in  
             srvrSkt,       /* the address of this server   */
             clntSkt;       /* remote client's socket       */
socklen_t clntLen;

//------------------------------------------------------------
//  Handle Ctrl-C or KILL 
//------------------------------------------------------------
void goodbye(int sig) 
{
    /* Mission Accomplished */
    printf( "\n### I (%d) have been nicely asked to TERMINATE. "
           "goodbye\n\n" , getpid() );  

    // need to send a protocall message to the client
    msgBuf msg;
    memset(&msg, 0, sizeof(msg));
    msg.purpose = htonl(PROTOCOL_ERR);
    if (sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, clntLen) < 0) {
        perror("sendto PROTOCOL_ERR failed");
    }
    close(sd);
    exit(EXIT_SUCCESS);
}

/*-------------------------------------------------------*/
int main( int argc , char *argv[] )
{
    char  *myName = "Joe DiRocco - Elia Kim" ; 
    unsigned short port = 50015 ;      /* service port number  */
    int    N = 1 ;                     /* Num threads serving the client */

    printf("\nThis is the FACTORY server ( by %s )\n\n" , myName ) ;
    char myUserName[30] ;
    getlogin_r ( myUserName , 30 ) ;
    time_t  now;
    time( &now ) ;
    //fprintf( stdout , "Logged in as user '%s' on %s\n\n" , myUserName ,  ctime( &now)  ) ;
    //fflush( stdout ) ;

	switch (argc) 
	{
      case 1:
        break ;     // use default port with a single factory thread
      
      case 2:
        N = atoi( argv[1] ); // get from command line
        port = 50015;            // use this port by default
        break;

      case 3:
        N    = atoi( argv[1] ) ; // get from command line
        port = atoi( argv[2] ) ; // use port from command line
        break;

      default:
        printf( "FACTORY Usage: %s [numThreads] [port]\n" , argv[0] );
        exit( 1 ) ;
    }

    // missing code goes here

    //creating UDP socket
    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        err_sys("Could NOT create socket");
    }

    // signal handlers
    sigactionWrapper(SIGINT, goodbye);
    sigactionWrapper(SIGTERM, goodbye);

    //prepare the servers socket address structure
    memset((void *) &srvrSkt, 0, sizeof(srvrSkt));
    srvrSkt.sin_family = AF_INET;
    srvrSkt.sin_addr.s_addr = htonl(INADDR_ANY);
    srvrSkt.sin_port = htons(port);

    // now bind the server to above socket
    if ( bind( sd, (SA *) & srvrSkt , sizeof(srvrSkt) ) < 0 )
    {
        err_sys("bind failed");
    }

    //turn the ip into a human readable string
    char ipStr[IPSTRLEN];
    inet_ntop( AF_INET, (void *) & srvrSkt.sin_addr.s_addr , ipStr , IPSTRLEN ) ;
    printf("I will attempt to accept orders at port %u and use %d sub-factories.\n\n", 
        ntohs(srvrSkt.sin_port), N);
    //print socket number, IP, and port
    printf("Bound socket %d to IP %s Port %u\n",
        sd, ipStr, ntohs(srvrSkt.sin_port));

    msgBuf msg1;
    srand(time(NULL));
    int forever = 1;
    while ( forever )
    {
        printf( "\nFACTORY server ( by %s )  waiting for Order Requests\n", myName ) ; 

        clntLen = sizeof(clntSkt);

        // receive REQUEST_MSG
        if (recvfrom(sd, &msg1, sizeof(msg1), 0, (SA *)&clntSkt, &clntLen) < 0) {
            err_sys("recvfrom failed");
        }

        printf("\n\nFACTORY server ( by %s ) received: ", myName ) ;
        printMsg( & msg1 );  puts("");
        char ipStr2[IPSTRLEN];
        printf("        From IP %s port %u", 
            inet_ntop(AF_INET, &clntSkt.sin_addr, ipStr2, IPSTRLEN),
            ntohs(clntSkt.sin_port));

        if ( ntohl(msg1.purpose) != REQUEST_MSG) {
            printf("ERROR: not a request message");
            continue;
        }

        int orderSize = ntohl(msg1.orderSize);
        remainsToMake = orderSize;
        actuallyMade = 0;

        // send order_confirm
        msg1.purpose = htonl(ORDR_CONFIRM);
        msg1.numFac = htonl(N);

        if (sendto(sd, &msg1, sizeof(msg1), 0, (SA *)&clntSkt, clntLen) < 0) {
            err_sys("sendto failed");
        }

        printf("\n\nFACTORY ( by %s ) sent this Order Confirmation to the client ", myName );
        printMsg(  & msg1 );  puts("");


        // start the timer so that procurement can print it in its final report
        struct timeval start, end;
        gettimeofday(&start, NULL);

        //track parts made per thread and iterations
        int partsMade[N];
        int iterations[N];

        //create the threads
        pthread_t thrd[N];
        factory_params *args;

        for (int i = 0; i < N; i++) {
            args = (factory_params *) malloc(sizeof(factory_params));
            if( ! args ) { puts("Out of Memory") ; exit(-1) ; }

            args->factoryID = i + 1;
            args->capacity = rand() % 41 + 10;
            args->duration = rand() % 701 + 500;
            args->partsMadePtr = &partsMade[i];
            args->iterationsPtr = &iterations[i];

            printf("Created Factory Thread # %d with capacity =  %d parts & duration = %d mSec\n", 
                args->factoryID, args->capacity, args->duration);

            Pthread_create(&thrd[i], NULL, subFactory, (void *)args);
        }

        for (int i = 0; i < N; i++) {
            Pthread_join(thrd[i], NULL);
        }

        gettimeofday(&end, NULL);
        double total_time = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;

        printf("\n****** FACTORY Server ( by %s ) Summary Report ******\n", myName);
        printf("%15s %14s %15s\n", "Sub-Factory", "Parts Made", "Iterations");
        int grandTotal = 0;
        for (int i = 1; i < N; i++) {
            //printf("             %d              %d           %d\n", i+1, partsMade[i], iterations[i]);
            printf("%15d %14d %15d\n", i, partsMade[i], iterations[i]);
            grandTotal += partsMade[i];
        }
        printf("====================================================\n");
        printf("Grand total parts made   =  %d   vs   order size of   %d\n\n", grandTotal, orderSize);
        printf("Order-to-Completion time = %.1f milliseconds\n", total_time);  
    }


    return 0 ;
}

void *subFactory( void *arg )
{
    char    strBuff[ MAXSTR ] ;   // snprint buffer
    int     partsImade = 0 , myIterations = 0 ;
    msgBuf  msg;

    factory_params *fact = (factory_params *) arg;
    int factoryID = fact-> factoryID;
    int myCapacity = fact->capacity;
    int myDuration = fact->duration;

    while ( 1 )
    {
        pthread_mutex_lock(&activeMutex);
        // See if there are still any parts to manufacture
        if ( remainsToMake <= 0 ) {
            pthread_mutex_unlock(&activeMutex);
            break ;   // Not anymore, exit the loop
        }


        // missing code goes here
        // choose how many to make
        int numCurMaking = minimum(myCapacity, remainsToMake);
        remainsToMake -= numCurMaking;

        pthread_mutex_unlock(&activeMutex);

        partsImade += numCurMaking;
        myIterations++;

        printf("Factory (Joe DiRocco - Elia Kim)  # %d: Going to make   %d parts in  %d mSec\n",
            factoryID, numCurMaking, myDuration); // I NEED TO NOT HARD CODE THE MYNAME PRINT
        /*remainsToMake -= numCurMaking;
        pthread_mutex_unlock(&activeMutex);

        partsImade += numCurMaking;
        myIterations++;*/

        //sleep to simulate production
        Usleep(myDuration * 1000);



        // Send a Production Message to Supervisor
        msg.purpose = htonl(PRODUCTION_MSG);
        msg.facID = htonl(factoryID);
        msg.capacity = htonl(myCapacity);
        msg.partsMade = htonl(numCurMaking);
        msg.duration = htonl(myDuration);

        /*if(sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, clntLen) < 0 ) {
            err_sys("sendto PRODUCTION_MSG failed");
        }*/
        sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, clntLen);


    }

    // Send a Completion Message to Supervisor


    msg.purpose = htonl(COMPLETION_MSG);
    msg.facID = htonl(factoryID);

    if (sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, clntLen) < 0) {
        err_sys("sendto COMPLETION_MSG failed");
    }


    snprintf( strBuff , MAXSTR , ">>> Factory # %-3d: Terminating after making total of %-5d parts in %-4d iterations\n" 
          , factoryID, partsImade, myIterations);
    factLog( strBuff ) ;

    // store totals for grand totals
    *(fact->partsMadePtr) = partsImade;
    *(fact->iterationsPtr) = myIterations;
    
    free(arg);
    return NULL;
}

