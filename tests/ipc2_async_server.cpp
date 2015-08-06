#include <gtest/gtest.h>

#include "simppl/stub.h"
#include "simppl/skeleton.h"
#include "simppl/dispatcher.h"
#include "simppl/interface.h"

#include <thread>


using namespace std::placeholders;


INTERFACE(AsyncServer)
{   
   Request<int> oneway;
   
   Request<int, double> add;
   Request<int, double> echo;
   
   Response<double> result;
   Response<int, double> rEcho;
   
   
   inline
   AsyncServer()
    : INIT_REQUEST(oneway)
    , INIT_REQUEST(add)
    , INIT_REQUEST(echo)
    , INIT_RESPONSE(result)
    , INIT_RESPONSE(rEcho)
    {
      add >> result;
      echo >> rEcho;
   }
};


namespace {
   

struct Client : simppl::ipc::Stub<AsyncServer>
{
   Client()   
    : simppl::ipc::Stub<AsyncServer>("s", "unix:AServerTest")    
   {
      connected >> std::bind(&Client::handleConnected, this, _1);
      result >> std::bind(&Client::handleResult, this, _1, _2);
      rEcho >> std::bind(&Client::handleEcho, this, _1, _2, _3);
   }
   
   
   void handleConnected(simppl::ipc::ConnectionState s)
   {
      EXPECT_EQ(simppl::ipc::ConnectionState::Connected, s);
      add(42, 777);
      echo(42, 3.1415);
   }
   
   
   void handleResult(const simppl::ipc::CallState& s, double d)
   {
      EXPECT_TRUE((bool)s);
      oneway(42);
   }
   
   
   void handleEcho(const simppl::ipc::CallState& s, int i, double d)
   {
      EXPECT_TRUE((bool)s);
      haveEcho_ = true;
   }
   
   bool haveEcho_ = false;
};


struct ShutdownClient : simppl::ipc::Stub<AsyncServer>
{
   ShutdownClient()   
    : simppl::ipc::Stub<AsyncServer>("s", "unix:AServerTest")    
   {
      connected >> std::bind(&ShutdownClient::handleConnected, this, _1);
      result >> std::bind(&ShutdownClient::handleResult, this, _1, _2);
   }
   
   
   void handleConnected(simppl::ipc::ConnectionState s)
   {
      EXPECT_EQ(expected_, s);
      
      if (s == simppl::ipc::ConnectionState::Connected)
      {
         add(42, 777);
         oneway(42);
      }
      
      expected_ = simppl::ipc::ConnectionState::Disconnected;
      ++count_;
   }
   
   
   void handleResult(const simppl::ipc::CallState& s, double d)
   {
      EXPECT_FALSE((bool)s);
      EXPECT_TRUE(s.isTransportError());
      
      disp().stop();
   }
   
   simppl::ipc::ConnectionState expected_ = simppl::ipc::ConnectionState::Connected;
   int count_ = 0;
};


struct Server : simppl::ipc::Skeleton<AsyncServer>
{
   Server(const char* rolename)
    : simppl::ipc::Skeleton<AsyncServer>(rolename)
   {
      oneway >> std::bind(&Server::handleOneway, this, _1);
      add >> std::bind(&Server::handleAdd, this, _1, _2);
      echo >> std::bind(&Server::handleEcho, this, _1, _2);
   }
   
   void handleOneway(int)
   {
      disp().stop();
   }
   
   void handleAdd(int i, double d)
   {
      req_ = deferResponse();
   }
   
   void handleEcho(int i, double d)
   {
      respondWith(rEcho(i, d));
      respondOn(req_, result(d));
   }
   
   simppl::ipc::ServerRequestDescriptor req_;
};


}   // anonymous namespace


/// one response can overtake another
TEST(AServer, trivial) 
{
   simppl::ipc::Dispatcher d("unix:AServerTest");
   Client c;
   Server s("s");
   
   d.addClient(c);
   d.addServer(s);
   
   d.run();
   
   EXPECT_TRUE(c.haveEcho_);
}


/// when shutting down a server, an outstanding response must be answered 
/// with a transport error
TEST(AServer, outstanding)
{
   std::unique_ptr<simppl::ipc::Dispatcher> sd(new simppl::ipc::Dispatcher("unix:AServerTest"));
   simppl::ipc::Dispatcher cd;
   
   ShutdownClient c;
   std::unique_ptr<Server> s(new Server("s"));
   
   cd.addClient(c);
   sd->addServer(*s);
   
   std::thread t([&sd, &s](){
      sd->run();
      
      s.reset();
      sd.reset();
   });
   
   cd.run();
   
   EXPECT_EQ(2, c.count_);
   
   t.join();
}