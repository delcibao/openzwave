//-----------------------------------------------------------------------------
//	Open Z-Wave Monitor Program.
//-----------------------------------------------------------------------------
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <iostream>
#include <list>
#include <string>
#include <ctime>
//
#include <openzwave/Options.h>
#include <openzwave/Manager.h>
#include <openzwave/Driver.h>
#include <openzwave/Node.h>
#include <openzwave/Group.h>
#include <openzwave/Notification.h>
#include <openzwave/value_classes/ValueStore.h>
#include <openzwave/value_classes/Value.h>
#include <openzwave/value_classes/ValueBool.h>
#include <openzwave/platform/Log.h>
#include <openzwave/value_classes/ValueID.h>
#include <openzwave/command_classes/SwitchBinary.h>
// 
#include "mongoose.h"
#include "Notify.h"

#define NUM_NODES 10

#define Event_Detected  0x06
#define Tamper_Switch   0x07
#define Cover_Ajar      0x00
#define Open            0xFF
#define Close           0x00
#define Sw_On              1
#define Sw_Off             0
#define Switch_Command  0x25

#ifdef DEBUG2
    #warning In debug mode
#endif



#define MainController  1       //Main Controller, USB Stick
#define WindowKitchen   2       // Magnetic switch sensor, Kitchen Windows.
#define FrontDoor       3       // Magnetic switch sensor, front door.
#define LivingRoom      4       // Movement sensor, living Room.
#define Switch1         5       // Remote controlled outlet, TBD.

using namespace OpenZWave;

bool temp = false;

bool AlarmEnabled = 0;
bool SensorTriggered = 0;

string status1; 
bool switch_value = 0;
bool prev_switch_value=0;
bool print_info = 0;

static uint32_t g_homeId = 0;
static bool g_initFailed = false;

Notify notification;

typedef struct
{
	uint32_t	m_homeId;
	uint8_t 	m_nodeId;
	bool		m_polled;
	list<ValueID>	m_values;
}NodeInfo;
//openzwave

class MyNode{
public:
//    Sensor(uint8_t id){nodeID = id;}
    uint8_t getID(){return nodeID;};
    void setID( uint8_t _node_ID){ nodeID = _node_ID;};
    bool activity(){ 
        bool ret_val = (detect != prev_detect) ;
        prev_detect = detect;
        return ret_val; };
    uint8_t detect = 0;         // sensor in alarm 
    uint8_t value = 0;          // 
    uint8_t tamper = 0;         // cover ajar. warning.
    time_t LastTimeStamp = 0;
    uint8_t BatteryLevel = 0;
    int32_t WakeupInterval = 0;
    uint8_t prev_detect = 0;
    uint8_t prev_value = 0;
    uint8_t prev_tamper = 0;
    MyNode (){ }
    MyNode (uint8_t _node_ID){ nodeID = _node_ID;};
    ~MyNode (){ }
private:
    uint8_t nodeID = 0;
};

/**
Sensor sensor_window(WindowKitchen);
Sensor sensor_door(FrontDoor);
Sensor sensor_movement(LivingRoom);
*/

MyNode sensor[NUM_NODES];
uint8_t node_list[] = {WindowKitchen, FrontDoor, LivingRoom, Switch1};

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

// Mongoose
static const char *s_http_port = "8080";
static struct mg_serve_http_opts s_http_server_opts;

