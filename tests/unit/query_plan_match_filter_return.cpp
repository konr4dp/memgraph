//
// Copyright 2017 Memgraph
// Created by Florijan Stamenkovic on 14.03.17.
//

#include <iterator>
#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "communication/result_stream_faker.hpp"
#include "dbms/dbms.hpp"
#include "query/context.hpp"
#include "query/exceptions.hpp"
#include "query/plan/operator.hpp"

#include "query_plan_common.hpp"

using namespace query;
using namespace query::plan;

TEST(QueryPlan, MatchReturn) {
  Dbms dbms;
  auto dba = dbms.active();

  // add a few nodes to the database
  dba->insert_vertex();
  dba->insert_vertex();
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto test_pull_count = [&](GraphView graph_view) {
    auto scan_all =
        MakeScanAll(storage, symbol_table, "n", nullptr, graph_view);
    auto output = NEXPR("n", IDENT("n"));
    auto produce = MakeProduce(scan_all.op_, output);
    symbol_table[*output->expression_] = scan_all.sym_;
    symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
    return PullAll(produce, *dba, symbol_table);
  };

  // TODO uncomment once the functionality is implemented
  // EXPECT_EQ(2, test_pull_count(GraphView::NEW));
  EXPECT_EQ(2, test_pull_count(GraphView::OLD));
  dba->insert_vertex();
  // EXPECT_EQ(3, test_pull_count(GraphView::NEW));
  EXPECT_EQ(2, test_pull_count(GraphView::OLD));
  dba->advance_command();
  EXPECT_EQ(3, test_pull_count(GraphView::OLD));
}

TEST(QueryPlan, MatchReturnCartesian) {
  Dbms dbms;
  auto dba = dbms.active();

  dba->insert_vertex().add_label(dba->label("l1"));
  dba->insert_vertex().add_label(dba->label("l2"));
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto n = MakeScanAll(storage, symbol_table, "n");
  auto m = MakeScanAll(storage, symbol_table, "m", n.op_);
  auto return_n = NEXPR("n", IDENT("n"));
  symbol_table[*return_n->expression_] = n.sym_;
  symbol_table[*return_n] = symbol_table.CreateSymbol("named_expression_1");
  auto return_m = NEXPR("m", IDENT("m"));
  symbol_table[*return_m->expression_] = m.sym_;
  symbol_table[*return_m] = symbol_table.CreateSymbol("named_expression_2");
  auto produce = MakeProduce(m.op_, return_n, return_m);

  ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
  auto result_data = result.GetResults();
  EXPECT_EQ(result_data.size(), 4);
  // ensure the result ordering is OK:
  // "n" from the results is the same for the first two rows, while "m" isn't
  EXPECT_EQ(result_data[0][0].Value<VertexAccessor>(),
            result_data[1][0].Value<VertexAccessor>());
  EXPECT_NE(result_data[0][1].Value<VertexAccessor>(),
            result_data[1][1].Value<VertexAccessor>());
}

TEST(QueryPlan, StandaloneReturn) {
  Dbms dbms;
  auto dba = dbms.active();

  // add a few nodes to the database
  dba->insert_vertex();
  dba->insert_vertex();
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto output = NEXPR("n", LITERAL(42));
  auto produce = MakeProduce(std::shared_ptr<LogicalOperator>(nullptr), output);
  symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");

  ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
  EXPECT_EQ(result.GetResults().size(), 1);
  EXPECT_EQ(result.GetResults()[0].size(), 1);
  EXPECT_EQ(result.GetResults()[0][0].Value<int64_t>(), 42);
}

