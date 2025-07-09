#include "vehicle.h"
#include "rsu.h"
#include <algorithm>

NS_LOG_COMPONENT_DEFINE("Vehicle");

Vehicle::Vehicle()
    : m_nodeType(ORDINARY),
      m_interactionCount(0),
      m_isInOnOffMode(false),
      m_isOn(false),
      m_onTime(DEFAULT_ON_PERIOD),
      m_offTime(DEFAULT_OFF_PERIOD),
      m_isServiceProvider(false),
      m_attackType(NO_ATTACK),
      m_serviceQuality(POSITIVE_SERVICE),
      m_isBad(false),
      m_serviceInterval(SERVICE_INTERVAL),
      m_endTime(SIMULATION_TIME),
      m_startWithOn(true),
      m_serviceProbability(0.95)
{
    m_uniformVar = CreateObject<UniformRandomVariable>();
}

void Vehicle::SetServiceProbability(double probability) 
{
    m_serviceProbability = probability;
}

void Vehicle::SetServiceInterval(double interval) 
{
    m_serviceInterval = interval;
}

void Vehicle::SetVspNonVspInterval(double interval) 
{
    m_vspNonVspInterval = interval;
}

void Vehicle::SetStartWithOn(bool startWithOn) 
{
    m_startWithOn = startWithOn;
}

void Vehicle::SetIsInOnOffMode(bool isInOnOffMode) 
{
    m_isInOnOffMode = isInOnOffMode;
    if (m_isInOnOffMode)
    {
        m_isOn = m_startWithOn;
        if (m_isOn) 
        {
            ToggleServiceQuality();
        }
        if (m_isOn) 
        {
            Simulator::Schedule(Seconds(20.0 + m_onTime), &Vehicle::ToggleOnOff, this);
        }
        else 
        {
            Simulator::Schedule(Seconds(20.0 + m_offTime), &Vehicle::ToggleOnOff, this);
        }
        // NS_LOG_INFO("V" << GetNode()->GetId() << " isOn = " << m_isOn);
    }
}

void Vehicle::SetIsOn(bool isOn) 
{
    m_isOn = isOn;
}

double Vehicle::SetOnTime(double onTime) 
{
    m_onTime = onTime;
    return m_onTime;
}

double Vehicle::SetOffTime(double offTime) 
{
    m_offTime = offTime;
    return m_offTime;
}

void Vehicle::SetAttackType(AttackType type)
{
    AttackType oldType = m_attackType;
    m_attackType = type;
    // NS_LOG_INFO("V" << GetNode()->GetId() << " attack type set to " << (int)type);
    if (oldType != type) 
    {
        UpdateAttackStatusWithRsu();
    }
}

AttackType Vehicle::GetAttackType() const
{
    return m_attackType;
}

void Vehicle::SetServiceQuality(ServiceQuality quality)
{
    m_serviceQuality = quality;
    NS_LOG_INFO("V" << GetNode()->GetId() << " service quality set to " << (int)quality);
}

void Vehicle::ToggleServiceQuality()
{
    m_serviceQuality = (m_serviceQuality == NEGATIVE_SERVICE) ? POSITIVE_SERVICE : NEGATIVE_SERVICE ;
    NS_LOG_INFO("V" << GetNode()->GetId() << " service quality set to " << (int)m_serviceQuality);
}

ServiceQuality Vehicle::GetServiceQuality() const
{
    return m_serviceQuality;
}

TypeId Vehicle::GetTypeId()
{
    static TypeId tid = TypeId("Vehicle").SetParent<Application>().AddConstructor<Vehicle>();
    return tid;
}

void Vehicle::DoInitialize()
{
    Application::DoInitialize();
}

