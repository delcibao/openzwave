//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//-----------------------------------------------------------------------------

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <iostream>
#include <list>
#include <string>
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

#include "DoorWindowSensor.h"

#define Event_Detected  0x06
#define Tamper_Switch   0x07
#define Open            0xFF
#define Close           0x00
#define Basic_Value     "Basic"
#define Battery_Level   "Battery Level"
#define Alarm_Type      "Alarm Type"
#define Alarm_Level     "Alarm Level"
        
//#define DEBUG 1

using namespace OpenZWave;

bool temp = false;

bool AlarmEnabled = 0;
bool SensorTriggered = 0;

static uint32_t g_homeId = 0;
static bool g_initFailed = false;

typedef struct
{
	uint32_t	m_homeId;
	uint8_t 	m_nodeId;
	bool		m_polled;
	list<ValueID>	m_values;
}NodeInfo;
/**
 * Basic(0): 255,
 * Sensor(0): False, 
 *  ZWave+ Version(0): 1, 
 *  InstallerIcon(1): 3079,  
 * UserIcon(2): 3079,  
 * External Switch(1): Off,  
 * Alarm Type(0): 6,  
 * Alarm Level(1): 255,  SourceNodeId(2): 0,  Access Control(9): 22,  Burglar(10): 0,  Power Management(11): 0,  System(12): 0,  Emergency(13): 0,  Clock(14): 0,  Powerlevel(0): Normal,  Timeout(1): 0,  Set Powerlevel(2): False,  Test Node(3): 0,  Test Powerlevel(4): Normal,  Frame Count(5): 0,  Test(6): False,  Report(7): False,  Test Status(8): Failed,  Acked Frames(9): 0,  Battery Level(0): 100,  Minimum Wake-up Interval(1): 600,  Maximum Wake-up Interval(2): 604800,  Default Wake-up Interval(3): 3600,  Wake-up Interval Step(4): 200,  Wake-up Interval(0): 3600,  Library Version(0): 3,  Protocol Version(1): 4.05,  Application Version(2): 5.01,

*/

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

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
                      
                    std::cout << "Home ID: " << (uint32_t)nodeInfo->m_homeId+0 << ", Node ID: " << (uint8_t)nodeInfo->m_nodeId+0;
/*
                    for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it)
                    {
                        ValueID v = *it;
                        string str0 = Manager::Get()->GetValueLabel(v);
                        string str1;
                        Manager::Get()->GetValueAsString(v,&str1);
                        cout << " " << str0.c_str() << ": " << str1.c_str();
                    }
                    cout << "\n";
*/
/**
                    ValueID v = nodeInfo->m_values.front();
                    string str0 = Manager::Get()->GetValueLabel(v);
                    string str1;
                    Manager::Get()->GetValueAsString(v,&str1);
                    int value = std::stoi(str1);
                    cout << str0.c_str() << ": " << value <<"\n";
*/

                    for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it)
                    {
                        string str0, str1, str2;
                        ValueID v = *it;
                        str0 = Manager::Get()->GetValueLabel(v);
                        if(str0 == Basic_Value){                                    // Open: 0xFF, Close: 0x00
                            Manager::Get()->GetValueAsString(v,&str1);
                            int value = std::stoi (str1);
                            std::cout << ", " << str0.c_str() << ": " << value;
                        }
                        if(str0 == Battery_Level){                            // In percentage
                            Manager::Get()->GetValueAsString(v,&str1);
                            int value = std::stoi (str1);
                            std::cout << ", " << str0.c_str() << ": " << value;
                        }
                        if(str0 == Alarm_Type){                               // Event Detected: 0x06, Tamper Switch: 0x07
                            Manager::Get()->GetValueAsString(v,&str1);
                            int value = std::stoi (str1);
                            if (value == Event_Detected)
                                std::cout << ", Event Detected: ";
                            if (value == Tamper_Switch)
                                std::cout << ", Tamper Switch: ";
                            //std::cout << ", " << str0.c_str() << ": " << str1.c_str();
                        }
                        if(str0 == Alarm_Level){                              // Open: 0xFF, Close: 0x00
                            Manager::Get()->GetValueAsString(v,&str1);
                            int value = std::stoi (str1);
                            if (value == Open)
                                std::cout << "Open";
                            if (value == Close)
                                std::cout << "Close";
                                
                            //std::cout << ", " << str0.c_str() << ": " << value;
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
                    /**
                    NodeInfo* nodeInfo = GetNodeInfo( _notification );                      
                    cout << "Home ID: " << (uint32_t)nodeInfo->m_homeId+0 << ", Node ID: " << (uint8_t)nodeInfo->m_nodeId+0 << " |";
                    for ( list<ValueID>::iterator it=nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it)
                    {
                        ValueID v = *it;
                        string str0 = Manager::Get()->GetValueLabel(v);
                        string str1;
                        Manager::Get()->GetValueAsString(v,&str1);
                        cout << " " << str0.c_str() << ": " << str1.c_str() <<"\n";
                    }
                    */
                    
                    NodeInfo* nodeInfo = GetNodeInfo( _notification );
                    if( nodeInfo )
                    {
                        nodeInfo = nodeInfo;		// placeholder for real action
                    }
                    break;
                    
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
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init ( &mutexattr );
	pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
	pthread_mutex_init( &g_criticalSection, &mutexattr );
	pthread_mutexattr_destroy( &mutexattr );

	pthread_mutex_lock( &initMutex );


	printf("Starting MinOZW with OpenZWave Version %s\n", Manager::getVersionAsString().c_str());

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
	// the log file will appear in the program's working directory.
	Options::Create( "/etc/openzwave/", "", "" );
#ifdef DEBUG        
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
/*
#ifdef DARWIN
	string port = "/dev/cu.usbserial";
#elif WIN32
        string port = "\\\\.\\COM6";
#else
	string port = "/dev/ttyUSB0";
#endif
*/
string port = "/dev/ttyACM0";

    if ( argc > 1 )
	{
		port = argv[1];
	}
	if( strcasecmp( port.c_str(), "usb" ) == 0 )
	{
		Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );
	}
	else
	{
		Manager::Get()->AddDriver( port );
	}

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

		// If we want to access our NodeInfo list, that has been built from all the
		// notification callbacks we received from the library, we have to do so
		// from inside a Critical Section.  This is because the callbacks occur on other 
		// threads, and we cannot risk the list being changed while we are using it.  
		// We must hold the critical section for as short a time as possible, to avoid
		// stalling the OpenZWave drivers.
		// At this point, the program just waits for 3 minutes (to demonstrate polling),
		// then exits
/**
		for( int i = 0; i < 60*3; i++ )
		{
			pthread_mutex_lock( &g_criticalSection );
			// but NodeInfo list and similar data should be inside critical section
			pthread_mutex_unlock( &g_criticalSection );
			sleep(1);
		}
*/
                while(true) {
                    // Press ENTER to gracefully exit.
                    char charValue = cin.get();
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
                    pthread_mutex_lock( &g_criticalSection );
                    if(SensorTriggered && AlarmEnabled){
                        cout << "Intruder!" << endl;
                        SensorTriggered = 0;
                    }
                    if(SensorTriggered && ~AlarmEnabled){
                        cout << "Notification" << endl;
                        SensorTriggered = 0;
                    }
                    pthread_mutex_unlock( &g_criticalSection );
                }
                
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
