#include "Curl.h"

#include <node_buffer.h>
#include <curl/curl.h>
#include <iostream>
#include <stdlib.h>
#include <string.h> //cstring?

// Set curl constants
#include "generated-stubs/curlOptionsString.h"
#include "generated-stubs/curlOptionsInteger.h"
#include "generated-stubs/curlOptionsFunction.h"
#include "generated-stubs/curlInfosString.h"
#include "generated-stubs/curlInfosInteger.h"
#include "generated-stubs/curlInfosDouble.h"

#include "generated-stubs/curlAuth.h"
#include "generated-stubs/curlProtocols.h"
#include "generated-stubs/curlPause.h"
#include "generated-stubs/curlHttp.h"


#define X(name) {#name, CURLOPT_##name}
Curl::CurlOption curlOptionsLinkedList[] = {
#if LIBCURL_VERSION_NUM >= 0x070a03
    X(HTTP200ALIASES),
#endif

#if LIBCURL_VERSION_NUM >= 0x071400
    X(MAIL_RCPT),
#endif

#if LIBCURL_VERSION_NUM >= 0x071503
    X(RESOLVE),
#endif

    //@TODO ADD SUPPORT FOR CURLOPT_HEADEROPT AND CURLOPT_PROXYHEADER
    X(HTTPPOST),
    X(HTTPHEADER),
    X(QUOTE),
    X(POSTQUOTE),
    X(PREQUOTE),
    X(TELNETOPTIONS)
};
#undef X

#define X(name) {#name, CURLINFO_##name}
Curl::CurlOption curlInfosLinkedList[] = {
    X(SSL_ENGINES),
    X(COOKIELIST)
};
#undef X

#define X(name) {#name, CurlHttpPost::name}
Curl::CurlOption curlHttpPostOptions[] = {
    X(NAME),
    X(FILE),
    X(CONTENTS),
    X(TYPE)
};
#undef X

//Make string all uppercase
void stringToUpper( std::string &s )
{
    for( unsigned int i = 0; i < s.length(); i++ )
        s[i] = toupper( s[i] );
}

//Function that checks if given option is inside the given Curl::CurlOption struct, if it is, returns the optionId
#define isInsideOption( options, option ) isInsideCurlOption( options, sizeof( options ), option )
int isInsideCurlOption( const Curl::CurlOption *curlOptions, const int lenOfOption, const v8::Handle<v8::Value> &option ) {

    v8::HandleScope scope;

    bool isString = option->IsString();
    bool isInt    = option->IsInt32();

    std::string optionName = "";
    int32_t optionId = -1;

    if ( !isString && !isInt ) {
        return 0;
    }

    if ( isString ) {

        v8::String::Utf8Value optionNameV8( option );

        optionName = std::string( *optionNameV8 );

        stringToUpper( optionName );

    } else { //int

        optionId = option->ToInteger()->Int32Value();

    }

    for ( uint32_t len = lenOfOption / sizeof( Curl::CurlOption ), i = 0; i < len; ++i ) {

        const Curl::CurlOption &curr = curlOptions[i];

        if ( (isString && curr.name == optionName) || (isInt && curr.value == optionId)  )
            return curlOptions[i].value;
    }

    return 0;
}

//Initialize maps
curlMapId optionsMapId;
curlMapName optionsMapName;

curlMapId infosMapId;
curlMapName infosMapName;

//Initialize static properties
v8::Persistent<v8::Function> Curl::constructor;
CURLM  *Curl::curlMulti      = NULL;
int     Curl::runningHandles = 0;
int     Curl::count          = 0;
std::map< CURL*, Curl* > Curl::curls;
uv_timer_t Curl::curlTimeout;

int v8AllocatedMemoryAmount = 4*4096;

