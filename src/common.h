#ifndef COMMON_H
#define COMMON_H

#include "ns3/core-module.h"
#include <vector>
#include <map>
#include <cmath>

using namespace ns3;

#define SIMULATION_TIME 5000.0

#define DT_THRESHOLD 0.5
#define PENALTY_FACTOR 1.5
#define DECAY_FACTOR 0.05
#define SLIDING_WINDOW 100
#define THETA 0.1

#define FCM_NUM_CLUSTERS 2  // trustworthy (cluster 0), untrustworthy (cluster 1)
#define FCM_FUZZINESS 2
#define FCM_EPSILON 1e-6
#define FCM_MAX_ITER 50
#define TRUSTWORTHY_THRESHOLD 0.5

#define INITIAL_SERVICE_REQUEST_OFFSET 5.0
#define INITIAL_SERVICE_REQUEST_INTERVAL 0.05
#define RSU_REGISTRATION_OFFSET 0.5
#define RSU_REGISTRATION_INTERVAL 0.02

#define SERVICE_INTERVAL 4.0
#define TRUST_EVAL_INTERVAL 100.0
#define DEFAULT_ON_PERIOD 30.0
#define DEFAULT_OFF_PERIOD 70.0

#define SERVICE_PORT 1000
#define REPORT_PORT 1001
#define REGISTRATION_PORT 1002
#define ATTACK_STATUS_PORT 1003

#define DEBUG_LOGGING 1

enum MessageType 
{
    MSG_SERVICE_REQUEST = 0,
    MSG_SERVICE_RESPONSE = 1,
    MSG_VEHICLE_REGISTRATION = 2,
    MSG_TRUST_INTERACTION_REPORT = 3,
    MSG_UPDATE_ATTACK_STATUS = 4
};

enum NodeType 
{
    ORDINARY = 0,
    SERVICE_PROVIDER = 1,
    PUBLIC = 2,
    AUTHORITY = 3
};

enum Feedback 
{
    NEGATIVE = 0,
    POSITIVE = 1
};

struct Message 
{
    MessageType type;
    uint32_t sourceId;
    uint32_t targetId;
    double value;  // Can hold trust values or feedback
    Time timestamp;
    bool isTampered;
};

struct Interaction 
{
    Time timestamp;
    Feedback feedback;
};

enum AttackType 
{
    NO_ATTACK = 0,
    BAD_MOUTHING = 1,
    BALLOT_STUFFING = 2,
    ON_OFF = 3
};

#endif