void Vehicle::StartApplication()
{
    // NS_LOG_INFO("V" << GetNode()->GetId() << " Started" << (m_isServiceProvider ? " (Service Provider)" : ""));
    m_serviceSocket = SetupListeningSocket(SERVICE_PORT);

    // Find RSU
    for (uint32_t i = 0; i < NodeList::GetNNodes(); i++)
    {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<RSU> rsuApp = DynamicCast<RSU>(node->GetApplication(0));
        if (rsuApp)
        {
            m_rsuNode = node;
            Ptr<Ipv4> ipv4 = m_rsuNode->GetObject<Ipv4>();
            m_rsuAddress = ipv4->GetAddress(1, 0).GetLocal();
            // NS_LOG_INFO("V" << GetNode()->GetId() << " found RSU" << m_rsuNode->GetId());
            break;
        }
    }

    // Find other vehicles and service providers
    m_serviceProviderNodes.clear();
    m_otherVehicleNodes.clear();
    uint32_t nServiceProviders = 0;
    uint32_t nOtherVehicles = 0;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); i++)
    {
        Ptr<Node> node = NodeList::GetNode(i);
        if (node->GetId() != GetNode()->GetId())
        {
            Ptr<Vehicle> vehicleApp = DynamicCast<Vehicle>(node->GetApplication(0));
            if (vehicleApp && vehicleApp->IsServiceProvider())
            {
                m_serviceProviderNodes.push_back(node);
                nServiceProviders++;
            }
            else if (vehicleApp && !vehicleApp->IsServiceProvider()) 
            {
                m_otherVehicleNodes.push_back(node);
                nOtherVehicles++;
            }
        }
    }
    // NS_LOG_INFO("V" << GetNode()->GetId() << " found " << nServiceProviders << " VSPs and " << nOtherVehicles << " vehicles");

    if (m_rsuNode)
    {
        double regDelay = RSU_REGISTRATION_OFFSET + (GetNode()->GetId() * RSU_REGISTRATION_INTERVAL); // Stagger registrations
        Simulator::Schedule(Seconds(regDelay), &Vehicle::RegisterWithRsu, this);
    }

    if (!m_isServiceProvider && !m_serviceProviderNodes.empty() && m_rsuNode)
    {
        double initialDelay = INITIAL_SERVICE_REQUEST_OFFSET + (GetNode()->GetId() * INITIAL_SERVICE_REQUEST_INTERVAL);
        // NS_LOG_INFO("V" << GetNode()->GetId() << " scheduling first service request after "<< initialDelay << " seconds, with interval " << m_serviceInterval);
        Simulator::Schedule(Seconds(initialDelay), &Vehicle::PerformServiceRequest, this, true);
        Simulator::Schedule(Seconds(initialDelay + m_vspNonVspInterval), &Vehicle::PerformServiceRequest, this, false);
    }
    else if (m_isServiceProvider)
    {
        // NS_LOG_INFO("V" << GetNode()->GetId() << " is a service provider, not scheduling service requests");
    }
    else if (m_serviceProviderNodes.empty())
    {
        // NS_LOG_INFO("V" << GetNode()->GetId() << " found no service providers");
    }
    else if (!m_rsuNode)
    {
        // NS_LOG_INFO("V" << GetNode()->GetId() << " could not find RSU");
    }
}

void Vehicle::RegisterWithRsu()
{
    if (!m_rsuNode)
    {
        // NS_LOG_ERROR("V" << GetNode()->GetId() << " cannot register - no RSU found");
        return;
    }

    Message msg;
    msg.type = MSG_VEHICLE_REGISTRATION;
    msg.sourceId = GetNode()->GetId();
    msg.targetId = m_rsuNode->GetId();
    msg.timestamp = Simulator::Now();

    double encodedValue = 0.0;
    // First bit: Is service provider?
    if (IsServiceProvider()) 
    {
        encodedValue += 1.0;
    }
    // Second bit: Is bad?
    if (IsBad() || m_attackType != NO_ATTACK) 
    {
        encodedValue += 0.1;
    }
    msg.value = encodedValue;
    
    Ptr<Socket> socket = SetupSendingSocket(m_rsuAddress, REGISTRATION_PORT);
    Ptr<Packet> packet = Create<Packet>((uint8_t *)&msg, sizeof(Message));
    socket->Send(packet);
    socket->Close();    

    // NS_LOG_INFO("V" << GetNode()->GetId() << " sent registration to RSU" << m_rsuNode->GetId());
}