// Add Curl constructor to the module exports
void Curl::Initialize( v8::Handle<v8::Object> exports ) {

    v8::HandleScope scope;

    //*** Initialize cURL ***//
    CURLcode code = curl_global_init( CURL_GLOBAL_ALL );
    if ( code != CURLE_OK ) {
        Curl::Raise( "curl_global_init failed!" );
        return;
    }

    Curl::curlMulti = curl_multi_init();
    if ( Curl::curlMulti == NULL ) {
        Curl::Raise( "curl_multi_init failed!" );
        return;
    }

    //init uv timer to be used with HandleTimeout
    int timerStatus = uv_timer_init( uv_default_loop(), &Curl::curlTimeout );
    assert( timerStatus == 0 );

    //set curl_multi callbacks to use libuv
    curl_multi_setopt( Curl::curlMulti, CURLMOPT_SOCKETFUNCTION, Curl::HandleSocket );
    curl_multi_setopt( Curl::curlMulti, CURLMOPT_TIMERFUNCTION, Curl::HandleTimeout );

    //** Construct Curl js "class"
    v8::Handle<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New( Curl::New );

    tpl->SetClassName( v8::String::NewSymbol( "Curl" ) );
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 ); //to wrap this

    // Prototype Methods
    NODE_SET_PROTOTYPE_METHOD( tpl, "_setOpt", Curl::SetOpt );
    NODE_SET_PROTOTYPE_METHOD( tpl, "_getInfo", Curl::GetInfo );
    NODE_SET_PROTOTYPE_METHOD( tpl, "_perform", Curl::Perform );
    NODE_SET_PROTOTYPE_METHOD( tpl, "_pause", Curl::Pause );
    NODE_SET_PROTOTYPE_METHOD( tpl, "_reset", Curl::Reset );
    NODE_SET_PROTOTYPE_METHOD( tpl, "_close", Curl::Close );

    // Static Methods
    NODE_SET_METHOD( tpl , "getCount" , GetCount );
    NODE_SET_METHOD( tpl , "getVersion" , GetVersion );

    // Export cURL Constants
    v8::Handle<v8::Function> tplFunction = tpl->GetFunction();

    v8::Handle<v8::Object> optionsObj   = v8::Object::New();
    v8::Handle<v8::Object> infosObj     = v8::Object::New();
    v8::Handle<v8::Object> protocolsObj = v8::Object::New();
    v8::Handle<v8::Object> pauseObj     = v8::Object::New();
    v8::Handle<v8::Object> authObj      = v8::Object::New();
    v8::Handle<v8::Object> httpObj      = v8::Object::New();

    Curl::ExportConstants( &optionsObj, curlOptionsString, sizeof( curlOptionsString ), &optionsMapId, &optionsMapName );
    Curl::ExportConstants( &optionsObj, curlOptionsInteger, sizeof( curlOptionsInteger ), &optionsMapId, &optionsMapName );
    Curl::ExportConstants( &optionsObj, curlOptionsFunction, sizeof( curlOptionsFunction ), &optionsMapId, &optionsMapName );
    Curl::ExportConstants( &optionsObj, curlOptionsLinkedList, sizeof( curlOptionsLinkedList ), &optionsMapId, &optionsMapName );

    Curl::ExportConstants( &infosObj, curlInfosString, sizeof( curlInfosString ), &infosMapId, &infosMapName );
    Curl::ExportConstants( &infosObj, curlInfosInteger, sizeof( curlInfosInteger ), &infosMapId, &infosMapName );
    Curl::ExportConstants( &infosObj, curlInfosDouble, sizeof( curlInfosDouble ), &infosMapId, &infosMapName );
    Curl::ExportConstants( &infosObj, curlInfosLinkedList, sizeof( curlInfosLinkedList ), &infosMapId, &infosMapName );

    Curl::ExportConstants( &authObj, curlAuth, sizeof( curlAuth ), nullptr, nullptr );
    Curl::ExportConstants( &httpObj, curlHttp, sizeof( curlHttp ), nullptr, nullptr );
    Curl::ExportConstants( &pauseObj, curlPause, sizeof( curlPause ), nullptr, nullptr );
    Curl::ExportConstants( &protocolsObj, curlProtocols, sizeof( curlProtocols ), nullptr, nullptr );


    //Add function properties (marking them as readonly)
    tplFunction->Set( v8::String::NewSymbol( "option" ), optionsObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );
    tplFunction->Set( v8::String::NewSymbol( "info" ),     infosObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );

    tplFunction->Set( v8::String::NewSymbol( "auth" ), authObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );
    tplFunction->Set( v8::String::NewSymbol( "http" ), httpObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );
    tplFunction->Set( v8::String::NewSymbol( "pause" ), pauseObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );
    tplFunction->Set( v8::String::NewSymbol( "protocol" ), protocolsObj, static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );

    //Static members
    tplFunction->Set( v8::String::NewSymbol( "VERSION_NUM" ), v8::Integer::New( LIBCURL_VERSION_NUM ), static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );
    tplFunction->Set( v8::String::NewSymbol( "_v8m" ), v8::Integer::New( v8AllocatedMemoryAmount ), static_cast<v8::PropertyAttribute>( v8::ReadOnly|v8::DontDelete ) );

    //Creates the Constructor from the template and assign it to the static constructor property for future use.
    Curl::constructor = v8::Persistent<v8::Function>::New( tplFunction );

    exports->Set( v8::String::NewSymbol( "Curl" ), Curl::constructor );
}

Curl::Curl( v8::Handle<v8::Object> obj ) : isInsideMultiCurl( false )
{
    ++Curl::count;

    obj->SetPointerInInternalField( 0, this );

    this->handle = v8::Persistent<v8::Object>::New( obj );
    handle.MakeWeak( this, Curl::Destructor );

    this->curl = curl_easy_init();

    if ( !this->curl ) {

        Curl::Raise( "curl_easy_init Failed!" );
        return;
    }

    this->callbacks.isProgressCbAlreadyAborted = false;

    //set callbacks
    curl_easy_setopt( this->curl, CURLOPT_WRITEFUNCTION, Curl::WriteFunction );
    curl_easy_setopt( this->curl, CURLOPT_WRITEDATA, this );
    curl_easy_setopt( this->curl, CURLOPT_HEADERFUNCTION, Curl::HeaderFunction );
    curl_easy_setopt( this->curl, CURLOPT_HEADERDATA, this );

    Curl::curls[curl] = this;
}