TEST(QueryPlan, NodeFilterLabelsAndProperties) {
  Dbms dbms;
  auto dba = dbms.active();

  // add a few nodes to the database
  GraphDbTypes::Label label = dba->label("Label");
  GraphDbTypes::Property property = dba->property("Property");
  auto v1 = dba->insert_vertex();
  auto v2 = dba->insert_vertex();
  auto v3 = dba->insert_vertex();
  auto v4 = dba->insert_vertex();
  auto v5 = dba->insert_vertex();
  dba->insert_vertex();
  // test all combination of (label | no_label) * (no_prop | wrong_prop |
  // right_prop)
  // only v1-v3 will have the right labels
  v1.add_label(label);
  v2.add_label(label);
  v3.add_label(label);
  // v1 and v4 will have the right properties
  v1.PropsSet(property, 42);
  v2.PropsSet(property, 1);
  v4.PropsSet(property, 42);
  v5.PropsSet(property, 1);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  // make a scan all
  auto n = MakeScanAll(storage, symbol_table, "n");
  n.node_->labels_.emplace_back(label);
  n.node_->properties_[property] = LITERAL(42);

  // node filtering
  auto node_filter = std::make_shared<NodeFilter>(n.op_, n.sym_, n.node_);

  // make a named expression and a produce
  auto output = NEXPR("x", IDENT("n"));
  symbol_table[*output->expression_] = n.sym_;
  symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
  auto produce = MakeProduce(node_filter, output);

  EXPECT_EQ(1, PullAll(produce, *dba, symbol_table));

  //  test that filtering works with old records
  v4.Reconstruct();
  v4.add_label(label);
  EXPECT_EQ(1, PullAll(produce, *dba, symbol_table));
  dba->advance_command();
  EXPECT_EQ(2, PullAll(produce, *dba, symbol_table));
}

TEST(QueryPlan, NodeFilterMultipleLabels) {
  Dbms dbms;
  auto dba = dbms.active();

  // add a few nodes to the database
  GraphDbTypes::Label label1 = dba->label("label1");
  GraphDbTypes::Label label2 = dba->label("label2");
  GraphDbTypes::Label label3 = dba->label("label3");
  // the test will look for nodes that have label1 and label2
  dba->insert_vertex();                    // NOT accepted
  dba->insert_vertex().add_label(label1);  // NOT accepted
  dba->insert_vertex().add_label(label2);  // NOT accepted
  dba->insert_vertex().add_label(label3);  // NOT accepted
  auto v1 = dba->insert_vertex();          // YES accepted
  v1.add_label(label1);
  v1.add_label(label2);
  auto v2 = dba->insert_vertex();  // NOT accepted
  v2.add_label(label1);
  v2.add_label(label3);
  auto v3 = dba->insert_vertex();  // YES accepted
  v3.add_label(label1);
  v3.add_label(label2);
  v3.add_label(label3);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  // make a scan all
  auto n = MakeScanAll(storage, symbol_table, "n");
  n.node_->labels_.emplace_back(label1);
  n.node_->labels_.emplace_back(label2);

  // node filtering
  auto node_filter = std::make_shared<NodeFilter>(n.op_, n.sym_, n.node_);

  // make a named expression and a produce
  auto output = NEXPR("n", IDENT("n"));
  auto produce = MakeProduce(node_filter, output);

  // fill up the symbol table
  symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
  symbol_table[*output->expression_] = n.sym_;

  ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
  EXPECT_EQ(result.GetResults().size(), 2);
}

