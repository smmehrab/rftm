#include "logging.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("LoggingUtility");

namespace ns3 {
    void EnableTimeLogging()
    {
        Time::SetResolution(Time::NS);
        
        LogComponentEnableAll(LOG_PREFIX_TIME);
        LogComponentEnableAll(LOG_PREFIX_NODE);
        LogComponentEnableAll(LOG_PREFIX_FUNC);
        
        LogComponentEnable("RFTM", LOG_LEVEL_INFO);
        LogComponentEnable("Vehicle", LOG_LEVEL_INFO);
        LogComponentEnable("RSU", LOG_LEVEL_INFO);
        LogComponentEnable("LoggingUtility", LOG_LEVEL_INFO);
        
        // NS_LOG_INFO("Time-stamped logging enabled");
    }

    void EnableDetailedLogging()
    {
        Time::SetResolution(Time::NS);
        
        LogComponentEnableAll(LOG_PREFIX_ALL);
        
        LogComponentEnable("RFTM", LOG_LEVEL_DEBUG);
        LogComponentEnable("Vehicle", LOG_LEVEL_DEBUG);
        LogComponentEnable("RSU", LOG_LEVEL_DEBUG);
        LogComponentEnable("LoggingUtility", LOG_LEVEL_DEBUG);
        
        // NS_LOG_INFO("Detailed logging enabled");
    }
}