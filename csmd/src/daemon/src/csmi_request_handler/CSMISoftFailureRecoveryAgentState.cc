/*================================================================================

    csmd/src/daemon/src/csmi_request_handler/CSMIBBCMDAgentState.cc

  © Copyright IBM Corporation 2015-2017. All Rights Reserved

    This program is licensed under the terms of the Eclipse Public License
    v1.0 as published by the Eclipse Foundation and available at
    http://www.eclipse.org/legal/epl-v10.html

    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
    restricted by GSA ADP Schedule Contract with IBM Corp.
 
================================================================================*/

#include "CSMISoftFailureRecoveryAgentState.h"
#include "helpers/csm_handler_exception.h"
#include "helpers/AgentHandler.h"

#include "csmi/src/wm/include/csmi_wm_type_internal.h"
#include "csmi/include/csm_api_consts.h"
#include "csmi/include/csm_api.h"
#include <syslog.h>

#define CMD_ID CSM_CMD_soft_failure_recovery
#define STATE_NAME "SoftFailureRecoveryAgentState:"

bool SoftFailureRecoveryAgentState::HandleNetworkMessage(
    const csm::network::MessageAndAddress content,
    std::vector<csm::daemon::CoreEvent*>& postEventList,
    csm::daemon::EventContextHandlerState_sptr& ctx ) 
{
    LOG( csmapi, trace ) << STATE_NAME ":HandleNetworkMessage: Enter";

    // Return status of this function.
    bool success = false;
    
    // EARLY RETURN This needs to invoke the error handler NOT through the aggregator.
    if ( content.GetAddr()->GetAddrType() != csm::network::CSM_NETWORK_TYPE_PTP )
    {
        //LOG(csmapi,error) << STATE_NAME " Expecting a PTP connection.";
        ctx->SetErrorCode(CSMERR_BAD_ADDR_TYPE);
        ctx->SetErrorMessage("Message: This API may not be called from a Compute Node");
        return false;
    }

    // Receive the payload after we know this is a valid invocation.
    csmi_soft_failure_recovery_payload_t *payload = nullptr;
    csm_init_struct_ptr(csmi_soft_failure_recovery_payload_t, payload);

    //if( !payload )
    //{
    //    ctx->SetErrorCode(CSMERR_PAYLOAD_EMPTY);
    //    ctx->SetErrorMessage("Message: Payload could not be initialized on " + 
    //        csm::daemon::Configuration::Instance()->GetHostname());
    //    return false;
    //}
    
    std::string hostname = csm::daemon::Configuration::Instance()->GetHostname();

    // Clone the hostname for RAS reporting.
    payload->hostname = strdup( hostname.c_str() ); 
    
    try
    {
        // 0. Clear out the CGroups.
        csm::daemon::helper::CGroup cgroup = csm::daemon::helper::CGroup(0);
        cgroup.ClearCGroups( true );

        // Note if the cgroup fails we don't care about running the recovery script.
        // 1. Run the local recovery script.
        char* cmd_out = nullptr;

        LOG( csmapi, info ) <<  ctx << "Running soft failure recovery script.";
        int errorCode = csm::daemon::helper::ExecuteSFRecovery(&cmd_out, (csm_get_agent_timeout(CMD_ID)/1000));
        LOG( csmapi, info ) <<  ctx << "Soft failure recovery exited with error code: " << errorCode;

        // Today the command output is totally unused.
        if( cmd_out ) free(cmd_out);

        if(errorCode)
        {
            std::string error = hostname + "[" + std::to_string(errorCode) + 
                "]: Soft failure recovery script did not end successfully;";
            payload->error_message = strdup(error.c_str()); 
            payload->error_code = CSMERR_SOFT_FAIL_RECOVERY_AGENT;
            LOG(csmapi, error) << "Message: " << payload->error_message;
        }

    }
    catch(const csm::daemon::helper::CSMHandlerException& e)
    {
        payload->error_code = CSMERR_CGROUP_FAIL;
        std::string error = hostname + ": " + e.what();
        payload->error_message = strdup(error.c_str());
        LOG(csmapi, error) << "Message: " << payload->error_message;
    }
    catch(const std::exception& e)
    {
        payload->error_code = CSMERR_CGROUP_FAIL;
        std::string error = hostname + ": " + e.what();
        payload->error_message = strdup(error.c_str());
        LOG(csmapi, error) << "Message: " << payload->error_message;
    }
    // Return the results to the Master via the Aggregator.
    char *buffer          = nullptr;
    uint32_t bufferLength = 0;

    csm_serialize_struct( csmi_soft_failure_recovery_payload_t, payload, &buffer, &bufferLength );

    if( buffer )
    {
        // Push the reply through the aggregator.
        this->PushReply(
            buffer,
            bufferLength,
            ctx,
            postEventList,
            true);

        free( buffer );

        success=true;
        LOG(csmapi,info) << ctx->GetCommandName() << ctx <<
            "; Message: Agent completed successfully;";
    }
    else
    {
        ctx->SetErrorCode(CSMERR_MSG_PACK_ERROR);
        ctx->SetErrorMessage("Message: Unable pack response to the Master Daemon;");
    }

    // Clean up the struct.
    csm_free_struct_ptr(csmi_soft_failure_recovery_payload_t, payload);

    LOG( csmapi, trace ) << STATE_NAME ":HandleNetworkMessage: Exit";

    return success;
}

void SoftFailureRecoveryAgentState::HandleError(
    csm::daemon::EventContextHandlerState_sptr& ctx,
    const csm::daemon::CoreEvent &aEvent,
    std::vector<csm::daemon::CoreEvent*>& postEventList,
    bool byAggregator )
{
    LOG( csmapi, trace ) << STATE_NAME ":HandleError: Enter";

    // Append the hostname to the start of the error message.
    // FIXME Workaround for Beta 1
    // ============================================================================
    ctx->SetErrorMessage(csm::daemon::Configuration::Instance()->GetHostname() + "; " + ctx->GetErrorMessage());
    // ============================================================================

    LOG( csmapi, error ) << STATE_NAME " Error Message: " << ctx->GetErrorCode() << " " <<ctx->GetErrorMessage();


    // FIXME Temporary fix for beta 1!
    // If this was a CSMERR_BAD_ADDR_TYPE don't talk through the aggregator.
    // Otherwise return the error through the aggregator.
    if ( ctx->GetErrorCode() == CSMERR_BAD_ADDR_TYPE )
    {
        CSMIHandlerState::DefaultHandleError( ctx, aEvent, postEventList, false );
    }
    else
    {
        CSMIHandlerState::DefaultHandleError( ctx, aEvent, postEventList, true );
    }

    LOG( csmapi, trace ) << STATE_NAME ":HandleError: Exit";
}