TEST(QueryPlan, Expand) {
  Dbms dbms;
  auto dba = dbms.active();

  // make a V-graph (v3)<-[r2]-(v1)-[r1]->(v2)
  auto v1 = dba->insert_vertex();
  v1.add_label((GraphDbTypes::Label)1);
  auto v2 = dba->insert_vertex();
  v2.add_label((GraphDbTypes::Label)2);
  auto v3 = dba->insert_vertex();
  v3.add_label((GraphDbTypes::Label)3);
  auto edge_type = dba->edge_type("Edge");
  dba->insert_edge(v1, v2, edge_type);
  dba->insert_edge(v1, v3, edge_type);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto test_expand = [&](EdgeAtom::Direction direction, GraphView graph_view) {
    auto n = MakeScanAll(storage, symbol_table, "n");
    auto r_m = MakeExpand(storage, symbol_table, n.op_, n.sym_, "r", direction,
                          false, "m", false, graph_view);

    // make a named expression and a produce
    auto output = NEXPR("m", IDENT("m"));
    symbol_table[*output->expression_] = r_m.node_sym_;
    symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
    auto produce = MakeProduce(r_m.op_, output);

    return PullAll(produce, *dba, symbol_table);
  };

  EXPECT_EQ(2, test_expand(EdgeAtom::Direction::RIGHT, GraphView::AS_IS));
  EXPECT_EQ(2, test_expand(EdgeAtom::Direction::LEFT, GraphView::AS_IS));
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::BOTH, GraphView::AS_IS));
  //
  // test that expand works well for both old and new graph state
  v1.Reconstruct();
  v2.Reconstruct();
  v3.Reconstruct();
  dba->insert_edge(v1, v2, edge_type);
  dba->insert_edge(v1, v3, edge_type);
  EXPECT_EQ(2, test_expand(EdgeAtom::Direction::RIGHT, GraphView::OLD));
  EXPECT_EQ(2, test_expand(EdgeAtom::Direction::LEFT, GraphView::OLD));
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::BOTH, GraphView::OLD));
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::RIGHT, GraphView::NEW));
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::LEFT, GraphView::NEW));
  EXPECT_EQ(8, test_expand(EdgeAtom::Direction::BOTH, GraphView::NEW));
  dba->advance_command();
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::RIGHT, GraphView::OLD));
  EXPECT_EQ(4, test_expand(EdgeAtom::Direction::LEFT, GraphView::OLD));
  EXPECT_EQ(8, test_expand(EdgeAtom::Direction::BOTH, GraphView::OLD));
}

TEST(QueryPlan, ExpandOptional) {
  Dbms dbms;
  auto dba = dbms.active();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  // graph (v2 {p: 2})<-[:T]-(v1 {p: 1})-[:T]->(v3 {p: 2})
  auto prop = dba->property("p");
  auto edge_type = dba->edge_type("T");
  auto v1 = dba->insert_vertex();
  v1.PropsSet(prop, 1);
  auto v2 = dba->insert_vertex();
  v2.PropsSet(prop, 2);
  dba->insert_edge(v1, v2, edge_type);
  auto v3 = dba->insert_vertex();
  v3.PropsSet(prop, 2);
  dba->insert_edge(v1, v3, edge_type);
  dba->advance_command();

  // MATCH (n) OPTIONAL MATCH (n)-[r]->(m)
  auto n = MakeScanAll(storage, symbol_table, "n");
  auto r_m = MakeExpand(storage, symbol_table, nullptr, n.sym_,
      "r", EdgeAtom::Direction::RIGHT, false, "m", false);
  auto optional = std::make_shared<plan::Optional>(n.op_, r_m.op_,
      std::vector<Symbol>{r_m.edge_sym_, r_m.node_sym_});

  // RETURN n, r, m
  auto n_ne = NEXPR("n", IDENT("n"));
  symbol_table[*n_ne->expression_] = n.sym_;
  symbol_table[*n_ne] = symbol_table.CreateSymbol("n");
  auto r_ne = NEXPR("r", IDENT("r"));
  symbol_table[*r_ne->expression_] = r_m.edge_sym_;
  symbol_table[*r_ne] = symbol_table.CreateSymbol("r");
  auto m_ne = NEXPR("m", IDENT("m"));
  symbol_table[*m_ne->expression_] = r_m.node_sym_;
  symbol_table[*m_ne] = symbol_table.CreateSymbol("m");
  auto produce = MakeProduce(optional, n_ne, r_ne, m_ne);

  auto results = CollectProduce(produce, symbol_table, *dba).GetResults();
  ASSERT_EQ(4, results.size());
  int v1_is_n_count = 0;
  for (auto &row : results) {
    ASSERT_EQ(row[0].type(), TypedValue::Type::Vertex);
    VertexAccessor &va = row[0].Value<VertexAccessor>();
    auto va_p = va.PropsAt(prop);
    ASSERT_EQ(va_p.type(), PropertyValue::Type::Int);
    if (va_p.Value<int64_t>() == 1) {
      v1_is_n_count++;
      EXPECT_EQ(row[1].type(), TypedValue::Type::Edge);
      EXPECT_EQ(row[2].type(), TypedValue::Type::Vertex);
    } else {
      EXPECT_EQ(row[1].type(), TypedValue::Type::Null);
      EXPECT_EQ(row[2].type(), TypedValue::Type::Null);
    }
  }
  EXPECT_EQ(2, v1_is_n_count);
}