void Vehicle::UpdateAttackStatusWithRsu()
{
    if (!m_rsuNode)
    {
        NS_LOG_ERROR("V" << GetNode()->GetId() << " cannot update attack status - no RSU found");
        return;
    }

    if (m_rsuAddress == Ipv4Address("0.0.0.0")) 
    {
        Ptr<Ipv4> ipv4 = m_rsuNode->GetObject<Ipv4>();
        m_rsuAddress = ipv4->GetAddress(1, 0).GetLocal();
        NS_LOG_DEBUG("V" << GetNode()->GetId() << " updated RSU address to " << m_rsuAddress);
    }

    Message msg;
    msg.type = MSG_UPDATE_ATTACK_STATUS;
    msg.sourceId = GetNode()->GetId();
    msg.targetId = m_rsuNode->GetId();
    msg.timestamp = Simulator::Now();
    msg.value = static_cast<double>(m_attackType);
    
    Ptr<Socket> socket = SetupSendingSocket(m_rsuAddress, ATTACK_STATUS_PORT);
    Ptr<Packet> packet = Create<Packet>((uint8_t *)&msg, sizeof(Message));
    
    int result = socket->Send(packet);
    socket->Close();
    
    // if (result >= 0) 
    // {
    //     NS_LOG_INFO("V" << GetNode()->GetId() << " ➤ RSU attack status update SENT" << " (type: " << (int)m_attackType  << ", port: " << ATTACK_STATUS_PORT << ", handler: HandleAttackStatusUpdate)");
    // }
    // else 
    // {
    //     NS_LOG_ERROR("V" << GetNode()->GetId() << " ✗ FAILED to send attack status update (result: " << result << ")");
    // }
}

void Vehicle::StopApplication()
{
    if (m_serviceSocket)
    {
        m_serviceSocket->Close();
        m_serviceSocket = 0;
    }

    for (auto &socketPair : m_socketMap)
    {
        if (socketPair.second)
        {
            socketPair.second->Close();
        }
    }
    m_socketMap.clear();
    // NS_LOG_INFO("V" << GetNode()->GetId() << " Stopped");
}

Ptr<Socket> Vehicle::SetupListeningSocket(uint16_t port)
{
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), tid);

    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    socket->Bind(local);
    socket->Listen();

    socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address &>(), MakeCallback(&Vehicle::HandleAccept, this));
    socket->SetRecvCallback(MakeCallback(&Vehicle::HandleRead, this));
    return socket;
}

Ptr<Socket> Vehicle::SetupSendingSocket(Ipv4Address destination, uint16_t port)
{
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), tid);
    socket->Connect(InetSocketAddress(destination, port));
    return socket;
}

void Vehicle::HandleAccept(Ptr<Socket> socket, const Address &from)
{
    socket->SetRecvCallback(MakeCallback(&Vehicle::HandleRead, this));
    m_socketMap[from] = socket;
    // NS_LOG_INFO("V" << GetNode()->GetId() << " accepted connection from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
}

void Vehicle::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }

        Message msg;
        packet->CopyData((uint8_t *)&msg, sizeof(Message));

        switch (msg.type)
        {
        case MSG_SERVICE_REQUEST:
        {
            // NS_LOG_INFO("V" << GetNode()->GetId() << " received service request from " << msg.sourceId);
            Feedback feedback = ProvideService(msg.sourceId);
            Ptr<Socket> respSocket = SetupSendingSocket(InetSocketAddress::ConvertFrom(from).GetIpv4(), SERVICE_PORT);
            SendServiceResponse(respSocket, msg.sourceId, feedback);
            break;
        }

        case MSG_SERVICE_RESPONSE:
        {
            Feedback receivedFeedback = (msg.value > 0.5) ? POSITIVE : NEGATIVE;
            // NS_LOG_INFO("V" << GetNode()->GetId() << " received service response from V" << msg.sourceId << " with value " << msg.value << " (" << (receivedFeedback == POSITIVE ? "POSITIVE" : "NEGATIVE") << ")");
            Feedback reportFeedback = ChangeFeedback(receivedFeedback);
            RecordInteraction(msg.sourceId, reportFeedback, m_rsuAddress);
            break;
        }

        default:
            // NS_LOG_INFO("V" << GetNode()->GetId() << " received unknown message type: " << msg.type);
            break;
        }
    }
}