static void handle_status_call(struct mg_connection *nc, struct http_message *hm)
{
    /* Send headers */
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Content-Type: text/json\r\n"
            "\r\n");
    /* Compute the result and send it back as a JSON object */
    //result = strtod(n1, NULL) + strtod(n2, NULL);

    pthread_mutex_lock(&g_criticalSection);                 
    mg_printf_http_chunk(nc, "{ \"window\": %d, \"door\":%d, \"movement\":%d }", 
    sensor[WindowKitchen].detect,
    sensor[FrontDoor].detect,
    sensor[LivingRoom].detect
    );
    pthread_mutex_unlock(&g_criticalSection);
 
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_switch_call(struct mg_connection *nc, struct http_message *hm)
{
    char n1[10];
    /* Get form variables */
    mg_get_http_var(&hm->body, "switch1", n1, sizeof(n1));
    /* Send headers */
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Content-Type: text/json\r\n"
            "\r\n");

    /* Compute the result and send it back as a JSON object */
    switch_value = atoi(n1);

    mg_printf_http_chunk(nc, "{ \"result\": %d }", switch_value);
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    struct http_message *hm = (struct http_message *) ev_data;
    switch (ev) {
        case MG_EV_HTTP_REQUEST:
            if (mg_vcmp(&hm->uri, "/api/v1/status") == 0) {
                handle_status_call(nc, hm); /* Handle RESTful call */
            } else if (mg_vcmp(&hm->uri, "/api/v1/switch") == 0){
                handle_switch_call(nc, hm); /* Handle RESTful call */
            } else if (mg_vcmp(&hm->uri, "/printcontent") == 0) {
                print_info = 1;
                char buf[100] = {0};
                memcpy(buf, hm->body.p,sizeof(buf) - 1 < hm->body.len ? sizeof(buf) - 1 : hm->body.len);
                printf("%s\n", buf);
                mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
                mg_printf_http_chunk(nc, "<html><head><title>example</title></head><body><p>content printed</p></body></html>");
                mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
            } else {
                mg_serve_http(nc, hm, s_http_server_opts); /* Serve static content */
            }
            break;
        default:
            break;
  }
}

// openzwave
//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Return the NodeInfo object associated with this notification
//-----------------------------------------------------------------------------
NodeInfo* GetNodeInfo
(
	Notification const* _notification
)
{
	uint32_t const homeId = _notification->GetHomeId();
	uint8_t const nodeId = _notification->GetNodeId();
	for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
	{
		NodeInfo* nodeInfo = *it;
		if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
		{
			return nodeInfo;
		}
	}

	return NULL;
}