Curl::~Curl(void)
{
    --Curl::count;

    //"return" the memory allocated by the object
    //v8::V8::AdjustAmountOfExternalAllocatedMemory( -v8AllocatedMemoryAmount );

    //cleanup curl related stuff
    if ( this->curl ) {

        if ( this->isInsideMultiCurl ) {

            curl_multi_remove_handle( this->curlMulti, this->curl );
        }

        Curl::curls.erase( this->curl );
        curl_easy_cleanup( this->curl );

    }

    for ( std::vector<curl_slist*>::iterator it = this->curlLinkedLists.begin(), end = this->curlLinkedLists.end(); it != end; ++it ) {

        curl_slist *linkedList = *it;

        if ( linkedList ) {

            curl_slist_free_all( linkedList );
        }
    }

    //dispose persistent callbacks
    this->DisposeCallbacks();
}

//Dispose persistent handler, and delete itself
void Curl::Dispose()
{
    this->handle->SetPointerInInternalField( 0, NULL );

    this->handle.Dispose();
    this->handle.Clear();

    delete this;
}

//The curl_multi_socket_action(3) function informs the application about updates
//  in the socket (file descriptor) status by doing none, one, or multiple calls to this function
int Curl::HandleSocket( CURL *easy, curl_socket_t s, int action, void *userp, void *socketp )
{
    CurlSocketContext *ctx;
    uv_err_s error;

    if ( action == CURL_POLL_IN || action == CURL_POLL_OUT || action == CURL_POLL_INOUT || action == CURL_POLL_NONE ) {

        //create ctx if it doesn't exists and assign it to the current socket,
        ctx = ( socketp ) ? static_cast<Curl::CurlSocketContext*>(socketp) : Curl::CreateCurlSocketContext( s );
        curl_multi_assign( Curl::curlMulti, s, static_cast<void*>( ctx ) );

        //set event based on the current action
        int events = 0;

        switch ( action ) {

        case CURL_POLL_IN:
            events |= UV_READABLE;
            break;
        case CURL_POLL_OUT:
            events |= UV_WRITABLE;
            break;
        case CURL_POLL_INOUT:
            events |= UV_READABLE | UV_WRITABLE;
            break;
        }

        //call process when possible
        return uv_poll_start( &ctx->pollHandle, events, Curl::Process );
    }

    //action == CURL_POLL_REMOVE
    if ( action == CURL_POLL_REMOVE && socketp ) {

        ctx = static_cast<CurlSocketContext*>( socketp );

        uv_poll_stop( &ctx->pollHandle );
        curl_multi_assign( Curl::curlMulti, s, NULL );

        Curl::DestroyCurlSocketContext( ctx );

        return 0;
    }

    //this should NEVER happen, I don't even know why this is here.
    error = uv_last_error( uv_default_loop() );
    std::cerr << uv_err_name( error ) << " " << uv_strerror( error );
    abort();
}

//Creates a Context to be used to store data between events
Curl::CurlSocketContext* Curl::CreateCurlSocketContext( curl_socket_t sockfd )
{
    int r;
    uv_err_s error;
    Curl::CurlSocketContext *ctx = NULL;

    ctx = static_cast<Curl::CurlSocketContext*>( malloc( sizeof( *ctx ) ) );

    ctx->sockfd = sockfd;

    //uv_poll simply watches file descriptors using the operating system notification mechanism
    //Whenever the OS notices a change of state in file descriptors being polled, libuv will invoke the associated callback.
    r = uv_poll_init_socket( uv_default_loop(), &ctx->pollHandle, sockfd );

    if ( r == -1 ) {

        error = uv_last_error( uv_default_loop() );
        std::cerr << uv_err_name( error ) << uv_strerror( error );
        abort();

    } else {

        ctx->pollHandle.data = ctx;
    }

    return ctx;
}

//This function will be called when the timeout value changes from LibCurl.
//The timeout value is at what latest time the application should call one of
//the "performing" functions of the multi interface (curl_multi_socket_action(3) and curl_multi_perform(3)) - to allow libcurl to keep timeouts and retries etc to work.
int Curl::HandleTimeout( CURLM *multi /* multi handle */ , long timeoutMs /* timeout in milliseconds */ , void *userp /* TIMERDATA */ )
{
    //A timeout value of -1 means that there is no timeout at all, and 0 means that the timeout is already reached.
    if ( timeoutMs <= 0 )
        timeoutMs = 1; //but we are going to wait a little

    return uv_timer_start( &Curl::curlTimeout, Curl::OnTimeout, timeoutMs, 0 );
}

//Function called when the previous timeout set reaches 0
void Curl::OnTimeout( uv_timer_t *req, int status )
{
    //timeout expired, let libcurl update handlers and timeouts
    curl_multi_socket_action( Curl::curlMulti, CURL_SOCKET_TIMEOUT, 0, &Curl::runningHandles );

    Curl::ProcessMessages();
}

