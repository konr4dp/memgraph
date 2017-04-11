#pragma once

#include <map>
#include <vector>

#include "query/frontend/ast/ast.hpp"
#include "query/frontend/semantic/symbol_table.hpp"
#include "query/typed_value.hpp"
#include "utils/assert.hpp"
#include "utils/exceptions/not_yet_implemented.hpp"

namespace query {

class Frame {
 public:
  Frame(int size) : size_(size), elems_(size_) {}

  TypedValue &operator[](const Symbol &symbol) { return elems_[symbol.position_]; }
  const TypedValue &operator[](const Symbol &symbol) const {
    return elems_[symbol.position_];
  }

 private:
  int size_;
  std::vector<TypedValue> elems_;
};

class ExpressionEvaluator : public TreeVisitorBase {
 public:
  ExpressionEvaluator(Frame &frame, const SymbolTable &symbol_table)
      : frame_(frame), symbol_table_(symbol_table) {}

  /** When evaluting @c RecordAccessor, use @c SwitchNew to get the new data, as
   * modified during the current command.
   */
  auto &SwitchNew() {
    use_new_ = true;
    return *this;
  };

  /** When evaluting @c RecordAccessor, use @c SwitchOld to get the old data,
   * before the modification done by the current command.
   */
  auto &SwitchOld() {
    use_new_ = false;
    return *this;
  };

  /**
   * Removes and returns the last value from the result stack.
   * Consumers of this function are PostVisit functions for
   * expressions that consume subexpressions, as well as top
   * level expression consumers.
   */
  auto PopBack() {
    debug_assert(result_stack_.size() > 0, "Result stack empty");
    auto last = result_stack_.back();
    result_stack_.pop_back();
    return last;
  }

  using TreeVisitorBase::Visit;
  using TreeVisitorBase::PostVisit;

  void PostVisit(NamedExpression &named_expression) override {
    auto symbol = symbol_table_.at(named_expression);
    frame_[symbol] = PopBack();
  }

  void Visit(Identifier &ident) override {
    auto value = frame_[symbol_table_.at(ident)];
    SwitchAccessors(value);
    result_stack_.emplace_back(std::move(value));
  }

#define BINARY_OPERATOR_VISITOR(OP_NODE, CPP_OP)             \
  void PostVisit(OP_NODE &) override {                       \
    auto expression2 = PopBack();                            \
    auto expression1 = PopBack();                            \
    result_stack_.push_back(expression1 CPP_OP expression2); \
  }

#define UNARY_OPERATOR_VISITOR(OP_NODE, CPP_OP) \
  void PostVisit(OP_NODE &) override {          \
    auto expression = PopBack();                \
    result_stack_.push_back(CPP_OP expression); \
  }

  BINARY_OPERATOR_VISITOR(OrOperator, ||);
  BINARY_OPERATOR_VISITOR(XorOperator, ^);
  BINARY_OPERATOR_VISITOR(AndOperator, &&);
  BINARY_OPERATOR_VISITOR(AdditionOperator, +);
  BINARY_OPERATOR_VISITOR(SubtractionOperator, -);
  BINARY_OPERATOR_VISITOR(MultiplicationOperator, *);
  BINARY_OPERATOR_VISITOR(DivisionOperator, /);
  BINARY_OPERATOR_VISITOR(ModOperator, %);
  BINARY_OPERATOR_VISITOR(NotEqualOperator, !=);
  BINARY_OPERATOR_VISITOR(EqualOperator, ==);
  BINARY_OPERATOR_VISITOR(LessOperator, <);
  BINARY_OPERATOR_VISITOR(GreaterOperator, >);
  BINARY_OPERATOR_VISITOR(LessEqualOperator, <=);
  BINARY_OPERATOR_VISITOR(GreaterEqualOperator, >=);

  UNARY_OPERATOR_VISITOR(NotOperator, !);
  UNARY_OPERATOR_VISITOR(UnaryPlusOperator, +);
  UNARY_OPERATOR_VISITOR(UnaryMinusOperator, -);

  void PostVisit(IsNullOperator &) override {
    auto expression = PopBack();
    result_stack_.push_back(
        TypedValue(expression.type() == TypedValue::Type::Null));
  }

#undef BINARY_OPERATOR_VISITOR
#undef UNARY_OPERATOR_VISITOR

  void PostVisit(PropertyLookup &property_lookup) override {
    auto expression_result = PopBack();
    switch (expression_result.type()) {
      case TypedValue::Type::Vertex:
        result_stack_.emplace_back(
            expression_result.Value<VertexAccessor>().PropsAt(
                property_lookup.property_));
        break;
      case TypedValue::Type::Edge: {
        result_stack_.emplace_back(
            expression_result.Value<EdgeAccessor>().PropsAt(
                property_lookup.property_));
        break;
      }
      case TypedValue::Type::Map:
        // TODO implement me
        throw NotYetImplemented();
        break;

      default:
        throw TypedValueException(
            "Expected Node, Edge or Map for property lookup");
    }
  }

  void Visit(Literal &literal) override {
    // TODO: no need to evaluate constants, we can write it to frame in one of
    // the previous phases.
    result_stack_.push_back(literal.value_);
  }

  void Visit(Aggregation &aggregation) override {
    auto value = frame_[symbol_table_.at(aggregation)];
    // Aggregation is probably always simple type, but let's switch accessor
    // just to be sure.
    SwitchAccessors(value);
    result_stack_.emplace_back(std::move(value));
  }

 private:
  // If the given TypedValue contains accessors, switch them to New or Old,
  // depending on use_new_ flag.
  void SwitchAccessors(TypedValue &value) {
    switch (value.type()) {
      case TypedValue::Type::Vertex: {
        auto &vertex = value.Value<VertexAccessor>();
        if (use_new_)
          vertex.SwitchNew();
        else
          vertex.SwitchOld();
        break;
      }
      case TypedValue::Type::Edge: {
        auto &edge = value.Value<EdgeAccessor>();
        if (use_new_)
          edge.SwitchNew();
        else
          edge.SwitchOld();
        break;
      }
      case TypedValue::Type::List: {
        auto &list = value.Value<std::vector<TypedValue>>();
        for (auto &list_value : list) SwitchAccessors(list_value);
      }
      case TypedValue::Type::Map: {
        auto &map = value.Value<std::map<std::string, TypedValue>>();
        for (auto &kv : map) SwitchAccessors(kv.second);
      }
      default:
        break;
    }
  }

  Frame &frame_;
  const SymbolTable &symbol_table_;
  std::list<TypedValue> result_stack_;
  // If true, use SwitchNew on evaluated record accessors. This should be done
  // only in expressions which may return one. E.g. identifier, list indexing.
  bool use_new_ = false;
};
}
