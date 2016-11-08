//--------------------------------------------------------------------------------------------------
/**
 * @file fwupdateDownloader.c
 *
 * fwupdate downloader implementation
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include <sys/socket.h>
#include <netinet/in.h>


//--------------------------------------------------------------------------------------------------
/**
 * Server TCP port.
 *
 * @note this is an arbitrary value and can be changed as required
 */
//--------------------------------------------------------------------------------------------------
#define FWUPDATE_SERVER_PORT 5000

//--------------------------------------------------------------------------------------------------
/**
 * This function checks the systems synchronization, and synchronized them if necessary
 *
 * @return
 *      - LE_OK if succeed
 *      - LE_FAULT if error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t CheckSystemState
(
    void
)
{
    le_result_t result;
    bool isSync;

    result = le_fwupdate_DualSysSyncState(&isSync);
    if (result != LE_OK)
    {
        LE_ERROR("Sync State check failed. Error %s", LE_RESULT_TXT(result));
        return LE_FAULT;
    }
    if (isSync == false)
    {
        result = le_fwupdate_DualSysSync();
        if (result != LE_OK)
        {
            LE_ERROR("SYNC operation failed. Error %s", LE_RESULT_TXT(result));
            return LE_FAULT;
        }
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * This function must be called to initialize the FW UPDATE DOWNLOADER module.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    struct sockaddr_in myAddress;
    int ret, optVal = 1;
    int sockFd;
    int connFd;

    LE_INFO("FW UPDATE DOWNLOADER starts");

    // Create the socket
    sockFd = socket (AF_INET, SOCK_STREAM, 0);

    if (sockFd < 0)
    {
        LE_ERROR("creating socket failed: %m");
        return;
    }

    // set socket option
    // we use SO_REUSEADDR to be able to accept several clients without closing the socket
    ret = setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
    if (ret)
    {
        LE_ERROR("error setting socket option %m");
        close(sockFd);
        return;
    }

    bzero(&myAddress, sizeof(myAddress));

    myAddress.sin_port = htons(FWUPDATE_SERVER_PORT);
    myAddress.sin_family = AF_INET;
    myAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind server - socket
    ret = bind(sockFd, (struct sockaddr_in *)&myAddress, sizeof(myAddress));
    if (ret)
    {
        LE_ERROR("bind failed %m");
        close(sockFd);
        return;
    }

    // Listen on the socket
    ret = listen(sockFd, 1);
    if (ret)
    {
        LE_ERROR("listen error: %m");
        close(sockFd);
        return;
    }

    while(1)
    {
        struct sockaddr_in clientAddress;
        le_result_t result;

        socklen_t addressLen = sizeof(clientAddress);

        LE_INFO("waiting connection ...");
        connFd = accept(sockFd, (struct sockaddr *)&clientAddress, &addressLen);

        if (connFd == -1)
        {
            LE_ERROR("accept error: %m");
        }
        else
        {
            LE_INFO("Connected ...");

            result = CheckSystemState();

            if (result == LE_OK)
            {
                result = le_fwupdate_Download(connFd);

                LE_INFO("Download result=%s", LE_RESULT_TXT(result));
                if (result == LE_OK)
                {
                    le_fwupdate_DualSysSwapAndSync();
                    // if this function returns so there is an error => do a SYNC
                    LE_ERROR("Swap And Sync failed -> Sync");
                    result = le_fwupdate_DualSysSync();
                    if (result != LE_OK)
                    {
                        LE_ERROR("SYNC failed");
                    }
                    // TODO send an error message to the host
                }
            }
            else
            {
                LE_ERROR("Connection error %d", result);
            }

            close(connFd);
        }
    }
    close(sockFd);
}
