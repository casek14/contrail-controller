/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <vector>
#include <string>
#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>
#include "base/test/task_test_util.h"

#include "controller/controller_ifmap.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"

#include "xml/xml_pugi.h"

#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_types.h"

using namespace pugi;

void RouterIdDepInit(Agent *agent) {
}

class ControlNodeMockBgpXmppPeer {
public:
    ControlNodeMockBgpXmppPeer() : channel_ (NULL), rx_count_(0) {
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
    }

    void HandleXmppChannelEvent(XmppChannel *channel,
                                xmps::PeerState state) {
        if (!channel_ && state == xmps::NOT_READY) {
            return;
        }
        if (state != xmps::READY) {
            assert(channel_ && channel == channel_);
            channel->UnRegisterReceive(xmps::BGP);
            channel_ = NULL;
        } else {
            if (channel_) {
                assert(channel == channel_);
            }
            //assert(channel_ && channel == channel_);
            channel->RegisterReceive(xmps::BGP,
                    boost::bind(&ControlNodeMockBgpXmppPeer::ReceiveUpdate,
                                this, _1));
            channel_ = channel;
        }
    }

    bool SendUpdate(uint8_t *msg, size_t size) {
        if (channel_ &&
            (channel_->GetPeerState() == xmps::READY)) {
            return channel_->Send(msg, size, xmps::BGP,
                   boost::bind(&ControlNodeMockBgpXmppPeer::WriteReadyCb, this, _1));
        }
        return false;
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockBgpXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentXmppUnitTest : public ::testing::Test {
protected:
    AgentXmppUnitTest() : thread_(&evm_), agent_(Agent::GetInstance()) {}

    virtual void SetUp() {

        //TestInit initilaizes xmpp connection to 127.0.0.1, so disconnect that
        //and again spawn a new one. Its required since the receive path
        //is overridden by mock class.
        //TODO later use the agent initializer
        agent_->controller()->Cleanup();
        client->WaitForIdle();
        agent_->controller()->DisConnect();
        client->WaitForIdle();

        xs1 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs2 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs3 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs4 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs5 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs6 = new XmppServer(&evm_, XmppInit::kControlNodeJID);

        xs1->Initialize(0, false);
        xs2->Initialize(0, false);
        xs3->Initialize(0, false);
        xs4->Initialize(0, false);
        xs5->Initialize(0, false);
        xs6->Initialize(0, false);

        thread_.Start();
    }

    virtual void TearDown() {

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->controller()->
                 DecommissionedPeerListSize() == 0));
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();

        TcpServerManager::DeleteServer(xs1);
        TcpServerManager::DeleteServer(xs2);
        TcpServerManager::DeleteServer(xs3);
        TcpServerManager::DeleteServer(xs4);
        TcpServerManager::DeleteServer(xs5);
        TcpServerManager::DeleteServer(xs6);

        evm_.Shutdown();
        thread_.Join();
        client->WaitForIdle();
    }

    void XmppServerConnectionInit() {

        //Init VNController for running tests
        agent_->controller()->Connect();

        //Create control-node bgp mock peer
        mock_peer1.reset(new ControlNodeMockBgpXmppPeer());
	xs1->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer1.get(), _1, _2));

        //Create control-node bgp mock peer
        mock_peer2.reset(new ControlNodeMockBgpXmppPeer());
	xs2->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer2.get(), _1, _2));

        //Create control-node bgp mock peer
        mock_peer3.reset(new ControlNodeMockBgpXmppPeer());
	xs3->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer3.get(), _1, _2));

        //Create control-node bgp mock peer
        mock_peer4.reset(new ControlNodeMockBgpXmppPeer());
	xs4->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer4.get(), _1, _2));

        //Create control-node bgp mock peer
        mock_peer5.reset(new ControlNodeMockBgpXmppPeer());
	xs5->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer5.get(), _1, _2));

        //Create control-node bgp mock peer
        mock_peer6.reset(new ControlNodeMockBgpXmppPeer());
	xs6->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                        mock_peer6.get(), _1, _2));
    }

    EventManager evm_;
    ServerThread thread_;

    XmppServer *xs1;
    XmppServer *xs2;
    XmppServer *xs3;
    XmppServer *xs4;
    XmppServer *xs5;
    XmppServer *xs6;

    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer1;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer2;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer3;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer4;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer5;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer6;
    Agent *agent_;
};


namespace {

TEST_F(AgentXmppUnitTest, XmppConnection_Discovery) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();

