/*******************************************************************************
 |    bbwrkqe.h
 |
 |  � Copyright IBM Corporation 2015,2016. All Rights Reserved
 |
 |    This program is licensed under the terms of the Eclipse Public License
 |    v1.0 as published by the Eclipse Foundation and available at
 |    http://www.eclipse.org/legal/epl-v10.html
 |
 |    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
 |    restricted by GSA ADP Schedule Contract with IBM Corp.
 *******************************************************************************/


#ifndef BB_BBWRKQE_H_
#define BB_BBWRKQE_H_

#include <string.h>

#include <queue>
#include <vector>

#include "bbinternal.h"
#include "LVKey.h"
#include "WorkID.h"

using namespace std;


/*******************************************************************************
 | Forward declarations
 *******************************************************************************/
class BBLV_Info;
class BBTransferDef;
class Extent;
class ExtentInfo;


/*******************************************************************************
 | Constants
 *******************************************************************************/
const int DEFAULT_DUMP_QUEUE_ON_REMOVE_WORK_ITEM = 0;
const bool DO_NOT_VALIDATE_WORK_QUEUE = false;
const bool VALIDATE_WORK_QUEUE = true;


/*******************************************************************************
 | Classes
 *******************************************************************************/

//
// WRKQE class
//

class WRKQE
{
  public:
    /**
     * \brief Default Constructor
     */
    WRKQE() :
        lvKey(std::pair<std::string, Uuid>("", Uuid())),
        jobid(UNDEFINED_JOBID),
        rate(0),
        bucket(0),
        suspended(0),
        transferThreadIsDelaying(0),
        dumpOnRemoveWorkItem(DEFAULT_DUMP_QUEUE_ON_REMOVE_WORK_ITEM),
        throttleWait(0),
        workQueueReturnedWithNegativeBucket(0),
        numberOfWorkItems(0),
        numberOfWorkItemsProcessed(0),
        lvinfo(0) {
        init();
    };

    /**
     * \brief Constructor
     */
    WRKQE(const LVKey* pLVKey, BBLV_Info* pLV_Info, const uint64_t pJobId, const int pSuspended) :
        lvKey(*pLVKey),
        jobid(pJobId),
        rate(0),
        bucket(0),
        suspended(pSuspended),
        transferThreadIsDelaying(0),
        dumpOnRemoveWorkItem(DEFAULT_DUMP_QUEUE_ON_REMOVE_WORK_ITEM),
        throttleWait(0),
        workQueueReturnedWithNegativeBucket(0),
        numberOfWorkItems(0),
        numberOfWorkItemsProcessed(0),
        lvinfo(pLV_Info) {
        init();
    };

    /**
     * \brief Destructor
     */
    virtual ~WRKQE() {
        if (wrkq)
        {
            delete wrkq;
            wrkq = 0;
        }
    };

    // Inline methods
    inline int64_t getBucket()
    {
        return bucket;
    };

    inline int getDumpOnRemoveWorkItem()
    {
        return dumpOnRemoveWorkItem;
    };

    inline int64_t getJobId()
    {
        return jobid;
    };

    inline BBLV_Info* getLV_Info()
    {
        return lvinfo;
    };

    inline LVKey* getLVKey()
    {
        return &lvKey;
    };

    inline uint64_t getNumberOfWorkItems()
    {
        return numberOfWorkItems;
    }

    inline uint64_t getNumberOfWorkItemsProcessed()
    {
        return numberOfWorkItemsProcessed;
    }

    inline uint64_t getRate()
    {
        return rate;
    };

    inline void incrementNumberOfWorkItemsProcessed()
    {
        ++numberOfWorkItemsProcessed;

        return;
    };

    inline void incrementNumberOfWorkItems()
    {
        ++numberOfWorkItems;

        return;
    };

    inline void init()
    {
        wrkq = new queue<WorkID>;
        lock_transferqueue = PTHREAD_MUTEX_INITIALIZER;
        transferQueueLocked = 0;

        return;
    };

    inline int isSuspended()
    {
        return suspended;
    };

    inline queue<WorkID>* getWrkQ()
    {
        return wrkq;
    };

    inline size_t getWrkQ_Size()
    {
        return wrkq->size();
    };

    inline void setDumpOnRemoveWorkItem(const int pValue)
    {
        dumpOnRemoveWorkItem = pValue;

        return;
    };

    inline void setRate(const uint64_t pRate)
    {
        rate = pRate;
        if (!rate)
        {
            bucket = 0;
            throttleWait = 0;
            workQueueReturnedWithNegativeBucket = 0;
        }

        return;
    };

    inline void setSuspended(const int pValue)
    {
        suspended = pValue;

        return;
    };

    inline void setThrottleWait(const int pValue)
    {
        throttleWait = pValue;

        return;
    }

    inline void setTransferThreadIsDelaying(const int pValue)
    {
        transferThreadIsDelaying = pValue;

        return;
    };

    inline bool transferQueueIsLocked()
    {
        return (transferQueueLocked == pthread_self());
    }

    inline bool workQueueIsAssignable()
    {
        return (workQueueReturnedWithNegativeBucket ? false : throttleWait ? false : true);
    }

    // Methods
    void addWorkItem(WorkID& pWorkItem, const bool pValidateQueue);
    void dump(const char* pSev, const char* pPrefix);
    int getIssuingWorkItem();
    void loadBucket();
    void lock(const LVKey* pLVKey, const char* pMethod);
    double processBucket(BBTagID& pTagId, ExtentInfo& pExtentInfo);
    void removeWorkItem(WorkID& pWorkItem, const bool pValidateQueue);
    void setIssuingWorkItem(const int pValue);
    void unlock(const LVKey* pLVKey, const char* pMethod);

    // Data members
    LVKey               lvKey;
    uint64_t            jobid;
    uint64_t            rate;       // bytes/sec
    int64_t             bucket;
    int                 suspended;
    int                 transferThreadIsDelaying;
    int                 dumpOnRemoveWorkItem;
    volatile int        throttleWait;
    volatile int        workQueueReturnedWithNegativeBucket;
    uint64_t            numberOfWorkItems;
    uint64_t            numberOfWorkItemsProcessed;
    BBLV_Info*          lvinfo;
    queue<WorkID>*      wrkq;                   // Access is serialized with the
                                                // lock_transferqueue lock
    pthread_mutex_t     lock_transferqueue;
    pthread_t           transferQueueLocked;
};

#endif /* BB_BBWRKQE_H_ */