//Called when libcurl thinks there is something to process
void Curl::Process( uv_poll_t* handle, int status, int events )
{
    //stop the timer, so curl_multi_socket_action is fired without a socket by the timeout cb
    uv_timer_stop( &Curl::curlTimeout );

    int flags = 0;

    CURLMcode code;

    if ( events & UV_READABLE ) flags |= CURL_CSELECT_IN;
    if ( events & UV_WRITABLE ) flags |= CURL_CSELECT_OUT;

    CurlSocketContext *ctx;

    ctx = static_cast<CurlSocketContext*>( handle->data );

    do {

        code = curl_multi_socket_action( Curl::curlMulti, ctx->sockfd, flags, &Curl::runningHandles );

    } while ( code == CURLM_CALL_MULTI_PERFORM ); //@todo is that loop really needed?

    if ( code != CURLM_OK ) {

        Curl::Raise( "curl_multi_socket_actioon Failed", curl_multi_strerror( code ) );
        return;
    }

    Curl::ProcessMessages();
}

void Curl::ProcessMessages()
{
    CURLMcode code;
    CURLMsg *msg = NULL;
    int pending = 0;

    while( ( msg = curl_multi_info_read( Curl::curlMulti, &pending ) ) ) {

        if ( msg->msg == CURLMSG_DONE ) {

            Curl *curl = Curl::curls[msg->easy_handle];

            CURLcode statusCode = msg->data.result;

            code = curl_multi_remove_handle( Curl::curlMulti, msg->easy_handle );

            curl->isInsideMultiCurl = false;

            if ( code != CURLM_OK ) {
                Curl::Raise( "curl_multi_remove_handle Failed", curl_multi_strerror( code ) );
                return;
            }

            if ( statusCode == CURLE_OK ) {

                curl->OnEnd();

            } else {

                curl->OnError( statusCode );
            }
        }
    }
}

//Called when libcurl thinks the socket can be destroyed
void Curl::DestroyCurlSocketContext( Curl::CurlSocketContext* ctx )
{
    uv_handle_t *handle = (uv_handle_t*) &ctx->pollHandle;

    uv_close( handle, Curl::OnCurlSocketClose );
}
void Curl::OnCurlSocketClose( uv_handle_t *handle )
{
    CurlSocketContext *ctx = static_cast<CurlSocketContext*>( handle->data );
    free( ctx );
}

//Called by libcurl when some chunk of data (from body) is available
size_t Curl::WriteFunction( char *ptr, size_t size, size_t nmemb, void *userdata )
{
    Curl *obj = static_cast<Curl*>( userdata );
    return obj->OnData( ptr, size, nmemb );
}

//Called by libcurl when some chunk of data (from headers) is available
size_t Curl::HeaderFunction( char *ptr, size_t size, size_t nmemb, void *userdata )
{
    Curl *obj = static_cast<Curl*>( userdata );
    return obj->OnHeader( ptr, size, nmemb );
}

size_t Curl::OnData( char *data, size_t size, size_t nmemb )
{
    //@TODO If the callback close the connection, an error will be throw!
    //@TODO Implement: From 7.18.0, the function can return CURL_WRITEFUNC_PAUSE which then will cause writing to this connection to become paused. See curl_easy_pause(3) for further details.
    v8::HandleScope scope;

    size_t n = size * nmemb;

    node::Buffer *buffer = node::Buffer::New( data, n );
    v8::Handle<v8::Value> argv[] = { buffer->handle_ };

    v8::Handle<v8::Value> retVal = node::MakeCallback( this->handle, "_onData", 1, argv );

    size_t ret = n;

    if ( retVal.IsEmpty() ) {
            ret = 0;
    } else {
            ret = retVal->Int32Value();
    }

    return ret;
}


size_t Curl::OnHeader( char *data, size_t size, size_t nmemb )
{
    v8::HandleScope scope;

    size_t n = size * nmemb;

    node::Buffer * buffer = node::Buffer::New( data, n );
    v8::Handle<v8::Value> argv[] = { buffer->handle_ };
    v8::Handle<v8::Value> retVal = node::MakeCallback( this->handle, "_onHeader", 1, argv );

    size_t ret = n;

    if ( retVal.IsEmpty() )
        ret = 0;
    else
        ret = retVal->Int32Value();

    return ret;
}

void Curl::OnEnd()
{
    v8::HandleScope scope;

    node::MakeCallback( this->handle, "_onEnd", 0, NULL );
}

void Curl::OnError( CURLcode errorCode )
{
    v8::HandleScope scope;

    v8::Handle<v8::Value> argv[] = { v8::Exception::Error( v8::String::New( curl_easy_strerror( errorCode ) ) ), v8::Integer::New( errorCode )  };
    node::MakeCallback( this->handle, "_onError", 2, argv );
}