TEST(QueryPlan, OptionalMatchEmptyDB) {
  Dbms dbms;
  auto dba = dbms.active();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  // OPTIONAL MATCH (n)
  auto n = MakeScanAll(storage, symbol_table, "n");
  // RETURN n
  auto n_ne = NEXPR("n", IDENT("n"));
  symbol_table[*n_ne->expression_] = n.sym_;
  symbol_table[*n_ne] = symbol_table.CreateSymbol("n");
  auto optional = std::make_shared<plan::Optional>(nullptr, n.op_,
      std::vector<Symbol>{n.sym_});
  auto produce = MakeProduce(optional, n_ne);

  auto results = CollectProduce(produce, symbol_table, *dba).GetResults();
  ASSERT_EQ(1, results.size());
  EXPECT_EQ(results[0][0].type(), TypedValue::Type::Null);
}

TEST(QueryPlan, ExpandExistingNode) {
  Dbms dbms;
  auto dba = dbms.active();

  // make a graph (v1)->(v2) that
  // has a recursive edge (v1)->(v1)
  auto v1 = dba->insert_vertex();
  auto v2 = dba->insert_vertex();
  auto edge_type = dba->edge_type("Edge");
  dba->insert_edge(v1, v1, edge_type);
  dba->insert_edge(v1, v2, edge_type);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto test_existing = [&](bool with_existing, int expected_result_count) {
    auto n = MakeScanAll(storage, symbol_table, "n");
    auto r_n =
        MakeExpand(storage, symbol_table, n.op_, n.sym_, "r",
                   EdgeAtom::Direction::RIGHT, false, "n", with_existing);
    if (with_existing)
      symbol_table[*r_n.node_->identifier_] =
          symbol_table[*n.node_->identifier_];

    // make a named expression and a produce
    auto output = NEXPR("n", IDENT("n"));
    symbol_table[*output->expression_] = n.sym_;
    symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
    auto produce = MakeProduce(r_n.op_, output);

    ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
    EXPECT_EQ(result.GetResults().size(), expected_result_count);
  };

  test_existing(true, 1);
  test_existing(false, 2);
}

TEST(QueryPlan, ExpandExistingEdge) {
  Dbms dbms;
  auto dba = dbms.active();

  // make a V-graph (v3)<-[r2]-(v1)-[r1]->(v2)
  auto v1 = dba->insert_vertex();
  v1.add_label((GraphDbTypes::Label)1);
  auto v2 = dba->insert_vertex();
  v2.add_label((GraphDbTypes::Label)2);
  auto v3 = dba->insert_vertex();
  v3.add_label((GraphDbTypes::Label)3);
  auto edge_type = dba->edge_type("Edge");
  dba->insert_edge(v1, v2, edge_type);
  dba->insert_edge(v1, v3, edge_type);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto test_existing = [&](bool with_existing, int expected_result_count) {
    auto i = MakeScanAll(storage, symbol_table, "i");
    auto r_j = MakeExpand(storage, symbol_table, i.op_, i.sym_, "r",
                          EdgeAtom::Direction::BOTH, false, "j", false);
    auto r_k = MakeExpand(storage, symbol_table, r_j.op_, r_j.node_sym_, "r",
                          EdgeAtom::Direction::BOTH, with_existing, "k", false);
    if (with_existing)
      symbol_table[*r_k.edge_->identifier_] =
          symbol_table[*r_j.edge_->identifier_];

    // make a named expression and a produce
    auto output = NEXPR("r", IDENT("r"));
    symbol_table[*output->expression_] = r_j.edge_sym_;
    symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
    auto produce = MakeProduce(r_k.op_, output);

    ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
    EXPECT_EQ(result.GetResults().size(), expected_result_count);

  };

  test_existing(true, 4);
  test_existing(false, 6);
}

TEST(QueryPlan, ExpandBothCycleEdgeCase) {
  // we're testing that expanding on BOTH
  // does only one expansion for a cycle
  Dbms dbms;
  auto dba = dbms.active();

  auto v = dba->insert_vertex();
  dba->insert_edge(v, v, dba->edge_type("et"));
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto n = MakeScanAll(storage, symbol_table, "n");
  auto r_ = MakeExpand(storage, symbol_table, n.op_, n.sym_, "r",
                       EdgeAtom::Direction::BOTH, false, "_", false);
  EXPECT_EQ(1, PullAll(r_.op_, *dba, symbol_table));
}