Feedback Vehicle::ChangeFeedback(Feedback feedback)
{
    Feedback changedFeedback = feedback;

    if (IsBad()) 
    {
        // Invert feedback if the vehicle is bad
        changedFeedback = (feedback == POSITIVE) ? NEGATIVE : POSITIVE;
        // NS_LOG_INFO("Invertion applied: feedback set to " << (changedFeedback == POSITIVE ? "POSITIVE" : "NEGATIVE"));
        return changedFeedback;
    }

    switch (m_attackType)
    {
    case BAD_MOUTHING:
        changedFeedback = NEGATIVE;
        // NS_LOG_INFO("Bad Mouthing applied: feedback set to NEGATIVE");
        break;

    case BALLOT_STUFFING:
        changedFeedback = POSITIVE;
        // NS_LOG_INFO("Ballot Stuffing applied: feedback set to POSITIVE");
        break;

    default:
        break;
    }

    return changedFeedback;
}

void Vehicle::SendServiceRequest(Ptr<Socket> socket, uint32_t targetId)
{
    Message msg;
    msg.type = MSG_SERVICE_REQUEST;
    msg.sourceId = GetNode()->GetId();
    msg.targetId = targetId;
    msg.value = 0.0; // Not used for request
    msg.timestamp = Simulator::Now();

    Ptr<Packet> packet = Create<Packet>((uint8_t *)&msg, sizeof(Message));
    socket->Send(packet);
    socket->Close();
    // NS_LOG_INFO("V" << GetNode()->GetId() << " sent service request to V" << targetId);
}

void Vehicle::SendServiceResponse(Ptr<Socket> socket, uint32_t targetId, Feedback feedback)
{
    Message msg;
    msg.type = MSG_SERVICE_RESPONSE;
    msg.sourceId = GetNode()->GetId();
    msg.targetId = targetId;
    msg.value = (feedback == POSITIVE) ? 1.0 : 0.0;
    msg.timestamp = Simulator::Now();

    Ptr<Packet> packet = Create<Packet>((uint8_t *)&msg, sizeof(Message));
    socket->Send(packet);
    socket->Close();
    // NS_LOG_INFO("V" << GetNode()->GetId() << " sent service response to " << targetId<< " with " << (feedback == POSITIVE ? "positive" : "negative") << " feedback");
}

void Vehicle::SetNodeType(NodeType type)
{
    m_nodeType = type;
    m_isServiceProvider = (type == SERVICE_PROVIDER);
}

NodeType Vehicle::GetNodeType() const
{
    return m_nodeType;
}

bool Vehicle::IsServiceProvider() const
{
    return m_isServiceProvider;
}

void Vehicle::SetBad(bool bad)
{
    m_isBad = bad;
}

bool Vehicle::IsBad() const
{
    return m_isBad;
}

void Vehicle::RecordInteraction(uint32_t otherId, Feedback feedback, Ipv4Address rsuAddress)
{
    Interaction interaction;
    interaction.timestamp = Simulator::Now();
    interaction.feedback = feedback;

    auto &history = m_interactionHistory[otherId];
    history.push_back(interaction);
    m_interactionCount++;

    // Enforce sliding window size
    // if (history.size() > SLIDING_WINDOW)
    // {
    //     history.erase(history.begin());
    // }

    // NS_LOG_INFO("V" << GetNode()->GetId()<< " recorded " << (feedback == POSITIVE ? "positive" : "negative")<< " feedback about V" << otherId);
    ReportInteractionToRsu(otherId, interaction, rsuAddress);
}

void Vehicle::ReportInteractionToRsu(uint32_t otherId, Interaction interaction, Ipv4Address rsuAddress)
{
    Ptr<Socket> socket = SetupSendingSocket(rsuAddress, REPORT_PORT);

    Message msg;
    msg.type = MSG_TRUST_INTERACTION_REPORT;
    msg.sourceId = GetNode()->GetId();
    msg.targetId = otherId;
    msg.value = (interaction.feedback == POSITIVE) ? 1.0 : 0.0;
    msg.timestamp = interaction.timestamp;

    Ptr<Packet> packet = Create<Packet>((uint8_t *)&msg, sizeof(Message));
    socket->Send(packet);
    socket->Close();    
    // NS_LOG_INFO("V" << GetNode()->GetId() << " reported interaction to RSU about V" << otherId<< " with " << (interaction.feedback == POSITIVE ? "positive" : "negative") << " feedback");
}