void Curl::DisposeCallbacks()
{
    if ( !this->callbacks.progress.IsEmpty() ) {
        this->callbacks.progress.Dispose();
        this->callbacks.progress.Clear();
    }

    if ( !this->callbacks.xferinfo.IsEmpty() ) {
        this->callbacks.xferinfo.Dispose();
        this->callbacks.xferinfo.Clear();
    }

    if ( !this->callbacks.debug.IsEmpty() ) {
        this->callbacks.debug.Dispose();
        this->callbacks.debug.Clear();
    }
}

//Export Options/Infos to constants in the given Object, and add their mapping to the respective maps.
template<typename T>
void Curl::ExportConstants( T *obj, Curl::CurlOption *optionGroup, uint32_t len, curlMapId *mapId, curlMapName *mapName )
{
    v8::HandleScope scope;

    len = len / sizeof( Curl::CurlOption );

    if ( !obj ) { //Null pointer, just stop
        return; //
    }

    for ( uint32_t i = 0; i < len; ++i ) {

        const Curl::CurlOption &option = optionGroup[i];

        const int32_t *optionId = &option.value;

        //I don't like to mess with memory
        std::string sOptionName( option.name );


        (*obj)->Set(
            v8::String::New( ( sOptionName ).c_str() ),
            v8::Integer::New( option.value ),
            static_cast<v8::PropertyAttribute>( v8::ReadOnly | v8::DontDelete )
        );

        //add to vector, and add pointer to respective map
        //using insert because of http://stackoverflow.com/a/16436560/710693
        if ( mapId && mapName ) {
            mapName->insert( std::make_pair( sOptionName, optionId ) );
            mapId->insert( std::make_pair( optionId, sOptionName ) );
        }
    }
}

// traits class to determine whether to do the check
template <typename> struct ResultCanBeNull : std::false_type {};
template <> struct ResultCanBeNull<char*> : std::true_type {};

template<typename TResultType, typename Tv8MappingType>
v8::Handle<v8::Value> Curl::GetInfoTmpl( const Curl &obj, int infoId )
{
    v8::HandleScope scope;

    TResultType result;

    CURLINFO info = (CURLINFO) infoId;
    CURLcode code = curl_easy_getinfo( obj.curl, info, &result );

    if ( code != CURLE_OK ) {
        Curl::Raise( "curl_easy_getinfo failed!", curl_easy_strerror( code ) );
        return v8::Undefined();
    }

    if ( ResultCanBeNull<TResultType>::value && !result ) //null pointer
        return scope.Close( v8::String::New( "" ) );

    return scope.Close( Tv8MappingType::New( result ) );
}

Curl* Curl::Unwrap( v8::Handle<v8::Object> value )
{
    return static_cast<Curl*>( value->GetPointerFromInternalField( 0 ) );
}

//Create a Exception with the given message and reason
v8::Handle<v8::Value> Curl::Raise( const char *message, const char *reason )
{
    const char *what = message;
    std::string msg;

    if ( reason ) {

        msg = string_format( "%s: %s", message, reason );
        what = msg.c_str();

    }

    return v8::ThrowException( v8::Exception::Error( v8::String::New( what ) ) );
}

//Callbacks
int Curl::CbProgress( void *clientp, double dltotal, double dlnow, double ultotal, double ulnow )
{
    Curl *obj = static_cast<Curl *>( clientp );

    assert( obj );

    if ( obj->callbacks.isProgressCbAlreadyAborted )
        return 1;

    v8::HandleScope scope;

    int32_t retvalInt32;

    v8::Handle<v8::Value> argv[] = {
        v8::Number::New( (double) dltotal ),
        v8::Number::New( (double) dlnow ),
        v8::Number::New( (double) ultotal ),
        v8::Number::New( (double) ulnow )
    };

    //Should handle possible exceptions here?
    v8::Handle<v8::Value> retval = obj->callbacks.progress->Call( obj->handle, 4, argv );

    if ( !retval->IsInt32() ) {

        Curl::Raise( "Return value from the progress callback must be an integer." ) ;

        retvalInt32 = 1;

    } else {

        retvalInt32 = retval->Int32Value();
    }

    if ( retvalInt32 )
        obj->callbacks.isProgressCbAlreadyAborted = true;

    return retvalInt32;
}