void SwitchValue(uint8_t _nodeID, bool _val)
{
    for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
        NodeInfo* nodeInfo = *it;
        if( nodeInfo->m_nodeId == _nodeID ) {
            for( list<ValueID>::iterator it2 = nodeInfo->m_values.begin();it2 != nodeInfo->m_values.end(); ++it2 ) {
                ValueID v = *it2;
                if( v.GetCommandClassId() == Switch_Command){
                    Manager::Get()->SetValue(v, _val);
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification
(
	Notification const* _notification,
	void* _context
)
{
    // Must do this inside a critical section to avoid conflicts with the main thread
    pthread_mutex_lock( &g_criticalSection );

    switch( _notification->GetType() )
    {
		case Notification::Type_ValueAdded:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID() );
			}
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				// Remove the value from out list
				for( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
				{
					if( (*it) == _notification->GetValueID() )
					{
						nodeInfo->m_values.erase( it );
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged: 
                {
                    // One of the node values has changed
                   
                    NodeInfo* nodeInfo = GetNodeInfo( _notification );
                    
                    sensor[nodeInfo->m_nodeId].LastTimeStamp = time(0);
                    sensor[nodeInfo->m_nodeId].setID(nodeInfo->m_nodeId);
                    
                    int8_t prev_value = 0;
                    string str0, str1;
                    uint8 value1;
                    bool value2;
#ifdef DEBUG2
                    std::cout << "(Value Changed) Node ID: " << (uint8_t)nodeInfo->m_nodeId+0;

                    for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it)
                    {
                        ValueID v = *it;
                        str0 = Manager::Get()->GetValueLabel(v);
                        
                        Manager::Get()->GetValueAsString(v,&str1);
                        cout << ",[" << (uint8_t)v.GetType()+0 << "]" << str0.c_str() <<  "(0x" << std::hex << std::uppercase << v.GetCommandClassId()+0 << "): " << str1.c_str();
                    }
                    cout << "\n";
#endif
                    std::cout << std::dec << "(Value Changed) Node ID: " << (uint8_t)nodeInfo->m_nodeId+0;
                    for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it){
                        ValueID v = *it;
                        str0 = Manager::Get()->GetValueLabel(v);
                        if(str0 == "Basic") {                                    // Open: 0xFF, Close: 0x00
                            Manager::Get()->GetValueAsByte(v,&value1);
                            std::cout << ", " << str0.c_str() << ": " << value1+0;
                            sensor[nodeInfo->m_nodeId].detect = value1+0;
                        }
                        if(str0 == "Sensor") {                                    // Open: 0xFF, Close: 0x00
                            Manager::Get()->GetValueAsBool(v,&value2);
                            std::cout << ", " << str0.c_str() << ": " << value2+0;
                            sensor[nodeInfo->m_nodeId].detect = value2+0;
                        }
                         if(str0 == "Switch") {
                            Manager::Get()->GetValueAsBool(v,&value2);
                            std::cout << ", " << str0.c_str() << ": " << value2+0;
                            sensor[nodeInfo->m_nodeId].value = value2+0;
                        }
                        if(str0 == "Battery Level"){                            // In percentage
                            Manager::Get()->GetValueAsByte(v,&value1);
                            std::cout << ", " << str0.c_str() << ": " << value1+0;
                            sensor[nodeInfo->m_nodeId].BatteryLevel = value1+0;
                        }
                        if(str0 == "Alarm Type"){                               // Event Detected: 0x06, Tamper Switch: 0x07
                            Manager::Get()->GetValueAsByte(v,&value1);
                            prev_value = value1;
                            if (value1 == Event_Detected)
                                std::cout << ", Event Detected: ";
                            if (value1 == Tamper_Switch)
                                std::cout << ", Tamper Switch: ";
                            if (value1 == Cover_Ajar)
                                 std::cout << ", Cover: ";
                        }
                        if(str0 == "Alarm Level"){                              // Open: 0xFF, Close: 0x00
                            Manager::Get()->GetValueAsByte(v,&value1);
                            if (value1 == Open)
                                std::cout << "Open";
                            if (value1 == Close)
                                std::cout << "Close";
                            if(prev_value == Event_Detected)
                                sensor[nodeInfo->m_nodeId].detect = value1;
                            if((prev_value == Tamper_Switch)||(prev_value == Cover_Ajar))
                                sensor[nodeInfo->m_nodeId].tamper = value1;
                        }
                        if(str0 ==  "Wake-up Interval"){
                            int32_t value;
                            Manager::Get()->GetValueAsInt(v,&value);
                            std::cout << ", " << str0.c_str() << ": " << value+0;
                            sensor[nodeInfo->m_nodeId].WakeupInterval = value+0;   
                        }
                    }
                    std::cout << "\n";
 
                    /**
                      if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                 ValueID id = _notification->GetValueID();
                string str;
                int32_t value_int=0;
                Manager::Get()->GetValueAsString(id, &str);
                printf("Notification Value Changed: Node %d Genre %d Class %d Instance %d Index %d Type %d, value %s \n",
                        _notification->GetNodeId(), id.GetGenre(), id.GetCommandClassId(),
                        id.GetInstance(), id.GetIndex(), id.GetType(), str.c_str());
                     */ 
                /**
                if (Manager::Get()->GetValueAsString(id, &str)) {
                    int level = 0;
                    printf("Values: %s\n",str.c_str());
                   // nodeInfo->m_level = level;
                }
                 
             } 
                  */   
                    /**
			if( nodeInfo  )
			{
                            //std::cout << "Node: " << nodeInfo->m_nodeId << std::endl;
				nodeInfo = nodeInfo;		// placeholder for real action
                                
			}
                     */ 
			break;
		}
                

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo = nodeInfo;		// placeholder for real action
			}
			break;
		}                
                
		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;		
			g_nodes.push_back( nodeInfo );
		        if (temp == true) {
			    Manager::Get()->CancelControllerCommand( _notification->GetHomeId() );
                        }
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32_t const homeId = _notification->GetHomeId();
			uint8_t const nodeId = _notification->GetNodeId();
			for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
			{
				NodeInfo* nodeInfo = *it;
				if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
				{
					g_nodes.erase( it );
					delete nodeInfo;
					break;
				}
			}
			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
                        NodeInfo* nodeInfo = GetNodeInfo( _notification );

                        std::cout << "(vent) Node ID: " << (uint8_t)nodeInfo->m_nodeId+0;
                        sensor[nodeInfo->m_nodeId].LastTimeStamp = time(0);
                        for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it)
                        {
                            ValueID v = *it;
                            string str0 = Manager::Get()->GetValueLabel(v);
                            string str1;
                            Manager::Get()->GetValueAsString(v,&str1);
                            cout << " " << str0.c_str() <<  "(0x" << std::hex << std::uppercase << v.GetCommandClassId()+0 << "): " << str1.c_str();
                        }
                        cout << "\n";
                }

		case Notification::Type_PollingDisabled:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo->m_polled = false;
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo->m_polled = true;
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		case Notification::Type_AllNodesQueriedSomeDead:
		{
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_DriverReset:
		case Notification::Type_Notification:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeProtocolInfo:
		case Notification::Type_NodeQueriesComplete:
		default:
		{
		}
	}

	pthread_mutex_unlock( &g_criticalSection );
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------
int main( int argc, char* argv[] )
{
    struct mg_mgr mgr;
    struct mg_connection *nc;
    struct mg_bind_opts bind_opts;
    int i;
    char *cp;
    const char *err_str;
    const char *ssl_cert = NULL;

    mg_mgr_init(&mgr, NULL);

    /* Use current binary directory as document root */
    if (argc > 0 && ((cp = strrchr(argv[0], DIRSEP)) != NULL)) {
        *cp = '\0';
        s_http_server_opts.document_root = argv[0];
        //s_http_server_opts.document_root = ".";
    }

    /* Set HTTP server options */
    memset(&bind_opts, 0, sizeof(bind_opts));
    bind_opts.error_string = &err_str;
    nc = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts);
    if (nc == NULL) {
        fprintf(stderr, "Error starting server on port %s: %s\n", s_http_port, *bind_opts.error_string);
    exit(1);
    }

    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.enable_directory_listing = "yes";

    printf("Starting RESTful server on port %s, serving %s\n", s_http_port,
         s_http_server_opts.document_root);

  // openzwave
	pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init ( &mutexattr );
    pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &g_criticalSection, &mutexattr );
    pthread_mutexattr_destroy( &mutexattr );

    pthread_mutex_lock( &initMutex );

    printf("Starting application with OpenZWave Version %s\n", Manager::getVersionAsString().c_str());

    // Create the OpenZWave Manager.
    // The first argument is the path to the config files (where the manufacturer_specific.xml file is located
    // The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
    // the log file will appear in the program's working directory.
    Options::Create( "/etc/openzwave/", "", "" );