void Vehicle::RequestService(uint32_t serviceProviderId, Ipv4Address targetAddress, Ipv4Address rsuAddress)
{
    m_rsuAddress = rsuAddress;
    Ptr<Socket> socket = SetupSendingSocket(targetAddress, SERVICE_PORT);
    SendServiceRequest(socket, serviceProviderId);
}

Feedback Vehicle::ProvideService(uint32_t requesterId)
{
    Feedback feedback;
    if (m_serviceQuality == NEGATIVE_SERVICE || IsBad()) 
    {
        double positiveProb = 1.0 - m_serviceProbability;
        feedback = (m_uniformVar->GetValue(0, 1) < positiveProb) ? POSITIVE : NEGATIVE;
        // NS_LOG_INFO("V" << GetNode()->GetId() << " provided service to V" << requesterId<< " with " << (feedback == POSITIVE ? "positive" : "negative") << " feedback");
    }
    else 
    {
        feedback = (m_uniformVar->GetValue(0, 1) < m_serviceProbability) ? POSITIVE : NEGATIVE;
        // NS_LOG_INFO("V" << GetNode()->GetId() << " provided service to V" << requesterId<< " with " << (feedback == POSITIVE ? "positive" : "negative") << " feedback");
    }
    return feedback;
}

void Vehicle::ToggleOnOff()
{
    if (!m_isInOnOffMode)
    {
        // NS_LOG_INFO("V" << GetNode()->GetId() << " is not in OnOff mode, skipping toggle");
        return;
    }

    if (m_isOn)
    {
        m_isOn = false;
        ToggleServiceQuality();
        Simulator::Schedule(Seconds(m_offTime), &Vehicle::ToggleOnOff, this);
    }
    else
    {
        m_isOn = true;
        ToggleServiceQuality();
        Simulator::Schedule(Seconds(m_onTime), &Vehicle::ToggleOnOff, this);
    }
    // NS_LOG_INFO("V" << GetNode()->GetId() << " isOn = " << m_isOn);
}

bool Vehicle::IsInOnOffMode() const
{
    return m_isInOnOffMode;
}

void Vehicle::PerformServiceRequest(bool toVSP)
{
    if (Simulator::Now().GetSeconds() >= m_endTime)
    {
        return;
    }

    if (m_serviceProviderNodes.empty())
    {
        // NS_LOG_INFO("V" << GetNode()->GetId() << " has no service providers to choose from");
        Simulator::Schedule(Seconds(m_serviceInterval), &Vehicle::PerformServiceRequest, this, toVSP);
        return;
    }

    Ptr<Node> destinationNode;
    if (toVSP) 
    {
        // Randomly select a VSP
        int vspIndex = m_serviceProviderNodes.size() == 1 ? 0 : m_uniformVar->GetInteger(0, m_serviceProviderNodes.size() - 1);
        destinationNode = m_serviceProviderNodes[vspIndex];
    }
    else 
    {
        // Randomly select a Vehicle
        int vIndex = m_otherVehicleNodes.size() == 1 ? 0 : m_uniformVar->GetInteger(0, m_otherVehicleNodes.size() - 1);
        destinationNode = m_otherVehicleNodes[vIndex];
    }

    Ptr<Ipv4> ipv4 = destinationNode->GetObject<Ipv4>();
    Ipv4Address destinationAddr = ipv4->GetAddress(1, 0).GetLocal();

    RequestService(destinationNode->GetId(), destinationAddr, m_rsuAddress);
    Simulator::Schedule(Seconds(m_serviceInterval), &Vehicle::PerformServiceRequest, this, toVSP);
}

NS_OBJECT_ENSURE_REGISTERED(Vehicle);