TEST(QueryPlan, EdgeFilter) {
  Dbms dbms;
  auto dba = dbms.active();

  // make an N-star expanding from (v1)
  // where only one edge will qualify
  // and there are all combinations of
  // (edge_type yes|no) * (property yes|absent|no)
  std::vector<GraphDbTypes::EdgeType> edge_types;
  for (int j = 0; j < 2; ++j)
    edge_types.push_back(dba->edge_type("et" + std::to_string(j)));
  std::vector<VertexAccessor> vertices;
  for (int i = 0; i < 7; ++i) vertices.push_back(dba->insert_vertex());
  GraphDbTypes::Property prop = dba->property("prop");
  std::vector<EdgeAccessor> edges;
  for (int i = 0; i < 6; ++i) {
    edges.push_back(
        dba->insert_edge(vertices[0], vertices[i + 1], edge_types[i % 2]));
    switch (i % 3) {
      case 0:
        edges.back().PropsSet(prop, 42);
        break;
      case 1:
        edges.back().PropsSet(prop, 100);
        break;
      default:
        break;
    }
  }
  dba->advance_command();
  for (auto &vertex : vertices) vertex.Reconstruct();
  for (auto &edge : edges) edge.Reconstruct();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto test_filter = [&]() {
    // define an operator tree for query
    // MATCH (n)-[r]->(m) RETURN m

    auto n = MakeScanAll(storage, symbol_table, "n");
    auto r_m = MakeExpand(storage, symbol_table, n.op_, n.sym_, "r",
                          EdgeAtom::Direction::RIGHT, false, "m", false);
    r_m.edge_->edge_types_.push_back(edge_types[0]);
    r_m.edge_->properties_[prop] = LITERAL(42);
    auto edge_filter =
        std::make_shared<EdgeFilter>(r_m.op_, r_m.edge_sym_, r_m.edge_);

    // make a named expression and a produce
    auto output = NEXPR("m", IDENT("m"));
    symbol_table[*output->expression_] = r_m.node_sym_;
    symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
    auto produce = MakeProduce(edge_filter, output);

    return PullAll(produce, *dba, symbol_table);
  };

  EXPECT_EQ(1, test_filter());
  // test that edge filtering always filters on old state
  for (auto &edge : edges) edge.PropsSet(prop, 42);
  EXPECT_EQ(1, test_filter());
  dba->advance_command();
  EXPECT_EQ(3, test_filter());
}

TEST(QueryPlan, EdgeFilterEmpty) {
  Dbms dbms;
  auto dba = dbms.active();

  auto v1 = dba->insert_vertex();
  auto v2 = dba->insert_vertex();
  dba->insert_edge(v1, v2, dba->edge_type("type"));
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto n = MakeScanAll(storage, symbol_table, "n");
  auto r_m = MakeExpand(storage, symbol_table, n.op_, n.sym_, "r",
                        EdgeAtom::Direction::RIGHT, false, "m", false);
  auto edge_filter =
      std::make_shared<EdgeFilter>(r_m.op_, r_m.edge_sym_, r_m.edge_);
  EXPECT_EQ(1, PullAll(edge_filter, *dba, symbol_table));
}

