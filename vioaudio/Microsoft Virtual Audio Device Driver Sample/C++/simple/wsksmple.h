#ifndef _MSVAD_WSK_SMPLE_H_
#define _MSVAD_WSK_SMPLE_H_

#include <ntddk.h>
#include <wsk.h>


#define list_for_each(pos, head)                                  \
	for (pos = (head)->Flink; pos != (head); pos = pos->Flink)    \

#define list_for_each_safe(pos, n, head)                     \
	for (pos = (head)->Flink, n = pos->Flink; pos != (head); \
		pos = n, n = pos->Flink)

#define list_entry(ptr, type, member)   \
	CONTAINING_RECORD(ptr, type, member)

#define list_for_each_entry(pos, head, type, member)                \
        for (pos = list_entry((head)->Flink, type, member);         \
             &pos->member != (head);                                \
                pos = list_entry(pos->member.Flink, type, member))

// Pool tags used for memory allocations
#define WSKSAMPLE_SOCKET_POOL_TAG ((ULONG)'sksw')
#define WSKSAMPLE_BUFFER_POOL_TAG ((ULONG)'bksw')
#define WSKSAMPLE_GENERIC_POOL_TAG ((ULONG)'xksw')

// Default length for data buffers used in send and receive operations
#define WSKSAMPLE_DATA_BUFFER_LENGTH 2048

// Forward declaration for the socket context structure
typedef struct _WSKSAMPLE_SOCKET_CONTEXT *PWSKSAMPLE_SOCKET_CONTEXT;

// Forward declaration for the socket operation context structure
typedef struct _WSKSAMPLE_SOCKET_OP_CONTEXT *PWSKSAMPLE_SOCKET_OP_CONTEXT;

typedef struct WskSampleServer *PWskSampleServer;

// Function prototype for socket operation handling routines
typedef
VOID
(*PWSKSAMPLE_OP_HANDLER_FN)(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext);



// Structure that represents a work queue processed by a dedicated worker thread
typedef class WsksampleWorkQueue {
public:
    // Initialize a given work queue and start the worker thread for it
    NTSTATUS Start();

    // Stop a given work queue and wait for its worker thread to exit
    VOID Stop();


public:
    // List head
    SLIST_HEADER Head;

    // Wake up event
    KEVENT Event;

    // Worker thread can exit safely only if queue is empty and Stop is TRUE
    BOOLEAN isStop;

    // Worker thread pointer
    PETHREAD Thread;

} *PWsksampleWorkQueue;



// Structure that represents the context for a WSK socket operation.
typedef struct _WSKSAMPLE_SOCKET_OP_CONTEXT {

    // Work queue linkage
    SLIST_ENTRY QueueEntry;

    // Pointer to the function that will handle the operation 
    PWSKSAMPLE_OP_HANDLER_FN OpHandler;

    PVOID Args;

    // Pointer to the WSK socket context.
    PWSKSAMPLE_SOCKET_CONTEXT SocketContext;

    // IRP to use for the operation
    PIRP Irp;

    // Data buffer and MDL used by send and receive operations
    _Field_size_bytes_part_(BufferLength, DataLength) PVOID  DataBuffer;
    PMDL   DataMdl;
    SIZE_T BufferLength; // size of the buffer
    SIZE_T DataLength;   // length of actual data stored in the buffer

} WSKSAMPLE_SOCKET_OP_CONTEXT;

// Maximum number of operations that can be outstanding on a socket at any time 
#define WSKSAMPLE_OP_COUNT 2

// Structure that represents the context for a WSK socket.
typedef struct _WSKSAMPLE_SOCKET_CONTEXT {

    PWskSampleServer Server;

    LIST_ENTRY       SocketContextEntry;

    KEVENT           SyncEvent;

    // Pointer to the WSK socket.
    PWSK_SOCKET Socket;

    // Work queue used for enqueueing operations on the socket
    PWsksampleWorkQueue WorkQueue;

    // Socket is being closed.
    BOOLEAN Closing;

    // Peer has gracefully disconnected its half of the connection and we are
    // about to disconnect (or have disconnected) our half.
    BOOLEAN Disconnecting;

    // Stop accepting incoming connections. Valid for listening sockets only.
    BOOLEAN StopListening;

    // Embedded array of contexts for outstanding operations on the socket.
    // Note that operation contexts could also be allocated separately. This
    // sample preallocates a fixed number of operation contexts along with
    // the socket context for a new socket.
    WSKSAMPLE_SOCKET_OP_CONTEXT OpContext[WSKSAMPLE_OP_COUNT];

} WSKSAMPLE_SOCKET_CONTEXT;

