/**
 *  \file semSharedReceptionist.c (implementation file)
 *
 *  \brief Problem name: Restaurant
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the receptionist:
 *     \li waitForGroup
 *     \li provideTableOrWaitingRoom
 *     \li receivePayment
 *
 *  \author Nuno Lau - December 2018
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "probConst.h"
#include "probDataStruct.h"
#include "logging.h"
#include "sharedDataSync.h"
#include "semaphore.h"
#include "sharedMemory.h"

/** \brief logging file name */
static char nFic[51];

/** \brief shared memory block access identifier */
static int shmid;

/** \brief semaphore set access identifier */
static int semgid;

/** \brief pointer to shared memory region */
static SHARED_DATA *sh;

/* constants for groupRecord */
#define TOARRIVE 0
#define WAIT 1
#define ATTABLE 2
#define DONE 3

/** \brief receptioninst view on each group evolution (useful to decide table binding) */
static int groupRecord[MAXGROUPS];

/** \brief receptionist waits for next request */
static request waitForGroup();

/** \brief receptionist waits for next request */
static void provideTableOrWaitingRoom(int n);

/** \brief receptionist receives payment */
static void receivePayment(int n);

/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the receptionist.
 */
int main(int argc, char *argv[])
{
    int key;    /*access key to shared memory and semaphore set */
    char *tinp; /* numerical parameters test flag */

    /* validation of command line parameters */
    if (argc != 4)
    {
        freopen("error_RT", "a", stderr);
        fprintf(stderr, "Number of parameters is incorrect!\n");
        return EXIT_FAILURE;
    }
    else
    {
        freopen(argv[3], "w", stderr);
        setbuf(stderr, NULL);
    }

    strcpy(nFic, argv[1]);
    key = (unsigned int)strtol(argv[2], &tinp, 0);
    if (*tinp != '\0')
    {
        fprintf(stderr, "Error on the access key communication!\n");
        return EXIT_FAILURE;
    }

    /* connection to the semaphore set and the shared memory region and mapping the shared region onto the
       process address space */
    if ((semgid = semConnect(key)) == -1)
    {
        perror("error on connecting to the semaphore set");
        return EXIT_FAILURE;
    }
    if ((shmid = shmemConnect(key)) == -1)
    {
        perror("error on connecting to the shared memory region");
        return EXIT_FAILURE;
    }
    if (shmemAttach(shmid, (void **)&sh) == -1)
    {
        perror("error on mapping the shared region on the process address space");
        return EXIT_FAILURE;
    }

    /* initialize random generator */
    srandom((unsigned int)getpid());

    /* initialize internal receptionist memory */
    int g;
    for (g = 0; g < sh->fSt.nGroups; g++)
    {
        groupRecord[g] = TOARRIVE;
    }

    /* simulation of the life cycle of the receptionist */
    int nReq = 0;
    request req;
    while (nReq < sh->fSt.nGroups * 2)
    {
        req = waitForGroup();
        switch (req.reqType)
        {
        case TABLEREQ:
            provideTableOrWaitingRoom(req.reqGroup);
            break;
        case BILLREQ:
            receivePayment(req.reqGroup);
            break;
        }
        nReq++;
    }

    /* unmapping the shared region off the process address space */
    if (shmemDettach(sh) == -1)
    {
        perror("error on unmapping the shared region off the process address space");
        return EXIT_FAILURE;
        ;
    }

    return EXIT_SUCCESS;
}

/**
 *  \brief decides table to occupy for group n or if it must wait.
 *
 *  Checks current state of tables and groups in order to decide table or wait.
 *
 *  \return table id or -1 (in case of wait decision)
 */
static int decideTableOrWait(int n)
{
    int num = 0;
    bool table1 = false;
    bool table2 = false;

    for (int j = 0; j < sh->fSt.nGroups; j++)
    {
        if (sh->fSt.assignedTable[j] == 0)
        {
            table1 = true;
            num++;
        }
        else if (sh->fSt.assignedTable[j] == 1)
        {
            table2 = true;
            num++;
        }
    }

    if (num < 2)
    {
        if (table1 == false)
        {
            return 0;
        }
        if (table2 == false)
        {
            return 1;
        }
    }
    return -1;
}