#if 0        
    Options::Get()->AddOptionInt("SaveLogLevel", LogLevel_Debug);
    Options::Get()->AddOptionInt("QueueLogLevel", LogLevel_Debug);
#else
    Options::Get()->AddOptionInt("SaveLogLevel", LogLevel_Error);
    Options::Get()->AddOptionInt("QueueLogLevel", LogLevel_Error);
#endif        
    Options::Get()->AddOptionInt( "DumpTrigger", LogLevel_Error );
    Options::Get()->AddOptionInt( "PollInterval", 500 );
    Options::Get()->AddOptionBool( "IntervalBetweenPolls", true );
    Options::Get()->AddOptionBool("ValidateValueChanges", true);
    Options::Get()->Lock();

    Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher( OnNotification, NULL );

    // Add a Z-Wave Driver
    // Modify this line to set the correct serial port for your PC interface.
    string port = "/dev/ttyACM0";
    Manager::Get()->AddDriver((argc > 1) ? argv[1] : port);

    // Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
    // then write out the config file.
    // In a normal app, we would be handling notifications and building a UI for the user.
    pthread_cond_wait( &initCond, &initMutex );

    // Since the configuration file contains command class information that is only 
    // known after the nodes on the network are queried, wait until all of the nodes 
    // on the network have been queried (at least the "listening" ones) before
    // writing the configuration file.  (Maybe write again after sleeping nodes have
    // been queried as well.)
    if( !g_initFailed )
    {
            // The section below demonstrates setting up polling for a variable.  In this simple
            // example, it has been hardwired to poll COMMAND_CLASS_BASIC on the each node that 
            // supports this setting.
            pthread_mutex_lock( &g_criticalSection );
            for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
            {
                    NodeInfo* nodeInfo = *it;

                    // skip the controller (most likely node 1)
                    if( nodeInfo->m_nodeId == 1) continue;

                    for( list<ValueID>::iterator it2 = nodeInfo->m_values.begin(); it2 != nodeInfo->m_values.end(); ++it2 )
                    {
                            ValueID v = *it2;
                            if( v.GetCommandClassId() == 0x20 )
                            {
//					Manager::Get()->EnablePoll( v, 2 );		// enables polling with "intensity" of 2, though this is irrelevant with only one value polled
                                    break;
                            }
                    }
            }
            pthread_mutex_unlock( &g_criticalSection );



            while(true) {
                // Press ENTER to gracefully exit.
                //char charValue = cin.get();
                char charValue = 0;
                if ( charValue == 'q') {
                    Manager::Get()->WriteConfig( g_homeId );
                    break;
                  }
                if ( charValue == 'e' ){
                    AlarmEnabled = 1;
                    cout << "Alarm Enabled" << endl;
                }
                if ( charValue == 'd'){
                    AlarmEnabled = 0;
                    cout << "Alarm Disabled" << endl;
                }
                if (prev_switch_value != switch_value){
                    cout << "send command to switch" << endl;
                    pthread_mutex_lock(&g_criticalSection);
                    SwitchValue(Switch1,switch_value);
                    pthread_mutex_unlock(&g_criticalSection);
                    prev_switch_value = switch_value;
                }
                if ( print_info){
                    print_info = 0;
                    pthread_mutex_lock(&g_criticalSection);
                    for (int n=0 ; n<0 ; ++n ){
                        cout << "*** ID: " << std::dec << sensor[node_list[n]].getID()+0 << endl;
                        cout << "* Detect: " << std::dec << sensor[node_list[n]].detect+0 << endl;
                        cout << "* Tamper: " << std::dec << sensor[node_list[n]].tamper+0 << endl;
                        cout << "* Battery Level: " << std::dec << sensor[node_list[n]].BatteryLevel+0 << " %" << endl;
                        cout << "* Wake-up interval: " << std::dec << sensor[node_list[n]].WakeupInterval+0 << " seconds." << endl;
                        cout << "* Last time: " <<  std::dec << (uint32_t)(time(0) - sensor[node_list[n]].LastTimeStamp)+0<< " seconds ago." << endl;
                        cout << "****" << endl;
                    }
                    pthread_mutex_unlock(&g_criticalSection);
                }
                
                pthread_mutex_lock(&g_criticalSection);
                if(sensor[WindowKitchen].activity()){
                    cout << "Movement sensor activated!" << endl;
                    time_t t = time(0);   // get time now
                    struct tm * now = localtime( & t );
                    cout << (now->tm_hour + 1900) << ':' << (now->tm_min << ':'<<  now->tm_sec << endl;
                    notification.send("Security","Movement sensor activity" );
                }
                pthread_mutex_unlock(&g_criticalSection);

                if(SensorTriggered && AlarmEnabled){
                    cout << "Intruder!" << endl;
                    SensorTriggered = 0;
                }
                if(SensorTriggered && ~AlarmEnabled){
                    cout << "Notification" << endl;
                    SensorTriggered = 0;
                }
                //pthread_mutex_unlock( &g_criticalSection );
                
                mg_mgr_poll(&mgr, 1000);
            }

            mg_mgr_free(&mgr);
            
            Driver::DriverData data;
            Manager::Get()->GetDriverStatistics( g_homeId, &data );
            printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.m_SOFCnt, data.m_ACKWaiting, data.m_readAborts, data.m_badChecksum);
            printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.m_readCnt, data.m_writeCnt, data.m_CANCnt, data.m_NAKCnt, data.m_ACKCnt, data.m_OOFCnt);
            printf("Dropped: %d Retries: %d\n", data.m_dropped, data.m_retries);
    }
    // program exit (clean up)
    if( strcasecmp( port.c_str(), "usb" ) == 0 )
    {
            Manager::Get()->RemoveDriver( "HID Controller" );
    }
    else
    {
            Manager::Get()->RemoveDriver( port );
    }
    Manager::Get()->RemoveWatcher( OnNotification, NULL );
    Manager::Destroy();
    Options::Destroy();
    pthread_mutex_destroy( &g_criticalSection );
    return 0;
}
