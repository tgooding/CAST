/*******************************************************************************
 |    xfer_perf.cc
 |
 |  © Copyright IBM Corporation 2015,2016. All Rights Reserved
 |
 |    This program is licensed under the terms of the Eclipse Public License
 |    v1.0 as published by the Eclipse Foundation and available at
 |    http://www.eclipse.org/legal/epl-v10.html
 |
 |    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
 |    restricted by GSA ADP Schedule Contract with IBM Corp.
*******************************************************************************/

#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <string>
#include <iostream>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

#include "bb/include/bbapi.h"

int mpirank = 0;
int size = 0;

int addMetadata(const char* label, const char* value)
{
    if(mpirank == 0) printf("METADATA %s=%s\n", label, value);
    return 0;
}
int addMetadata_fp(const char* label, double value)
{
    if(mpirank == 0) printf("METADATA %s=%g\n", label, value);
    return 0;
}
int addMetric(const char* label, double value)
{
    if(mpirank == 0) printf("METRIC %s=%g\n", label, value);
    return 0;
}
int addMetric_str(const char* label, const char* value)
{
    if(mpirank == 0) printf("METRIC %s=%s\n", label, value);
    return 0;
}

void getLastErrorDetails(BBERRORFORMAT pFormat, char** pBuffer)
{
    int rc;
    size_t l_NumBytesAvailable;
    if(pBuffer)
    {
        rc = BB_GetLastErrorDetails(pFormat, &l_NumBytesAvailable, 0, NULL);
        if(rc == 0)
        {
            *pBuffer = (char*)malloc(l_NumBytesAvailable+1);
            BB_GetLastErrorDetails(pFormat, NULL, l_NumBytesAvailable, *pBuffer);
        }
        else
        {
            *pBuffer = NULL;
        }
    }
}

int check(int rc)
{
    if(rc)
    {
        char* errstring = 0;
        getLastErrorDetails(BBERRORJSON, &errstring);
        printf("Error rc:       %d\n", rc);
        printf("Error details:  %s\n", errstring);
        free(errstring);

        printf("Aborting due to failures\n");
        exit(-1);
    }
    return 0;
}

