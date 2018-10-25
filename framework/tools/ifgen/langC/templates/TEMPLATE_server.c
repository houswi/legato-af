{#-
 #  Jinja2 template for generating server stubs for Legato APIs.
 #
 #  Note: C/C++ comments apply to the generated code.  For example this template itself is not
 #  autogenerated, but the comment is copied verbatim into the generated file when the template is
 #  expanded.
 #
 #  Copyright (C) Sierra Wireless Inc.
 #}
{% import 'pack.templ' as pack with context -%}
/*
 * ====================== WARNING ======================
 *
 * THE CONTENTS OF THIS FILE HAVE BEEN AUTO-GENERATED.
 * DO NOT MODIFY IN ANY WAY.
 *
 * ====================== WARNING ======================
 */


#include "{{apiName}}_server.h"
#include "{{apiBaseName}}_messages.h"
#include "{{apiName}}_service.h"


//--------------------------------------------------------------------------------------------------
// Generic Server Types, Variables and Functions
//--------------------------------------------------------------------------------------------------
{%- if args.localService %}

//--------------------------------------------------------------------------------------------------
/**
 * Expected number of clients of this server API.
 *
 * Default to one client per server.
 */
//--------------------------------------------------------------------------------------------------
#ifndef HIGH_CLIENT_COUNT
#define HIGH_CLIENT_COUNT (LE_CDATA_COMPONENT_COUNT)
#endif

/// Expected number of client messages.  Expect at most one event to each client and one
/// call from each client.
#define HIGH_CLIENT_MESSAGES  (HIGH_CLIENT_COUNT*2)


//--------------------------------------------------------------------------------------------------
/**
 * Static pool for client messages
 */
//--------------------------------------------------------------------------------------------------
LE_MEM_DEFINE_STATIC_POOL({{apiName}}Messages, HIGH_CLIENT_COUNT,
                          LE_CDATA_COMPONENT_COUNT*(LE_MSG_LOCAL_HEADER_SIZE +_MAX_MSG_SIZE));

//--------------------------------------------------------------------------------------------------
/**
 * Reference to message pool.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t {{apiName}}MessagesRef;
{%- endif %}


//--------------------------------------------------------------------------------------------------
/**
 * Type definition for generic function to remove a handler, given the handler ref.
 */
//--------------------------------------------------------------------------------------------------
typedef void(* RemoveHandlerFunc_t)(void *handlerRef);


//--------------------------------------------------------------------------------------------------
/**
 * Server Data Objects
 *
 * This object is used to store additional context info for each request
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_msg_SessionRef_t   clientSessionRef;     ///< The client to send the response to
    void*                 contextPtr;           ///< ContextPtr registered with handler
    le_event_HandlerRef_t handlerRef;           ///< HandlerRef for the registered handler
    RemoveHandlerFunc_t   removeHandlerFunc;    ///< Function to remove the registered handler
}
_ServerData_t;


//--------------------------------------------------------------------------------------------------
/**
 * Expected number of simultaneous server data objects.
 */
//--------------------------------------------------------------------------------------------------
#define HIGH_SERVER_DATA_COUNT            3


//--------------------------------------------------------------------------------------------------
/**
 * Static pool for server data objects
 */
//--------------------------------------------------------------------------------------------------
LE_MEM_DEFINE_STATIC_POOL({{apiName}}_ServerData, HIGH_SERVER_DATA_COUNT, sizeof(_ServerData_t));
{%- if args.async %}

//--------------------------------------------------------------------------------------------------
/**
 * Server command object.
 *
 * This object is used to store additional information about a command
 */
//--------------------------------------------------------------------------------------------------
typedef struct {{apiName}}_ServerCmd
{
    le_msg_MessageRef_t msgRef;           ///< Reference to the message
    le_dls_Link_t cmdLink;                ///< Link to server cmd objects
    uint32_t requiredOutputs;           ///< Outputs which must be sent (if any)
    {%- if interface|MaxCOutputBuffers > 0 %}
    void* outputBuffers[{{interface|MaxCOutputBuffers}}];
    size_t bufferSize[{{interface|MaxCOutputBuffers}}];
    {%- endif %}
} {{apiName}}_ServerCmd_t;


//--------------------------------------------------------------------------------------------------
/**
 * Expected number of simultaneous async server commands.
 */
//--------------------------------------------------------------------------------------------------
#define HIGH_SERVER_CMD_COUNT            5

//--------------------------------------------------------------------------------------------------
/**
 * Static pool for server commands
 */
//--------------------------------------------------------------------------------------------------
LE_MEM_DEFINE_STATIC_POOL({{apiName}}_ServerCmd,
                          HIGH_SERVER_CMD_COUNT,
                          sizeof({{apiName}}_ServerCmd_t));

{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * The memory pool for server data objects
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t _ServerDataPool;


//--------------------------------------------------------------------------------------------------
/**
 *  Static safe reference map for use with Add/Remove handler references
 */
//--------------------------------------------------------------------------------------------------
LE_REF_DEFINE_STATIC_MAP({{apiName}}_ServerHandlers, HIGH_SERVER_DATA_COUNT);


//--------------------------------------------------------------------------------------------------
/**
 * Safe Reference Map for use with Add/Remove handler references
 *
 * @warning Use _Mutex, defined below, to protect accesses to this data.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t _HandlerRefMap;
{%- if args.async %}

//--------------------------------------------------------------------------------------------------
/**
 * The memory pool for server command objects
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t _ServerCmdPool;
{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * Mutex and associated macros for use with the above HandlerRefMap.
 *
 * Unused attribute is needed because this variable may not always get used.
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static pthread_mutex_t _Mutex = PTHREAD_MUTEX_INITIALIZER;
    {#- #}   // POSIX "Fast" mutex.

/// Locks the mutex.
#define _LOCK    LE_ASSERT(pthread_mutex_lock(&_Mutex) == 0);

/// Unlocks the mutex.
#define _UNLOCK  LE_ASSERT(pthread_mutex_unlock(&_Mutex) == 0);


//--------------------------------------------------------------------------------------------------
/**
 * Forward declaration needed by StartServer
 */
//--------------------------------------------------------------------------------------------------
static void ServerMsgRecvHandler
(
    le_msg_MessageRef_t msgRef,
    void*               contextPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Per-server data:
 *  - Server service reference
 *  - Server thread reference
 *  - Client session reference
 */
//--------------------------------------------------------------------------------------------------
LE_CDATA_DECLARE({le_msg_ServiceRef_t _ServerServiceRef;
        le_thread_Ref_t _ServerThreadRef;
        le_msg_SessionRef_t _ClientSessionRef;});

//--------------------------------------------------------------------------------------------------
/**
 * Trace reference used for controlling tracing in this module.
 */
//--------------------------------------------------------------------------------------------------
#if defined(MK_TOOLS_BUILD) && !defined(NO_LOG_SESSION)

static le_log_TraceRef_t TraceRef;

/// Macro used to generate trace output in this module.
/// Takes the same parameters as LE_DEBUG() et. al.
#define TRACE(...) LE_TRACE(TraceRef, ##__VA_ARGS__)

/// Macro used to query current trace state in this module
#define IS_TRACE_ENABLED LE_IS_TRACE_ENABLED(TraceRef)

#else

#define TRACE(...)
#define IS_TRACE_ENABLED 0

#endif
//--------------------------------------------------------------------------------------------------
/**
 * Cleanup client data if the client is no longer connected
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static void CleanupClientData
(
    le_msg_SessionRef_t sessionRef,
    void *contextPtr
)
{
    LE_DEBUG("Client %p is closed !!!", sessionRef);

    // Iterate over the server data reference map and remove anything that matches
    // the client session.
    _LOCK

    // Store the client session ref so it can be retrieved by the server using the
    // GetClientSessionRef() function, if it's needed inside handler removal functions.
    LE_CDATA_THIS->_ClientSessionRef = sessionRef;

    le_ref_IterRef_t iterRef = le_ref_GetIterator(_HandlerRefMap);
    le_result_t result = le_ref_NextNode(iterRef);
    _ServerData_t const* serverDataPtr;

    while ( result == LE_OK )
    {
        serverDataPtr =  le_ref_GetValue(iterRef);

        if ( sessionRef != serverDataPtr->clientSessionRef )
        {
            LE_DEBUG("Found session ref %p; does not match",
                     serverDataPtr->clientSessionRef);
        }
        else
        {
            LE_DEBUG("Found session ref %p; match found, so needs cleanup",
                     serverDataPtr->clientSessionRef);

            // Remove the handler, if the Remove handler functions exists.
            if ( serverDataPtr->removeHandlerFunc != NULL )
            {
                serverDataPtr->removeHandlerFunc( serverDataPtr->handlerRef );
            }

            // Release the server data block
            le_mem_Release((void*)serverDataPtr);

            // Delete the associated safeRef
            le_ref_DeleteRef( _HandlerRefMap, (void*)le_ref_GetSafeRef(iterRef) );
        }

        // Get the next value in the reference mpa
        result = le_ref_NextNode(iterRef);
    }

    // Clear the client session ref, since the event has now been processed.
    LE_CDATA_THIS->_ClientSessionRef = 0;

    _UNLOCK
}


//--------------------------------------------------------------------------------------------------
/**
 * Send the message to the client (queued version)
 *
 * This is a wrapper around le_msg_Send() with an extra parameter so that it can be used
 * with le_event_QueueFunctionToThread().
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static void SendMsgToClientQueued
(
    void*  msgRef,  ///< [in] Reference to the message.
    void*  unused   ///< [in] Not used
)
{
    le_msg_Send(msgRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Send the message to the client.
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static void SendMsgToClient
(
    le_msg_MessageRef_t msgRef      ///< [in] Reference to the message.
)
{
    /*
     * If called from a thread other than the server thread, queue the message onto the server
     * thread.  This is necessary to allow async response/handler functions to be called from any
     * thread, whereas messages to the client can only be sent from the server thread.
     */
    if ( le_thread_GetCurrent() != LE_CDATA_THIS->_ServerThreadRef )
    {
        le_event_QueueFunctionToThread(LE_CDATA_THIS->_ServerThreadRef,
                                       SendMsgToClientQueued,
                                       msgRef,
                                       NULL);
    }
    else
    {
        le_msg_Send(msgRef);
    }
}
{%- if args.localService %}

//--------------------------------------------------------------------------------------------------
/**
 * Initialize the local service
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_InitService
(
    le_msg_LocalService_t* servicePtr
)
{
    if (!{{apiName}}MessagesRef)
    {
        {{apiName}}MessagesRef = le_mem_InitStaticPool({{apiName}}Messages,
                                                       HIGH_CLIENT_COUNT,
                                                       LE_MSG_LOCAL_HEADER_SIZE +
                                                       _MAX_MSG_SIZE);
    }

    le_msg_InitLocalService(servicePtr,
                            SERVICE_INSTANCE_NAME,
                            {{apiName}}MessagesRef);
}
{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * Get the server service reference
 */
//--------------------------------------------------------------------------------------------------
le_msg_ServiceRef_t {{apiName}}_GetServiceRef
(
    void
)
{
    return LE_CDATA_THIS->_ServerServiceRef;
}
{%- if args.localService %}

//--------------------------------------------------------------------------------------------------
/**
 * Set the server service reference (for local services).
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_SetServiceRef
(
    le_msg_LocalService_t* servicePtr      ///< [IN] Service reference
)
{
    LE_CDATA_THIS->_ServerServiceRef = &servicePtr->service;
}
{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * Get the client session reference for the current message
 */
//--------------------------------------------------------------------------------------------------
le_msg_SessionRef_t {{apiName}}_GetClientSessionRef
(
    void
)
{
    return LE_CDATA_THIS->_ClientSessionRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the server and advertise the service.
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_AdvertiseService
(
    void
)
{
    LE_DEBUG("======= Starting Server %s ========", SERVICE_INSTANCE_NAME);

    // Get a reference to the trace keyword that is used to control tracing in this module.
#if defined(MK_TOOLS_BUILD) && !defined(NO_LOG_SESSION)
    TraceRef = le_log_GetTraceRef("ipc");
#endif

    // Create the server data pool
    _ServerDataPool = le_mem_InitStaticPool({{apiName}}_ServerData,
                                            HIGH_SERVER_DATA_COUNT,
                                            sizeof(_ServerData_t));
    {%- if args.async %}

    // Create the server command pool
    _ServerCmdPool = le_mem_InitStaticPool({{apiName}}_ServerCmd,
                                           HIGH_SERVER_CMD_COUNT,
                                           sizeof({{apiName}}_ServerCmd_t));
    {%- endif %}

    // Create safe reference map for handler references.
    // The size of the map should be based on the number of handlers defined for the server.
    // Don't expect that to be more than 2-3, so use 3 as a reasonable guess.
    _HandlerRefMap = le_ref_InitStaticMap({{apiName}}_ServerHandlers, HIGH_SERVER_DATA_COUNT);

    // Start the server side of the service
    {%- if not args.localService %}
    le_msg_ProtocolRef_t protocolRef;

    protocolRef = le_msg_GetProtocolRef(PROTOCOL_ID_STR, sizeof(_Message_t));
    LE_CDATA_THIS->_ServerServiceRef = le_msg_CreateService(protocolRef, SERVICE_INSTANCE_NAME);
    {%- endif %}
    le_msg_SetServiceRecvHandler(LE_CDATA_THIS->_ServerServiceRef, ServerMsgRecvHandler, NULL);
    le_msg_AdvertiseService(LE_CDATA_THIS->_ServerServiceRef);
    {%- if not args.localService %}

    // Register for client sessions being closed
    le_msg_AddServiceCloseHandler(LE_CDATA_THIS->_ServerServiceRef, CleanupClientData, NULL);
    {%- endif %}

    // Need to keep track of the thread that is registered to provide this service.
    LE_CDATA_THIS->_ServerThreadRef = le_thread_GetCurrent();
}
{%- if args.direct %}


//--------------------------------------------------------------------------------------------------
// Client Stubs if this code is called in-place
//--------------------------------------------------------------------------------------------------
{%- if args.localService %}


//--------------------------------------------------------------------------------------------------
/**
 * Get if this client bound locally.
 *
 * If using this version of the function, it's a local binding.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED bool ifgen_{{apiBaseName}}_HasLocalBinding
(
    void
)
{
    return true;
}
{%- endif %}


//--------------------------------------------------------------------------------------------------
/**
 * Init data that is common across all threads
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void ifgen_{{apiBaseName}}_InitCommonData
(
    void
)
{
    // Empty stub
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform common initialization and open a session
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t ifgen_{{apiBaseName}}_OpenSession
(
    le_msg_SessionRef_t _ifgen_sessionRef,
    bool isBlocking
)
{
    // Empty stub
    return LE_OK;
}
{%- for function in functions %}


//--------------------------------------------------------------------------------------------------
{{function.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
LE_SHARED {{function.returnType|FormatType(useBaseName=True)}} ifgen_{{apiBaseName}}_{{function.name}}
(
    le_msg_SessionRef_t _ifgen_sessionRef
    {%- for parameter in function|CAPIParameters %}
    {%- if loop.first %},{% endif %}
    {{parameter|FormatParameter(useBaseName=True)}}{% if not loop.last %},{% endif %}
        ///< [{{parameter.direction|FormatDirection}}]
             {{-parameter.comments|join("\n///<")|indent(8)}}
    {%-endfor%}
)
{
    {% if function.returnType %}return{% endif %} {{apiName}}_{{function.name}}(
        {%- for parameter in function|CAPIParameters %}
        {{parameter|FormatParameterName}}{% if not loop.last %},{% endif %}
        {%- endfor %}
    );
}
{%- endfor %}
{%- endif %}


//--------------------------------------------------------------------------------------------------
// Client Specific Server Code
//--------------------------------------------------------------------------------------------------
{%- for function in functions %}
{#- Write out handler first; there should only be one per function #}
{%- for handler in function.parameters if handler.apiType is HandlerType %}


static void AsyncResponse_{{apiName}}_{{function.name}}
(
    {%- for parameter in handler.apiType|CAPIParameters %}
    {{parameter|FormatParameter}}{% if not loop.last %},{% endif %}
    {%- endfor %}
)
{
    le_msg_MessageRef_t _msgRef;
    _Message_t* _msgPtr;
    _ServerData_t* serverDataPtr = (_ServerData_t*)contextPtr;
    {%- if function is not AddHandlerFunction %}

    // This is a one-time handler; if the server accidently calls it a second time, then
    // the client sesssion ref would be NULL.
    if ( serverDataPtr->clientSessionRef == NULL )
    {
        LE_FATAL("Handler passed to {{apiName}}_{{function.name}}() can't be called
                  {#- #} more than once");
    }
    {% endif %}

    // Will not be used if no data is sent back to client
    __attribute__((unused)) uint8_t* _msgBufPtr;

    // Create a new message object and get the message buffer
    _msgRef = le_msg_CreateMsg(serverDataPtr->clientSessionRef);
    _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    _msgPtr->id = _MSGID_{{apiBaseName}}_{{function.name}};
    _msgBufPtr = _msgPtr->buffer;

    // Always pack the client context pointer first
    LE_ASSERT(le_pack_PackReference( &_msgBufPtr, serverDataPtr->contextPtr ))

    // Pack the input parameters
    {{ pack.PackInputs(handler.apiType.parameters) }}

    // Send the async response to the client
    TRACE("Sending message to client session %p : %ti bytes sent",
          serverDataPtr->clientSessionRef,
          _msgBufPtr-_msgPtr->buffer);

    SendMsgToClient(_msgRef);

    {%- if function is not AddHandlerFunction %}

    // The registered handler has been called, so no longer need the server data.
    // Explicitly set clientSessionRef to NULL, so that we can catch if this function gets
    // accidently called again.
    serverDataPtr->clientSessionRef = NULL;
    le_mem_Release(serverDataPtr);
    {%- endif %}
}
{%- endfor %}

{% if args.async and function is not EventFunction and function is not HasCallbackFunction %}
//--------------------------------------------------------------------------------------------------
/**
 * Server-side respond function for {{apiName}}_{{function.name}}
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_{{function.name}}Respond
(
    {{apiName}}_ServerCmdRef_t _cmdRef
    {%- if function.returnType %},
    {{function.returnType|FormatType}} _result
    {%- endif %}
    {%- for parameter in function|CAPIParameters if parameter is OutParameter %},
    {{parameter|FormatParameter(forceInput=True)}}
    {%- endfor %}
)
{
    LE_ASSERT(_cmdRef != NULL);

    // Get the message related data
    le_msg_MessageRef_t _msgRef = _cmdRef->msgRef;
    _Message_t* _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    __attribute__((unused)) uint8_t* _msgBufPtr = _msgPtr->buffer;

    // Ensure the passed in msgRef is for the correct message
    LE_ASSERT(_msgPtr->id == _MSGID_{{apiBaseName}}_{{function.name}});

    // Ensure that this Respond function has not already been called
    LE_FATAL_IF( !le_msg_NeedsResponse(_msgRef), "Response has already been sent");
    {%- if function.returnType %}

    // Pack the result first
    LE_ASSERT({{function.returnType|PackFunction}}( &_msgBufPtr,
                                                    _result ));
    {%- endif %}

    // Null-out any parameters which are not required so pack knows not to pack them.
    {%- for parameter in function.parameters if parameter is OutParameter %}
    {% if parameter is ArrayParameter %}
    size_t* {{parameter.name}}SizePtr = &{{parameter.name}}Size;
    LE_ASSERT({{parameter|FormatParameterName}});
    {%- elif parameter is not StringParameter %}
    {{parameter.apiType|FormatType}}* {{parameter|FormatParameterName}} = &{{parameter.name}};
    {%- else %}
    LE_ASSERT({{parameter|FormatParameterName}});
    {%- endif %}
    if (!(_cmdRef->requiredOutputs & (1 << {{loop.index0}})))
    {
        {{parameter|FormatParameterName}} = NULL;
    }
    {%- endfor %}

    // Pack any "out" parameters
    {{- pack.PackOutputs(function.parameters,initiatorWaits=True) }}
    {%- if args.localService %}

    // And copy any output parameter buffers
    {%- for parameter in function.parameters
                          if parameter is OutParameter and
                             (parameter is ArrayParameter or parameter is StringParameter) %}
    if (_cmdRef->outputBuffers[{{loop.index0}}])
    {
        {%- if parameter is StringParameter %}
        LE_ASSERT(_cmdRef->bufferSize[{{loop.index0}}] >=
                  strlen(_cmdRef->outputBuffers[{{loop.index0}}]));
        strncpy(_cmdRef->outputBuffers[{{loop.index0}}],
                {{parameter|FormatParameterName}},
                _cmdRef->bufferSize[{{loop.index0}}]);
        ((char*)_cmdRef->outputBuffers[{{loop.index0}}])[_cmdRef->bufferSize[{{loop.index0}}]]
            = '\0';
        {%- else %}
        memcpy(_cmdRef->outputBuffers[{{loop.index0}}],
               {{parameter|FormatParameterName}},
               {{parameter|GetParameterCount}}*sizeof(parameter.apiType));
        {%- endif %}
    }
    {%- endfor %}
    {%- endif %}

    // Return the response
    TRACE("Sending response to client session %p", le_msg_GetSession(_msgRef));

    le_msg_Respond(_msgRef);

    // Release the command
    le_mem_Release(_cmdRef);
}

static void Handle_{{apiName}}_{{function.name}}
(
    le_msg_MessageRef_t _msgRef
)
{
    {%- with error_unpack_label=Labeler("error_unpack") %}
    // Create a server command object
    {{apiName}}_ServerCmd_t* _serverCmdPtr = le_mem_ForceAlloc(_ServerCmdPool);
    _serverCmdPtr->cmdLink = LE_DLS_LINK_INIT;
    _serverCmdPtr->msgRef = _msgRef;

    // Get the message buffer pointer
    __attribute__((unused)) uint8_t* _msgBufPtr =
        ((_Message_t*)le_msg_GetPayloadPtr(_msgRef))->buffer;

    // Unpack which outputs are needed.
    _serverCmdPtr->requiredOutputs = 0;
    {%- if any(function.parameters, "OutParameter") %}
    if (!le_pack_UnpackUint32(&_msgBufPtr, &_serverCmdPtr->requiredOutputs))
    {
        goto {{error_unpack_label}};
    }
    {%- endif %}

    // Unpack the input parameters from the message
    {%- call pack.UnpackInputs(function.parameters,initiatorWaits=True) %}
        goto {{error_unpack_label}};
    {%- endcall %}
    {%- if args.localService %}

    // And save any destination buffers
    {%- for parameter in function.parameters
                          if parameter is OutParameter and
                             (parameter is ArrayParameter or parameter is StringParameter) %}
    _serverCmdPtr->outputBuffers[{{loop.index0}}] = {{parameter|FormatParameterName}};
    _serverCmdPtr->bufferSize[{{loop.index0}}] = {{parameter|GetParameterCount}};
    {%- endfor %}
    {%- endif %}

    // Call the function
    {{apiName}}_{{function.name}} ( _serverCmdPtr
        {%- for parameter in function|CAPIParameters if parameter is InParameter %},
        {#- #} {% if parameter.apiType is HandlerType %}AsyncResponse_{{apiName}}_{{function.name}}
        {%- elif parameter is SizeParameter -%}
        {{parameter.name}}
        {%- else -%}
        {{parameter|FormatParameterName(forceInput=True)}}
        {%- endif %}
        {%- endfor %} );

    return;
    {%- if error_unpack_label.IsUsed() %}

error_unpack:
    le_mem_Release(_serverCmdPtr);

    LE_KILL_CLIENT("Error unpacking inputs");
    {%- endif %}
    {%- endwith %}
}
{%- else %}
static void Handle_{{apiName}}_{{function.name}}
(
    le_msg_MessageRef_t _msgRef

)
{
    {%- with error_unpack_label=Labeler("error_unpack") %}
    // Get the message buffer pointer
    __attribute__((unused)) uint8_t* _msgBufPtr =
        ((_Message_t*)le_msg_GetPayloadPtr(_msgRef))->buffer;

    // Needed if we are returning a result or output values
    uint8_t* _msgBufStartPtr = _msgBufPtr;

    // Unpack which outputs are needed
    {%- if any(function.parameters, "OutParameter") %}
    uint32_t _requiredOutputs = 0;
    if (!le_pack_UnpackUint32(&_msgBufPtr, &_requiredOutputs))
    {
        goto {{error_unpack_label}};
    }
    {%- endif %}

    // Unpack the input parameters from the message
    {%- if function is RemoveHandlerFunction %}
    {#- Remove handlers only have one parameter which is treated specially,
     # so do separate handling
     #}
    {{function.parameters[0].apiType|FormatType}} {{function.parameters[0]|FormatParameterName}}
        {#- #} = {{function.parameters[0].apiType|FormatTypeInitializer}};
    if (!le_pack_UnpackReference( &_msgBufPtr,
                                  &{{function.parameters[0]|FormatParameterName}} ))
    {
        goto {{error_unpack_label}};
    }
    // The passed in handlerRef is a safe reference for the server data object.  Need to get the
    // real handlerRef from the server data object and then delete both the safe reference and
    // the object since they are no longer needed.
    _LOCK
    _ServerData_t* serverDataPtr = le_ref_Lookup(_HandlerRefMap,
                                                 {{function.parameters[0]|FormatParameterName}});
    if ( serverDataPtr == NULL )
    {
        _UNLOCK
        LE_KILL_CLIENT("Invalid reference");
        return;
    }
    le_ref_DeleteRef(_HandlerRefMap, {{function.parameters[0]|FormatParameterName}});
    _UNLOCK
    handlerRef = ({{function.parameters[0].apiType|FormatType}})serverDataPtr->handlerRef;
    le_mem_Release(serverDataPtr);
    {%- else %}
    {%- call pack.UnpackInputs(function.parameters,initiatorWaits=True) %}
        goto {{error_unpack_label}};
    {%- endcall %}
    {%- endif %}
    {#- Now create handler parameters, if there are any.  Should be zero or one #}
    {%- for handler in function.parameters if handler.apiType is HandlerType %}

    // Create a new server data object and fill it in
    _ServerData_t* serverDataPtr = le_mem_ForceAlloc(_ServerDataPool);
    serverDataPtr->clientSessionRef = le_msg_GetSession(_msgRef);
    serverDataPtr->contextPtr = contextPtr;
    serverDataPtr->handlerRef = NULL;
    serverDataPtr->removeHandlerFunc = NULL;
    contextPtr = serverDataPtr;
    {%- endfor %}

    // Define storage for output parameters
    {%- for parameter in function.parameters if parameter is OutParameter %}
    {%- if args.localService and parameter is StringParameter %}
    /* No storage needed for {{parameter.name}} */
    {%- elif args.localService and parameter is ArrayParameter %}
    size_t *{{parameter.name}}SizePtr = &{{parameter.name}}Size;
    {%- elif parameter is StringParameter %}
    char {{parameter.name}}Buffer[{{parameter.maxCount + 1}}] = { 0 };
    char *{{parameter|FormatParameterName}} = {{parameter.name}}Buffer;
    {%- elif parameter is ArrayParameter %}
    {{parameter.apiType|FormatType}} {{parameter.name}}Buffer
        {#- #}[{{parameter.maxCount}}] = { {{parameter.apiType|FormatTypeInitializer}} };
    {{parameter.apiType|FormatType}} *{{parameter|FormatParameterName}} = {{parameter.name}}Buffer;
    size_t *{{parameter.name}}SizePtr = &{{parameter.name}}Size;
    {%- else %}
    {{parameter.apiType|FormatType}} {{parameter.name}}Buffer = {{parameter.apiType|FormatTypeInitializer}};
    {{parameter.apiType|FormatType}} *{{parameter|FormatParameterName}} = &{{parameter.name}}Buffer;
    {%- endif %}
    if (!(_requiredOutputs & (1u << {{loop.index0}})))
    {
        {{parameter|FormatParameterName}} = NULL;
        {%- if parameter is StringParameter %}
        {{parameter.name}}Size = 0;
        {%- endif %}
    }
    {%- endfor %}

    // Call the function
    {% if function.returnType -%}
    {{function.returnType|FormatType}} _result;
    _result  = {% endif -%}
    {{apiName}}_{{function.name}} ( {% for parameter in function|CAPIParameters -%}
        {%- if parameter.apiType is HandlerType -%}
        AsyncResponse_{{apiName}}_{{function.name}}
        {%- elif parameter is SizeParameter %}
        {%- if parameter is not OutParameter %}
        {{parameter.name}}
        {%- else %}
        &{{parameter.name}}
        {%- endif %}
        {%- else %}
        {{parameter|FormatParameterName}}
        {%- endif %}{% if not loop.last %}, {% endif %}
        {%- endfor %} );
    {%- if function is AddHandlerFunction %}

    if (_result)
    {
        // Put the handler reference result and a pointer to the associated remove function
        // into the server data object.  This function pointer is needed in case the client
        // is closed and the handlers need to be removed.
        serverDataPtr->handlerRef = (le_event_HandlerRef_t)_result;
        serverDataPtr->removeHandlerFunc =
            (RemoveHandlerFunc_t){{apiName}}_{{function.name|replace("Add", "Remove", 1)}};

        // Return a safe reference to the server data object as the reference.
        _LOCK
        _result = le_ref_CreateRef(_HandlerRefMap, serverDataPtr);
        _UNLOCK
    }
    else
    {
        // Adding handler failed; release serverDataPtr and return NULL back to the client.
        le_mem_Release(serverDataPtr);
    }
    {%- endif %}

    // Re-use the message buffer for the response
    _msgBufPtr = _msgBufStartPtr;
    {%- if function.returnType %}

    // Pack the result first
    LE_ASSERT({{function.returnType|PackFunction}}( &_msgBufPtr, _result ));
    {%- endif %}

    // Pack any "out" parameters
    {{- pack.PackOutputs(function.parameters,initiatorWaits=True) }}

    // Return the response
    TRACE("Sending response to client session %p : %ti bytes sent",
          le_msg_GetSession(_msgRef),
          _msgBufPtr-_msgBufStartPtr);


    le_msg_Respond(_msgRef);

    return;
    {%- if error_unpack_label.IsUsed() %}

error_unpack:
    LE_KILL_CLIENT("Error unpacking message");
    {%- endif %}
    {%- endwith %}
}
{%- endif %}
{%- endfor %}


static void ServerMsgRecvHandler
(
    le_msg_MessageRef_t msgRef,
    void*               contextPtr
)
{
    // Get the message payload so that we can get the message "id"
    _Message_t* msgPtr = le_msg_GetPayloadPtr(msgRef);

    // Get the client session ref for the current message.  This ref is used by the server to
    // get info about the client process, such as user id.  If there are multiple clients, then
    // the session ref may be different for each message, hence it has to be queried each time.
    LE_CDATA_THIS->_ClientSessionRef = le_msg_GetSession(msgRef);

    // Dispatch to appropriate message handler and get response
    switch (msgPtr->id)
    {
        {%- for function in functions %}
        case _MSGID_{{apiBaseName}}_{{function.name}} :
            Handle_{{apiName}}_{{function.name}}(msgRef);
            break;
        {%- endfor %}

        default: LE_ERROR("Unknowm msg id = %" PRIu32 , msgPtr->id);
    }

    // Clear the client session ref associated with the current message, since the message
    // has now been processed.
    LE_CDATA_THIS->_ClientSessionRef = 0;
}