// Forward declaration for WskAcceptEvent in WSK_CLIENT_LISTEN_DISPATCH 
NTSTATUS
WSKAPI
WskSampleAcceptEvent(
    _In_  PVOID         SocketContext,
    _In_  ULONG         Flags,
    _In_  PSOCKADDR     LocalAddress,
    _In_  PSOCKADDR     RemoteAddress,
    _In_opt_  PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID *AcceptSocketContext,
    _Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch
);

struct WskSampleServer {
    // The socket context for the listening socket
    PWSKSAMPLE_SOCKET_CONTEXT m_ListeningSocketContext;
    PWsksampleWorkQueue       m_WorkQueue;

    ADDRESS_FAMILY            m_AddressFamily = AF_INET; // AF_INET6;
    USHORT                    m_SocketType    = SOCK_STREAM;
    ULONG                     m_Protocol      = IPPROTO_TCP;
    ULONG                     m_Flags         = WSK_FLAG_LISTEN_SOCKET;

    LIST_ENTRY                m_ClientsList;
    KMUTEX                    m_ClientsMutex;
    KSPIN_LOCK                m_ClientsSpinLock;

    BOOLEAN                   m_isStarted;

    // Socket-level callback table for listening sockets
    const WSK_CLIENT_LISTEN_DISPATCH WskSampleClientListenDispatch = {
        WskSampleAcceptEvent,
        NULL, // WskInspectEvent is required only if conditional-accept is used.
        NULL  // WskAbortEvent is required only if conditional-accept is used.
    };

    SOCKADDR_IN               m_ListeningAddress = { AF_INET, RtlUshortByteSwap((USHORT)8086), INADDR_ANY, 0 };
    
    // IPv6 wildcard address and port number 40007 to listen on
    SOCKADDR_IN6              m_IPv6ListeningAddress = {
                    AF_INET6,
                    0x479c, // 40007 in hex in network byte order 
                    0,
                    IN6ADDR_ANY_INIT,
                    0 };

    WskSampleServer(PWsksampleWorkQueue WorkQueue);
    WskSampleServer() { WskSampleServer(NULL); }

    NTSTATUS
        Start(
        );
    VOID
        Stop(
        );

    BOOLEAN isRun() { return m_isStarted; }

    NTSTATUS AddClientSocketContext(
        PWSKSAMPLE_SOCKET_CONTEXT SocketContext
    );

    NTSTATUS RemoveClientSocketContext(
        PWSKSAMPLE_SOCKET_CONTEXT SocketContext
    );

    NTSTATUS SendData(
        PWSK_BUF Buf, BOOLEAN IsBlocked
    );
};



_Must_inspect_result_
__drv_allocatesMem(Mem)
_Success_(return != NULL)
PWSKSAMPLE_SOCKET_CONTEXT
WskSampleAllocateSocketContext(
    _In_ PWskSampleServer Server,
    _In_ ULONG BufferLength
);

_At_(SocketContext, __drv_freesMem(Mem))
VOID
WskSampleFreeSocketContext(
    _In_ PWSKSAMPLE_SOCKET_CONTEXT SocketContext
);

_At_(SocketOpContext, __drv_aliasesMem)
VOID
WskSampleEnqueueOp(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext,
    _In_ PWSKSAMPLE_OP_HANDLER_FN OpHandler,
    _In_ PVOID Args
);

VOID
WskSampleWorkerThread(
    _In_ PVOID Context
);

VOID
WskSampleSetupListeningSocket(
    _In_ PWSK_PROVIDER_NPI WskProviderNpi,
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpStartListen(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpStopListen(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpReceive(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpSend(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpSendBlocked(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpDisconnect(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpClose(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

VOID
WskSampleOpFree(
    _In_ PWSKSAMPLE_SOCKET_OP_CONTEXT SocketOpContext
);

#endif