    //Bring down Xmpp Server
    xs1->Shutdown();
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->controller_xmpp_channel(1) == NULL);

    //Bring down Xmpp Server
    xs2->Shutdown();
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->controller_xmpp_channel(1) == NULL);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();
    EXPECT_TRUE(agent_->controller_xmpp_channel(1) == NULL);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) == (uint32_t)xs4->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    xs4->Shutdown();
    client->WaitForIdle();

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs5->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs5->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs6->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs5->Shutdown();
    client->WaitForIdle();
    EXPECT_TRUE(agent_->controller()->config_cleanup_timer().
                cleanup_timer_->running() == true);
    if (agent_->headless_agent_mode()) {
        EXPECT_TRUE(agent_->controller()->unicast_cleanup_timer().
                cleanup_timer_->running() == true);
        EXPECT_TRUE(agent_->controller()->multicast_cleanup_timer().
                cleanup_timer_->running() == true);
    }
    client->WaitForIdle();

    //TODO Ensure timers are not running and have expired

    xs6->Shutdown();
    client->WaitForIdle();

    //wait for connection establishment
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
}


TEST_F(AgentXmppUnitTest, XmppConnection_Discovery_TimedOut) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    unsigned int port = xs1->GetPort();
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Pull down both servers
    xs1->Shutdown();
    client->WaitForIdle();
    xs2->Shutdown();
    client->WaitForIdle();

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(port);
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) == port);
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs3->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.3");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.3");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    xs4->Shutdown();
    client->WaitForIdle();
    xs5->Shutdown();
    client->WaitForIdle();

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort()); // Dummy port place-holder
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.5");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs6->GetPort()); // Dummy port place-holder
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.6");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.5");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    xs6->Shutdown();
    client->WaitForIdle();

    //wait for connection establishment
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
}

TEST_F(AgentXmppUnitTest, XmppConnection_ApplyDiscovery_OrderedList) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.2");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.3");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs5->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.3");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Increment connection attempts to simulate TIMEDOUT state
    XmppConnection *connection =
       const_cast<XmppConnection *>
       (agent_->controller_xmpp_channel(0)->GetXmppChannel()->connection());
    XmppStateMachine *sm = connection->state_machine();
    uint8_t count = 0;
    while (count++ < XmppStateMachine::kMaxAttempts) {
        sm->connect_attempts_inc();
    }

    // Bring the connected Server Down, to trigger Discovery Server list walk
    xs3->Shutdown();
    client->WaitForIdle();
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    // Agent to walk the discovery list and apply new published service
    WAIT_FOR(1000, 10000,
        agent_->controller_ifmap_xmpp_port(0) == (uint32_t)xs5->GetPort());
    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.5");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Increment connection attempts to simulate TIMEDOUT state
    connection = const_cast<XmppConnection *>
       (agent_->controller_xmpp_channel(0)->GetXmppChannel()->connection());
    sm = connection->state_machine();
    count = 0;
    while (count++ < XmppStateMachine::kMaxAttempts) {
        sm->connect_attempts_inc();
    }

    // Bring the connected Server Down, to trigger Discovery Server list walk
    xs5->Shutdown();
    client->WaitForIdle();
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    // Agent to walk the discovery list and apply new published service
    WAIT_FOR(1000, 10000,
        agent_->controller_ifmap_xmpp_port(0) == (uint32_t)xs6->GetPort());
    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.6");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs4->GetPort());
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.4");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.2");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Simulate Discovery response for Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.2");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    // Increment connection attempts to simulate TIMEDOUT state
    connection = const_cast<XmppConnection *>
       (agent_->controller_xmpp_channel(1)->GetXmppChannel()->connection());
    sm = connection->state_machine();
    count = 0;
    while (count++ < XmppStateMachine::kMaxAttempts) {
        sm->connect_attempts_inc();
    }

    // Bring the connected Server Down, to trigger Discovery Server list walk
    xs2->Shutdown();
    client->WaitForIdle();
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    //wait for connection establishment
    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(0).c_str(), "127.0.0.1");
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    ASSERT_STREQ(agent_->controller_ifmap_xmpp_server(1).c_str(), "127.0.0.2");
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);


    xs1->Shutdown();
    xs2->Shutdown();
    xs3->Shutdown();
    xs4->Shutdown();
    xs5->Shutdown();
    xs6->Shutdown();
    client->WaitForIdle();

    //wait for connection establishment
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
}

TEST_F(AgentXmppUnitTest, XmppConnection_Discovery_Back2Back) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    //client->WaitForIdle();

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    //client->WaitForIdle();

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) == (uint32_t)xs4->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    //Ensure all older connections are cleaned up.
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->controller()->
                 DecommissionedPeerListSize() == 0));
   
    xs1->Shutdown();
    client->WaitForIdle();

    xs2->Shutdown();
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    xs4->Shutdown();
    client->WaitForIdle();

    xs5->Shutdown();
    client->WaitForIdle();

    xs6->Shutdown();
    client->WaitForIdle();

    //wait for connection establishment
    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    WAIT_FOR(1000, 10000,
        agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
}

}