int main(int argc, char** argv)
{
    int rc;
    po::variables_map vm;
    try
    {
        po::options_description desc("Allowed options");
        
        desc.add_options()
        ("help,h", po::bool_switch(), "Display this help message")
        ("pfs,p",  po::value<string>()->default_value(""), "Path to the parallel file system") 
        ("size,s", po::value<unsigned long>()->default_value(1048576), "fixed size of file")
        ("poll",   po::value<double>()->default_value(1.0), "number of seconds to poll")
        ("in",     po::bool_switch(), "Perform PFS->SSD transfer")
        ("subdir", po::bool_switch(), "Create sub-directory for each compute node on the PFS")
        ("barrier",po::bool_switch(), "Perform barrier between BB operations")
        ("noinfo", po::bool_switch(), "Skip final BB_GetTransferInfo query")
        ("noopt",  po::bool_switch(), "Skip BB_GetTransferCount query")
        ;
    
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if(vm["help"].as<bool>())
        {
            cout << desc << endl;
            exit(0);
        }
    }
    catch (exception& e)
    {
        printf("Error: %s\n", e.what());
        exit(1);
    }

    bool dir           = vm["in"].as<bool>();
    string pfspath     = vm["pfs"].as<string>();
    size_t filesize    = vm["size"].as<unsigned long>();
    bool use_subdir    = vm["subdir"].as<bool>();
    bool nooptrun      = vm["noopt"].as<bool>();;
    bool barrier       = vm["barrier"].as<bool>();;
    double poll_intvl  = vm["poll"].as<double>();;
    bool noinfo        = vm["noinfo"].as<bool>();;

    BBTransferInfo_t info;
    double start, stop;
    double beginning;
    int dogenerate = 1;
    char host_name[MPI_MAX_PROCESSOR_NAME];
    int h_len;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Get_processor_name(host_name, &h_len);
    //printf("My rank is %d out of %d running on %s\n", mpirank, size, host_name);

    rc = BB_InitLibrary(mpirank, BBAPI_CLIENTVERSIONSTR);
    check(rc);

    char sfn[256];
    char tfn[256];
    char tdn[256];
    char cmd[256];
    const char* bbpath = getenv("BBPATH");

    switch(dir)
    {
        case true: // GPFS -> SSD
            if(strcmp(pfspath.c_str(), "/dev/zero") == 0)
            {
                snprintf(sfn, sizeof(sfn), "%s", pfspath.c_str());
                dogenerate = 0;
            }
            else
            {
                if(use_subdir)
                {
                    snprintf(sfn, sizeof(sfn), "%s/%s/rank.%d", pfspath.c_str(), host_name, mpirank);
                    snprintf(tdn, sizeof(tdn), "%s/%s", pfspath.c_str(), host_name);
                    struct stat st_buf = {0};
                    if(0 != stat(tdn, &st_buf))
                    {
                        if(0 != mkdir(tdn, 0700))
                        {
                            // It is ok if the directory exists (our peer probably created it)
                            // Throw a warning if the error is something else.
                            if(EEXIST != errno)
                            {
                                printf("%d/%d on %s) Warning. mkdir() returned the error (%d): %s\n",
                                       mpirank, size, host_name, errno, strerror(errno));
                            }
                        }
                    }
                }
                else
                {
                    snprintf(sfn, sizeof(sfn), "%s/rank.%d", pfspath.c_str(), mpirank);
                }
            }
            snprintf(tfn, sizeof(tfn), "%s/rank.%d", bbpath, mpirank);
            break;
        case false:  // SSD -> GPFS
            snprintf(sfn, sizeof(sfn), "%s/rank.%d", bbpath, mpirank);
            if(strcmp(pfspath.c_str(), "/dev/null") == 0)
            {
                snprintf(tfn, sizeof(tfn), "%s", pfspath.c_str());
            }
            else
            {
                if(use_subdir)
                {
                    snprintf(tfn, sizeof(tfn), "%s/%s/rank.%d", pfspath.c_str(), host_name, mpirank);
                    snprintf(tdn, sizeof(tdn), "%s/%s", pfspath.c_str(), host_name);
                    struct stat st_buf = {0};
                    if(0 != stat(tdn, &st_buf))
                    {
                        if(0 != mkdir(tdn, 0700))
                        {
                            // It is ok if the directory exists (our peer probably created it)
                            // Throw a warning if the error is something else.
                            if(EEXIST != errno)
                            {
                                printf("%d/%d on %s) Warning. mkdir() returned the error (%d): %s\n",
                                       mpirank, size, host_name, errno, strerror(errno));
                            }
                        }
                    }
                }
                else
                {
                    snprintf(tfn, sizeof(tfn), "%s/rank.%d", pfspath.c_str(), mpirank);
                }
            }
            break;
    }
    
    if(mpirank == 0)
    {
        printf("Source file: %s\n", sfn);
        printf("Target file: %s\n", tfn);
    }

    addMetadata("branch",      getenv("BRANCH_NAME"));
    addMetadata("gitcommit",   getenv("GIT_COMMIT"));
    addMetadata("job",         getenv("TESTNAME"));
    addMetadata("type",        (dir)?"in":"out");
    addMetadata("jobid",       getenv("LSB_JOBID"));
    addMetadata("jobstep",     getenv("PMIX_NAMESPACE"));
    addMetadata("user",        getenv("USER"));
    addMetadata("pfspath",     pfspath.c_str());
    addMetadata("bbpath",      bbpath);
    addMetadata("execname",    argv[0]);
    
    addMetadata_fp("filesize", filesize);
    addMetadata_fp("ranks",    size);

    if(dogenerate)
    {
        snprintf(cmd, sizeof(cmd), "/opt/ibm/bb/tools/randfile --file=%s --by 512 --size=%ld", sfn, filesize);
        if(mpirank == 0)
        {
            printf("Generate random file: %s\n", cmd);
        }
        system(cmd);
    }

    BBTransferDef_t* tdef;
    BBTransferHandle_t thandle;
    uint32_t contriblist = mpirank;

    if(mpirank == 0)
    {
        printf("Creating transfer definition\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    beginning = MPI_Wtime();
    rc = BB_CreateTransferDef(&tdef);
    check(rc);

    if(mpirank == 0)
    {
        printf("Adding files to transfer definition\n");
    }
    rc = BB_AddFiles(tdef, sfn, tfn, (BBFILEFLAGS)0);
    check(rc);

    if(mpirank == 0)
    {
        printf("Obtaining transfer handle\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);
    start = MPI_Wtime();
    rc = BB_GetTransferHandle(mpirank, 1, &contriblist, &thandle);
    check(rc);
    if(barrier == false)
    {
        MPI_Barrier(MPI_COMM_WORLD);
    }
    stop = MPI_Wtime();
    if(mpirank == 0)
    {
        printf("PERF(%d,%s,%ld x %d):  BB_GetTransferHandle took %g seconds\n", dir, pfspath.c_str(), filesize, size, stop-start);
        addMetric("bbGetTransferHandle_time", stop-start);
    }

    if(mpirank == 0)
    {
        printf("Starting transfer\n");
    }
    start = MPI_Wtime();
    rc = BB_StartTransfer(tdef, thandle);
    check(rc);
    MPI_Barrier(MPI_COMM_WORLD);
    stop = MPI_Wtime();
    if(mpirank == 0)
    {
        printf("PERF(%d,%s,%ld x %d):  BB_StartTransfer took %g seconds\n", dir, pfspath.c_str(), filesize, size, stop-start);
        addMetric("bbStartTransfer_time", stop-start);
    }

    start = MPI_Wtime();
    while(1)
    {
        if(!nooptrun)
        {
            uint64_t count = 0;
            rc = BB_GetTransferCount(thandle, &count);
            check(rc);
            if(count > 0)
            {
                usleep(poll_intvl * 1000000.0);
                continue;
            }
        }
        if(noinfo)
            break;
        rc = BB_GetTransferInfo(thandle, &info);
        check(rc);
        if((info.status == BBFULLSUCCESS) || (info.status == BBCANCELED) || (info.status == BBFAILED))
            break;
        usleep(poll_intvl * 1000000.0);
    }
    
    MPI_Barrier(MPI_COMM_WORLD);

    stop = MPI_Wtime();
    if(mpirank == 0)
    {
        printf("PERF(%d,%s,%ld x %d):  Transfer took %g seconds (%g MiBps)\n", dir, pfspath.c_str(), filesize, size, stop-start, (double)filesize * size / (stop-start) / 1024 / 1024);
        addMetric("bbTransfer_time", stop-start);
        addMetric("bbTransfer_bandwidth", filesize * size / (stop-start));
        addMetric("bbFullTransfer_time", stop-beginning);
    }

    if(mpirank == 0)
    {
        printf("Terminating BB library\n");
    }
    rc = BB_TerminateLibrary();
    check(rc);

    MPI_Finalize();
    return 0;
}