int Curl::CbXferinfo( void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
{
    Curl *obj = static_cast<Curl *>( clientp );

    assert( obj );

    if ( obj->callbacks.isProgressCbAlreadyAborted )
        return 1;

    v8::HandleScope scope;

    int32_t retvalInt32;

    v8::Handle<v8::Value> argv[] = {
        v8::Number::New( (double) dltotal ),
        v8::Number::New( (double) dlnow ),
        v8::Number::New( (double) ultotal ),
        v8::Number::New( (double) ulnow )
    };

    v8::Handle<v8::Value> retval = obj->callbacks.xferinfo->Call( obj->handle, 4, argv );

    if ( !retval->IsInt32() ) {

        Curl::Raise( "Return value from the progress callback must be an integer." );

        retvalInt32 = 1;

    } else {

        retvalInt32 = retval->Int32Value();
    }

    if ( retvalInt32 )
        obj->callbacks.isProgressCbAlreadyAborted = true;

    return retvalInt32;
}

int Curl::CbDebug( CURL *handle, curl_infotype type, char *data, size_t size, void *userptr )
{
    Curl *obj = Curl::curls[handle];

    assert( obj );

    v8::HandleScope scope;

    v8::Handle<v8::Value> argv[] = {
        v8::Integer::New( type ),
        v8::String::New( data, static_cast<int>(size) )
    };

    v8::Handle<v8::Value> retval = obj->callbacks.debug->Call( obj->handle, 2, argv );

    int32_t retvalInt = 0;

    if ( !retval->IsInt32() ) {

        Curl::Raise( "Return value from the debug callback must be an integer." );

        retvalInt = 1;

    } else {

        retvalInt = retval->Int32Value();
    }

    return retvalInt;
}

//Javascript Constructor
v8::Handle<v8::Value> Curl::New( const v8::Arguments &args ) {

    v8::HandleScope scope;

    if ( args.IsConstructCall() ) {
        // Invoked as constructor: `new Curl(...)`

        Curl *obj = new Curl( args.This() );

        static v8::Persistent<v8::String> SYM_ON_CREATED = v8::Persistent<v8::String>::New( v8::String::NewSymbol( "_onCreated" ) );
        v8::Handle<v8::Value> cb = obj->handle->Get( SYM_ON_CREATED );

        if ( cb->IsFunction() ) {
            cb->ToObject()->CallAsFunction( obj->handle, 0, NULL );
        }

        return args.This();

    } else {
        // Invoked as plain function `Curl(...)`, turn into construct call.

        const int argc = 1;
        v8::Handle<v8::Value> argv[argc] = { args[0] };

        return scope.Close( constructor->NewInstance( argc, argv ) );
    }
}

//Javascript Constructor
//This is called by v8 when there are no more references to the Curl instance on js.
void Curl::Destructor( v8::Persistent<v8::Value> value, void *data )
{
    v8::Handle<v8::Object> object = value->ToObject();
    Curl * curl = static_cast<Curl*>( object->GetPointerFromInternalField( 0 ) );
    curl->Dispose();
}

v8::Handle<v8::Value> Curl::SetOpt( const v8::Arguments &args ) {

    v8::HandleScope scope;

    Curl *obj = Curl::Unwrap( args.This() );

    v8::Handle<v8::Value> opt   = args[0];
    v8::Handle<v8::Value> value = args[1];

    v8::Handle<v8::Integer> optCallResult = v8::Integer::New( CURLE_FAILED_INIT );

    int optionId;

    //check if option is linked list, and the value is correct
    if ( ( optionId = isInsideOption( curlOptionsLinkedList, opt ) ) ) {

        //special case, array of objects
        if ( optionId == CURLOPT_HTTPPOST ) {

            std::string invalidArrayMsg = "Option value should be an Array of Objects.";

            if ( !value->IsArray() ) {
                v8::ThrowException(v8::Exception::TypeError(
                    v8::String::New( invalidArrayMsg.c_str() )
                ));
                return v8::Undefined();
            }

            CurlHttpPost &httpPost = obj->httpPost;

            v8::Handle<v8::Array> rows = v8::Handle<v8::Array>::Cast( value );

            //we could append, but lets reset, that is the desired behavior
            httpPost.reset();

            // [{ key : val }]
            for ( uint32_t i = 0, len = rows->Length(); i < len; ++i ) {

                // not an array of objects
                if ( !rows->Get( i )->IsObject() ) {
                    v8::ThrowException(v8::Exception::TypeError(
                        v8::String::New( invalidArrayMsg.c_str() )
                    ));
                    return v8::Undefined();
                }

                v8::Handle<v8::Object> postData = v8::Handle<v8::Object>::Cast( rows->Get( i ) );

                httpPost.append();

                const v8::Handle<v8::Array> props = postData->GetPropertyNames();
                const uint32_t postDataLength = props->Length();

                for ( uint32_t j = 0 ; j < postDataLength ; ++j ) {

                    int httpPostId = -1;

                    const v8::Handle<v8::Value> postDataKey = props->Get( j );
                    const v8::Handle<v8::Value> postDataValue = postData->Get( postDataKey );

                    //convert postDataKey to field id
                    v8::String::Utf8Value fieldName( postDataKey );
                    std::string optionName = std::string( *fieldName );
                    stringToUpper( optionName );

                    for ( uint32_t k = 0, kLen = sizeof( curlHttpPostOptions ) / sizeof( Curl::CurlOption ); k < kLen; ++k ) {

                        if ( curlHttpPostOptions[k].name == optionName )
                            httpPostId = curlHttpPostOptions[k].value;

                    }

                    //Property not found
                    if ( httpPostId == -1 ) {

                        std::string errorMsg = string_format( "Invalid property \"%s\" given.", *fieldName );
                        Curl::Raise( errorMsg.c_str() );
                        return v8::Undefined();
                    }


                    //Check if value is a string.
                    if ( !postDataValue->IsString() ) {

                        std::string errorMsg = string_format( "Value for property \"%s\" should be a string.", *fieldName );
                        Curl::Raise( errorMsg.c_str() );
                        return v8::Undefined();

                    }

                    v8::String::Utf8Value postDataValueAsString( postDataValue );

                    httpPost.set( httpPostId, *postDataValueAsString, postDataValueAsString.length() );
                }
            }

            optCallResult = v8::Integer::New( curl_easy_setopt( obj->curl, CURLOPT_HTTPPOST, obj->httpPost.first ) );


        } else {

            if ( value->IsNull() ) {
                optCallResult = v8::Integer::New(
                    curl_easy_setopt(
                        obj->curl, (CURLoption) optionId, NULL
                    )
                );

            } else {

                if ( !value->IsArray() ) {
                    v8::ThrowException(v8::Exception::TypeError(
                        v8::String::New( "Option value should be an array." )
                    ));
                    return v8::Undefined();
                }

                //convert value to curl linked list (curl_slist)
                curl_slist *slist = NULL;
                v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast( value );

                for ( uint32_t i = 0, len = array->Length(); i < len; ++i )
                {
                    slist = curl_slist_append( slist, *v8::String::Utf8Value( array->Get( i ) ) );
                }

                obj->curlLinkedLists.push_back( slist );

                optCallResult = v8::Integer::New(
                    curl_easy_setopt(
                        obj->curl, (CURLoption) optionId, slist
                    )
                );
            }
        }

        //check if option is string, and the value is correct
    } else if ( ( optionId = isInsideOption( curlOptionsString, opt ) ) ) {

        if ( !value->IsString() ) {
            v8::ThrowException(v8::Exception::TypeError(
                v8::String::New( "Option value should be a string." )
            ));
            return v8::Undefined();
        }

        // Create a string copy
        bool isNull = value->IsNull();

        if ( !isNull ) {

            //Curl don't copies the string before version 7.17
            v8::String::Utf8Value value( args[1] );
            int length = value.length();
            obj->curlStrings[optionId] = std::string( *value, length );
        }

        optCallResult = v8::Integer::New(
            curl_easy_setopt(
                obj->curl, (CURLoption) optionId, ( !isNull ) ? obj->curlStrings[optionId].c_str() : NULL
            )
        );


        //check if option is a integer, and the value is correct
    } else if ( ( optionId = isInsideOption( curlOptionsInteger, opt ) )  ) {

        int32_t val = value->Int32Value();

        //If not integer, but a not falsy value, val = 1
        if ( !value->IsInt32() ) {
            val = value->BooleanValue();
        }

        optCallResult = v8::Integer::New(
            curl_easy_setopt(
                obj->curl, (CURLoption) optionId, val
            )
        );

    } else if ( ( optionId = isInsideOption( curlOptionsFunction, opt ) ) ) {

        if ( !value->IsFunction() ) {
            Curl::Raise( "Option value must be a function." );
            return v8::Undefined();
        }

        v8::Handle<v8::Function> callback = value.As<v8::Function>();

        switch ( optionId ) {

#if LIBCURL_VERSION_NUM >= 0x072000
            /* xferinfo was introduced in 7.32.0, no earlier libcurl versions will compile as they won't have the symbols around.
                New libcurls will prefer the new callback and instead use that one even if both callbacks are set. */
            case CURLOPT_XFERINFOFUNCTION:

                obj->callbacks.xferinfo = v8::Persistent<v8::Function>::New( callback );
                curl_easy_setopt( obj->curl, CURLOPT_XFERINFODATA, obj );
                optCallResult = v8::Integer::New( curl_easy_setopt( obj->curl, CURLOPT_XFERINFOFUNCTION, Curl::CbXferinfo ) );

                break;
#endif

            case CURLOPT_PROGRESSFUNCTION:

                obj->callbacks.progress = v8::Persistent<v8::Function>::New( callback );
                curl_easy_setopt( obj->curl, CURLOPT_PROGRESSFUNCTION, obj );
                optCallResult = v8::Integer::New( curl_easy_setopt( obj->curl, CURLOPT_PROGRESSFUNCTION, Curl::CbProgress ) );

                break;

            case CURLOPT_DEBUGFUNCTION:

                obj->callbacks.debug = v8::Persistent<v8::Function>::New( callback );
                curl_easy_setopt( obj->curl, CURLOPT_DEBUGFUNCTION, obj );
                optCallResult = v8::Integer::New( curl_easy_setopt( obj->curl, CURLOPT_DEBUGFUNCTION, Curl::CbDebug ) );

                break;
        }

    }

    CURLcode code = (CURLcode) optCallResult->Int32Value();

    if ( code != CURLE_OK ) {

        Curl::Raise(
            code == CURLE_FAILED_INIT ? "Unknown option given. First argument must be the option internal id or the option name. You can use the Curl.option constants." : curl_easy_strerror( code )
        );
        return v8::Undefined();
    }

    return scope.Close( optCallResult );
}


v8::Handle<v8::Value> Curl::GetInfo( const v8::Arguments &args )
{
    v8::HandleScope scope;

    Curl *obj = Curl::Unwrap( args.This() );

    v8::Handle<v8::Value> infoVal = args[0];

    v8::Handle<v8::Value> retVal = v8::Undefined();

    int infoId;

    CURLINFO info;
    CURLcode code;

    //String
    if ( (infoId = isInsideOption( curlInfosString, infoVal ) ) ) {

        retVal = Curl::GetInfoTmpl<char*, v8::String>( *(obj), infoId );

    //Integer
    } else if ( (infoId = isInsideOption( curlInfosInteger, infoVal ) ) ) {

        retVal = Curl::GetInfoTmpl<long, v8::Integer>(  *(obj), infoId );

    //Double
    } else if ( (infoId = isInsideOption( curlInfosDouble, infoVal ) ) ) {

        retVal = Curl::GetInfoTmpl<double, v8::Number>( *(obj), infoId );

    //Linked list
    } else if ( (infoId = isInsideOption( curlInfosLinkedList, infoVal ) ) ) {

        curl_slist *linkedList;
        curl_slist *curr;

        info = (CURLINFO) infoId;
        code = curl_easy_getinfo( obj->curl, info, &linkedList );

        if ( code != CURLE_OK )
            return scope.Close( Curl::Raise( "curl_easy_getinfo failed!", curl_easy_strerror( code ) ) );

        v8::Handle<v8::Array> arr = v8::Array::New();

        if ( linkedList ) {

            curr = linkedList;

            while ( curr ) {

                arr->Set( arr->Length(), v8::String::New( curr->data ) );
                curr = curr->next;
            }

            curl_slist_free_all( linkedList );
        }

        retVal = arr;
    }

    return scope.Close( retVal );

}

//Add this handle for processing on the curl_multi handler.
v8::Handle<v8::Value> Curl::Perform( const v8::Arguments &args ) {

    v8::HandleScope scope;

    Curl *obj = Curl::Unwrap( args.This() );

    if ( !obj ) {
        Curl::Raise( "Curl is closed." );
        return v8::Undefined();
    }

    //client should not call this method more than one time by request
    if ( obj->isInsideMultiCurl ) {
        Curl::Raise( "Curl session is already running." );
        return v8::Undefined();
    }

    CURLMcode code = curl_multi_add_handle( Curl::curlMulti, obj->curl );

    if ( code != CURLM_OK ) {

        Curl::Raise( "curl_multi_add_handle Failed", curl_multi_strerror( code ) );
        return v8::Undefined();
    }

    obj->isInsideMultiCurl = true;

    return args.This();
}

v8::Handle<v8::Value> Curl::Pause( const v8::Arguments &args )
{
    v8::HandleScope scope;

    Curl *obj = Curl::Unwrap( args.This() );

    if ( !obj ) {
        Curl::Raise( "Curl is closed." );
        return v8::Undefined();
    }

    if ( !args[0]->IsUint32() ) {
        Curl::Raise( "Bitmask value must be an integer." );
        return v8::Undefined();
    }

    int32_t bitmask = args[0]->Int32Value();

    CURLcode code = curl_easy_pause( obj->curl, bitmask );

    if ( code != CURLE_OK ) {
        Curl::Raise( curl_easy_strerror( code ) );
        return v8::Undefined();
    }

    return args.This();
}

v8::Handle<v8::Value> Curl::Close( const v8::Arguments &args )
{
    Curl *obj = Curl::Unwrap( args.This() );

    if ( obj )
        obj->Dispose();

    return args.This();
}

//Re-initializes all options previously set on a specified CURL handle to the default values.
v8::Handle<v8::Value> Curl::Reset( const v8::Arguments &args )
{
    v8::HandleScope scope;

    Curl *obj = Curl::Unwrap( args.This() );

    if ( !obj ) {

        Curl::Raise( "Curl handle is already closed." );
        return v8::Undefined();

    }

    curl_easy_reset( obj->curl );

    // reset the URL, https://github.com/bagder/curl/commit/ac6da721a3740500cc0764947385eb1c22116b83
    curl_easy_setopt( obj->curl, CURLOPT_URL, "" );

    obj->DisposeCallbacks();

    return args.This();
}

//returns the amount of curl instances
v8::Handle<v8::Value> Curl::GetCount( const v8::Arguments &args )
{
    v8::HandleScope scope;
    return scope.Close( v8::Integer::New( Curl::count ) );
}

//Returns a human readable string with the version number of libcurl and some of its important components (like OpenSSL version).
v8::Handle<v8::Value> Curl::GetVersion( const v8::Arguments &args )
{
    v8::HandleScope scope;

    const char *version = curl_version();

    v8::Handle<v8::Value> versionObj = v8::String::New( version );

    return scope.Close( versionObj );
}