TEST(QueryPlan, EdgeFilterMultipleTypes) {
  Dbms dbms;
  auto dba = dbms.active();

  auto v1 = dba->insert_vertex();
  auto v2 = dba->insert_vertex();
  auto type_1 = dba->edge_type("type_1");
  auto type_2 = dba->edge_type("type_2");
  auto type_3 = dba->edge_type("type_3");
  dba->insert_edge(v1, v2, type_1);
  dba->insert_edge(v1, v2, type_2);
  dba->insert_edge(v1, v2, type_3);
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  // make a scan all
  auto n = MakeScanAll(storage, symbol_table, "n");
  auto r_m = MakeExpand(storage, symbol_table, n.op_, n.sym_, "r",
                        EdgeAtom::Direction::RIGHT, false, "m", false);
  // add a property filter
  auto edge_filter =
      std::make_shared<EdgeFilter>(r_m.op_, r_m.edge_sym_, r_m.edge_);
  r_m.edge_->edge_types_.push_back(type_1);
  r_m.edge_->edge_types_.push_back(type_2);

  // make a named expression and a produce
  auto output = NEXPR("m", IDENT("m"));
  auto produce = MakeProduce(edge_filter, output);

  // fill up the symbol table
  symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
  symbol_table[*output->expression_] = r_m.node_sym_;

  ResultStreamFaker result = CollectProduce(produce, symbol_table, *dba);
  EXPECT_EQ(result.GetResults().size(), 2);
}

TEST(QueryPlan, Filter) {
  Dbms dbms;
  auto dba = dbms.active();

  // add a 6 nodes with property 'prop', 2 have true as value
  GraphDbTypes::Property property = dba->property("Property");
  for (int i = 0; i < 6; ++i)
    dba->insert_vertex().PropsSet(property, i % 3 == 0);
  dba->insert_vertex();  // prop not set, gives NULL
  dba->advance_command();

  AstTreeStorage storage;
  SymbolTable symbol_table;

  auto n = MakeScanAll(storage, symbol_table, "n");
  auto e =
      storage.Create<PropertyLookup>(storage.Create<Identifier>("n"), property);
  symbol_table[*e->expression_] = n.sym_;
  auto f = std::make_shared<Filter>(n.op_, e);

  auto output =
      storage.Create<NamedExpression>("x", storage.Create<Identifier>("n"));
  symbol_table[*output->expression_] = n.sym_;
  symbol_table[*output] = symbol_table.CreateSymbol("named_expression_1");
  auto produce = MakeProduce(f, output);

  EXPECT_EQ(CollectProduce(produce, symbol_table, *dba).GetResults().size(), 2);
}

TEST(QueryPlan, ExpandUniquenessFilter) {
  Dbms dbms;
  auto dba = dbms.active();

  // make a graph that has (v1)->(v2) and a recursive edge (v1)->(v1)
  auto v1 = dba->insert_vertex();
  auto v2 = dba->insert_vertex();
  auto edge_type = dba->edge_type("edge_type");
  dba->insert_edge(v1, v2, edge_type);
  dba->insert_edge(v1, v1, edge_type);
  dba->advance_command();

  auto check_expand_results = [&](bool vertex_uniqueness,
                                  bool edge_uniqueness) {
    AstTreeStorage storage;
    SymbolTable symbol_table;

    auto n1 = MakeScanAll(storage, symbol_table, "n1");
    auto r1_n2 = MakeExpand(storage, symbol_table, n1.op_, n1.sym_, "r1",
                            EdgeAtom::Direction::RIGHT, false, "n2", false);
    std::shared_ptr<LogicalOperator> last_op = r1_n2.op_;
    if (vertex_uniqueness)
      last_op = std::make_shared<ExpandUniquenessFilter<VertexAccessor>>(
          last_op, r1_n2.node_sym_, std::vector<Symbol>{n1.sym_});
    auto r2_n3 =
        MakeExpand(storage, symbol_table, last_op, r1_n2.node_sym_, "r2",
                   EdgeAtom::Direction::RIGHT, false, "n3", false);
    last_op = r2_n3.op_;
    if (edge_uniqueness)
      last_op = std::make_shared<ExpandUniquenessFilter<EdgeAccessor>>(
          last_op, r2_n3.edge_sym_, std::vector<Symbol>{r1_n2.edge_sym_});
    if (vertex_uniqueness)
      last_op = std::make_shared<ExpandUniquenessFilter<VertexAccessor>>(
          last_op, r2_n3.node_sym_,
          std::vector<Symbol>{n1.sym_, r1_n2.node_sym_});

    return PullAll(last_op, *dba, symbol_table);
  };

  EXPECT_EQ(2, check_expand_results(false, false));
  EXPECT_EQ(0, check_expand_results(true, false));
  EXPECT_EQ(1, check_expand_results(false, true));
}