/**
 *  \brief called when a table gets vacant and there are waiting groups 
 *         to decide which group (if any) should occupy it.
 *
 *  Checks current state of tables and groups in order to decide group.
 *
 *  \return group id or -1 (in case of wait decision)
 */
static int decideNextGroup()
{
    for (int j = 0; j < sh->fSt.nGroups; j++)
    {
        if (groupRecord[j] == WAIT)
        {
            return j;
        }
    }
    return -1;
}

/**
 *  \brief receptionist waits for next request 
 *
 *  Receptionist updates state and waits for request from group, then reads request,
 *  and signals availability for new request.
 *  The internal state should be saved.
 *
 *  \return request submitted by group
 */
static request waitForGroup()
{
    request ret;
    /* enter critical region */
    if (semDown(semgid, sh->mutex) == -1)
    {
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    sh->fSt.st.receptionistStat = WAIT_FOR_REQUEST;
    saveState(nFic, &sh->fSt);

    if (semUp(semgid, sh->mutex) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
    /* exit critical region */

    if (semDown(semgid, sh->receptionistReq) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    /* enter critical region */
    if (semDown(semgid, sh->mutex) == -1)
    {
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    ret.reqGroup = sh->fSt.receptionistRequest.reqGroup;

    if (sh->fSt.receptionistRequest.reqType == TABLEREQ)
    {
        ret.reqType = TABLEREQ;
    }
    if (sh->fSt.receptionistRequest.reqType == BILLREQ)
    {
        ret.reqType = BILLREQ;
    }

    if (semUp(semgid, sh->mutex) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
    /* exit critical region */

    if (semUp(semgid, sh->receptionistRequestPossible) == -1)
    {
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    return ret;
}

/**
 *  \brief receptionist decides if group should occupy table or wait
 *
 *  Receptionist updates state and then decides if group occupies table
 *  or waits. Shared (and internal) memory may need to be updated.
 *  If group occupies table, it must be informed that it may proceed. 
 *  The internal state should be saved.
 */
static void provideTableOrWaitingRoom(int n)
{
    /* enter critical region */
    if (semDown(semgid, sh->mutex) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    if (decideTableOrWait(n) > -1)
    {
        sh->fSt.st.receptionistStat = ASSIGNTABLE;
        sh->fSt.assignedTable[n] = decideTableOrWait(n);
        groupRecord[n] = ATTABLE;
        if (semUp(semgid, sh->waitForTable[n]) == -1)
        {
            perror("error on the up operation for semaphore access (WT)");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        sh->fSt.groupsWaiting++;
        groupRecord[n] = WAIT;
    }
    saveState(nFic, &sh->fSt);

    if (semUp(semgid, sh->mutex) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
    /* exit critical region */
}

/**
 *  \brief receptionist receives payment 
 *
 *  Receptionist updates its state and receives payment.
 *  If there are waiting groups, receptionist should check if table that just became
 *  vacant should be occupied. Shared (and internal) memory should be updated.
 *  The internal state should be saved.
 */
static void receivePayment(int n)
{
    /* enter critical region */
    if (semDown(semgid, sh->mutex) == -1)
    {
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    sh->fSt.st.receptionistStat = RECVPAY;
    groupRecord[n] = DONE;
    saveState(nFic, &sh->fSt);

    int assignedTable = sh->fSt.assignedTable[n];

    if (sh->fSt.groupsWaiting > 0)
    {

        int nextGroup = decideNextGroup();
        sh->fSt.assignedTable[nextGroup] = sh->fSt.assignedTable[n];
        groupRecord[nextGroup] = ATTABLE;
        sh->fSt.st.receptionistStat = WAIT_FOR_REQUEST;
        sh->fSt.groupsWaiting--;
        if (semUp(semgid, sh->waitForTable[nextGroup]) == -1)
        {
            perror("error on the up operation for semaphore access (WT)");
            exit(EXIT_FAILURE);
        }
    }
    sh->fSt.assignedTable[n] = -1;

    if (semUp(semgid, sh->mutex) == -1)
    { /* exit critical region */
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // TODO insert your code here
    if (semUp(semgid, sh->tableDone[assignedTable]) == -1)
    {
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
}