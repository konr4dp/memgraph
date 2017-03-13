#include <iostream>

#include "dbms/dbms.hpp"
#include "logging/default.hpp"
#include "logging/streams/stdout.cpp"
#include "query/entry.hpp"
#include "query/backend/cpp/typed_value.hpp"
#include "query/frontend/logical/operator.hpp"

using std::cout;
using std::cin;
using std::endl;

int main(int argc, char* argv[]) {
  // init arguments
  REGISTER_ARGS(argc, argv);

  // init logger
  logging::init_sync();
  logging::log->pipe(std::make_unique<Stdout>());

  // init db context
  Dbms dbms;
  query::ConsoleResultStream stream;
  query::Engine<query::ConsoleResultStream> query_engine;

  // initialize the database
  auto dba = dbms.active();

    // labels
    auto company = dba.label("Company");
    auto person = dba.label("Person");
    auto device = dba.label("Device");

  // props
  auto name = dba.property("name");
  auto age = dba.property("age");
  auto type = dba.property("type");

  // vertices
  auto memgraph = dba.insert_vertex();
  memgraph.PropsSet(name, "Memgraph");
    memgraph.add_label(company);
  auto teon = dba.insert_vertex();
  memgraph.PropsSet(name, "Teon");
  memgraph.PropsSet(age, 26);
    teon.add_label(person);
  auto mislav = dba.insert_vertex();
  memgraph.PropsSet(name, "Mislav");
  memgraph.PropsSet(age, 22);
    mislav.add_label(person);
  auto florijan = dba.insert_vertex();
  memgraph.PropsSet(name, "Florijan");
  memgraph.PropsSet(age, 31);
    florijan.add_label(person);
  auto xps_15 = dba.insert_vertex();
  memgraph.PropsSet(type, "PC");
  memgraph.PropsSet(name, "Dell XPS 15");
    xps_15.add_label(device);

  // edges
  dba.insert_edge(teon, memgraph, dba.edge_type("MEMBER_OF"));
  dba.insert_edge(mislav, memgraph, dba.edge_type("MEMBER_OF"));
  dba.insert_edge(florijan, memgraph, dba.edge_type("MEMBER_OF"));

  dba.insert_edge(teon, mislav, dba.edge_type("FRIEND_OF"));
  dba.insert_edge(mislav, teon, dba.edge_type("FRIEND_OF"));
  dba.insert_edge(florijan, mislav, dba.edge_type("FRIEND_OF"));
  dba.insert_edge(mislav, florijan, dba.edge_type("FRIEND_OF"));
  dba.insert_edge(florijan, teon, dba.edge_type("FRIEND_OF"));
  dba.insert_edge(teon, florijan, dba.edge_type("FRIEND_OF"));

  dba.insert_edge(memgraph, xps_15, dba.edge_type("OWNS"));

  dba.insert_edge(teon, xps_15, dba.edge_type("USES"));
  dba.insert_edge(mislav, xps_15, dba.edge_type("USES"));
  dba.insert_edge(florijan, xps_15, dba.edge_type("USES"));

  dba.advance_command();

  cout << "-- Memgraph Query Engine --" << endl;

  while (true) {
    // read command
    cout << "> ";
    std::string command;
    std::getline(cin, command);
    if (command == "quit") break;
    // execute command / query
    // try {
      // auto db_accessor = dbms.active();
      query_engine.Execute(command, dba, stream);
    // } catch (const std::exception& e) {
    //   cout << e.what() << endl;
    // } catch (...) {
    //   // pass
    // }
//
  }

  return 0;
}
